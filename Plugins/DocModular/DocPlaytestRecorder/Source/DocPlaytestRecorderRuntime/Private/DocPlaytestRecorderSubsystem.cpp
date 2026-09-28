#include "DocPlaytestRecorderSubsystem.h"
#include "DocPlaytestSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

static TWeakObjectPtr<UDocPlaytestRecorderSubsystem> GDocPlaytestTestOverride;

UDocPlaytestRecorderSubsystem::UDocPlaytestRecorderSubsystem()
{
	SessionId = FGuid::NewGuid().ToString();
}

UDocPlaytestRecorderSubsystem* UDocPlaytestRecorderSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocPlaytestRecorderSubsystem* Override = GDocPlaytestTestOverride.Get())
	{
		return Override;
	}

	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GI = World->GetGameInstance();
	return GI ? GI->GetSubsystem<UDocPlaytestRecorderSubsystem>() : nullptr;
}

void UDocPlaytestRecorderSubsystem::SetSubsystemOverrideForTesting(UDocPlaytestRecorderSubsystem* Override)
{
	GDocPlaytestTestOverride = Override;
}

void UDocPlaytestRecorderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bCaptureEnabled = false; // explicit opt-in only
	SessionId = FGuid::NewGuid().ToString();
}

void UDocPlaytestRecorderSubsystem::Deinitialize()
{
	EventRingBuffer.Empty();
	Markers.Empty();
	RegisteredProviders.Empty();

	Super::Deinitialize();
}

TArray<FString> UDocPlaytestRecorderSubsystem::GetRedactedKeywords() const
{
	const UDocPlaytestSettings* Settings = GetDefault<UDocPlaytestSettings>();
	return Settings ? Settings->RedactedKeywords : TArray<FString>();
}

void UDocPlaytestRecorderSubsystem::EnableCapture()
{
	bCaptureEnabled = true;
}

void UDocPlaytestRecorderSubsystem::DisableCapture()
{
	bCaptureEnabled = false;
}

FString UDocPlaytestRecorderSubsystem::SanitizeAndRedact(const FString& InputJson, const TArray<FString>& RedactedKeys)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InputJson);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		// Not a JSON object: check raw string for sensitive keywords
		FString Result = InputJson;
		for (const FString& Keyword : RedactedKeys)
		{
			if (Result.Contains(Keyword, ESearchCase::IgnoreCase))
			{
				return TEXT("\"[REDACTED]\"");
			}
		}
		return InputJson;
	}

	TFunction<void(TSharedPtr<FJsonObject>)> RedactObject;
	RedactObject = [&](TSharedPtr<FJsonObject> Obj)
	{
		if (!Obj.IsValid()) return;

		for (auto& Pair : Obj->Values)
		{
			FString KeyLower = FString(Pair.Key).ToLower();
			bool bShouldRedact = false;
			for (const FString& Keyword : RedactedKeys)
			{
				if (KeyLower.Contains(Keyword.ToLower()))
				{
					bShouldRedact = true;
					break;
				}
			}

			if (bShouldRedact)
			{
				Pair.Value = MakeShared<FJsonValueString>(TEXT("[REDACTED]"));
			}
			else if (Pair.Value->Type == EJson::Object)
			{
				RedactObject(Pair.Value->AsObject());
			}
			else if (Pair.Value->Type == EJson::Array)
			{
				for (auto& Item : Pair.Value->AsArray())
				{
					if (Item->Type == EJson::Object)
					{
						RedactObject(Item->AsObject());
					}
				}
			}
		}
	};

	RedactObject(JsonObject);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	return OutputString;
}

void UDocPlaytestRecorderSubsystem::RecordEvent(const FString& Category, const FString& PayloadJson)
{
	// DBG-09: Disabled capture avoids expensive payload construction and allocations
	if (!bCaptureEnabled)
	{
		return;
	}

	const UDocPlaytestSettings* Settings = GetDefault<UDocPlaytestSettings>();
	TArray<FString> RedactedKeys;
	if (Settings)
	{
		RedactedKeys = Settings->RedactedKeywords;
	}

	FDocPlaytestEvent NewEvent;
	NewEvent.EventId = FGuid::NewGuid();
	NewEvent.Timestamp = FDateTime::UtcNow();
	NewEvent.MonotonicSeconds = FPlatformTime::Seconds();
	NewEvent.Category = Category;

	// DBG-01: per-event cap before any parsing work
	const int64 MaxPayloadBytes = Settings ? Settings->MaxEventPayloadBytes : (64 * 1024);
	const bool bOversized = static_cast<int64>(PayloadJson.Len()) * static_cast<int64>(sizeof(TCHAR)) > MaxPayloadBytes;

	// DBG-03: Redact sensitive fields before retention in buffer
	NewEvent.PayloadJson = bOversized ? FString::Printf(TEXT("{\"truncated\": true, \"original_chars\": %d}"), PayloadJson.Len())
		: SanitizeAndRedact(PayloadJson, RedactedKeys);
	NewEvent.PayloadSizeBytes = NewEvent.PayloadJson.Len() * sizeof(TCHAR);
	NewEvent.bIsRedacted = NewEvent.PayloadJson.Contains(TEXT("[REDACTED]"));

	EventRingBuffer.Add(NewEvent);
	CurrentBufferBytes += NewEvent.PayloadSizeBytes;

	EnforceBufferLimits();
}

void UDocPlaytestRecorderSubsystem::EnforceBufferLimits()
{
	const UDocPlaytestSettings* Settings = GetDefault<UDocPlaytestSettings>();
	int32 MaxCount = Settings ? Settings->MaxEventCount : 1000;
	int64 MaxBytes = Settings ? Settings->MaxBufferSizeBytes : (5 * 1024 * 1024);
	float MaxWindowSec = Settings ? Settings->MaxRetentionWindowSeconds : 60.0f;

	double Now = FPlatformTime::Seconds();
	double CutoffTime = Now - MaxWindowSec;

	// Count the oldest events that violate any limit, then remove them in one shift (not one copy per event).
	int32 Evict = 0;
	int64 Bytes = CurrentBufferBytes;
	while (Evict < EventRingBuffer.Num())
	{
		const FDocPlaytestEvent& Oldest = EventRingBuffer[Evict];
		const bool bTooOld = Oldest.MonotonicSeconds < CutoffTime;
		const bool bTooMany = EventRingBuffer.Num() - Evict > MaxCount;
		const bool bTooBig = Bytes > MaxBytes; // DBG-01
		if (!bTooOld && !bTooMany && !bTooBig)
		{
			break;
		}
		Bytes -= Oldest.PayloadSizeBytes;
		++Evict;
	}
	if (Evict > 0)
	{
		EventRingBuffer.RemoveAt(0, Evict);
		EvictedEventCount += Evict;
		CurrentBufferBytes = Bytes;
	}

	if (CurrentBufferBytes < 0)
	{
		CurrentBufferBytes = 0;
	}
}

TArray<FDocPlaytestEvent> UDocPlaytestRecorderSubsystem::GetEventsInTimeRange(double StartMonotonicSeconds, double EndMonotonicSeconds) const
{
	TArray<FDocPlaytestEvent> Result;
	for (const FDocPlaytestEvent& Evt : EventRingBuffer)
	{
		if (Evt.MonotonicSeconds >= StartMonotonicSeconds && Evt.MonotonicSeconds <= EndMonotonicSeconds)
		{
			Result.Add(Evt);
		}
	}
	return Result;
}

FDocIssueMarker UDocPlaytestRecorderSubsystem::AddIssueMarker(const FString& TesterNote, const FString& Category, float PreWindowSeconds, float PostWindowSeconds)
{
	FDocIssueMarker Marker;
	Marker.MarkerId = FGuid::NewGuid();
	Marker.Timestamp = FDateTime::UtcNow();
	Marker.MonotonicSeconds = FPlatformTime::Seconds();
	Marker.TesterNote = TesterNote;
	Marker.Category = Category;
	Marker.PreWindowSeconds = PreWindowSeconds;
	Marker.PostWindowSeconds = PostWindowSeconds;
	Marker.Status = EDocPlaytestMarkerStatus::Active;

	Markers.Add(Marker.MarkerId, Marker);
	return Marker;
}

bool UDocPlaytestRecorderSubsystem::FinalizeMarker(const FGuid& MarkerId)
{
	FDocIssueMarker* Found = Markers.Find(MarkerId);
	if (Found)
	{
		Found->Status = EDocPlaytestMarkerStatus::Finalized;
		return true;
	}
	return false;
}

bool UDocPlaytestRecorderSubsystem::GetMarker(const FGuid& MarkerId, FDocIssueMarker& OutMarker) const
{
	const FDocIssueMarker* Found = Markers.Find(MarkerId);
	if (Found)
	{
		OutMarker = *Found;
		return true;
	}
	return false;
}

void UDocPlaytestRecorderSubsystem::RegisterEvidenceProvider(UObject* ProviderObj)
{
	if (ProviderObj)
	{
		if (IDocPlaytestEvidenceProvider* Provider = Cast<IDocPlaytestEvidenceProvider>(ProviderObj))
		{
			RegisteredProviders.Add(Provider->GetProviderName(), ProviderObj);
		}
	}
}

void UDocPlaytestRecorderSubsystem::UnregisterEvidenceProvider(const FString& ProviderName)
{
	RegisteredProviders.Remove(ProviderName);
}

void UDocPlaytestRecorderSubsystem::NotifyWorldTeardown(const FString& WorldName)
{
	// Notify providers
	for (auto& Pair : RegisteredProviders)
	{
		if (UObject* Obj = Pair.Value.Get())
		{
			if (IDocPlaytestEvidenceProvider* Provider = Cast<IDocPlaytestEvidenceProvider>(Obj))
			{
				Provider->NotifyWorldEpochBoundary(WorldName);
			}
		}
	}

	// Insert world teardown context boundary event into event timeline (DBG-07)
	FString BoundaryPayload = FString::Printf(TEXT("{\"event\": \"world_teardown\", \"world\": \"%s\"}"), *WorldName);
	RecordEvent(TEXT("Engine.WorldBoundary"), BoundaryPayload);
}

FString UDocPlaytestRecorderSubsystem::ComputeFileHash(const FString& FileContent)
{
	return FMD5::HashAnsiString(*FileContent);
}

bool UDocPlaytestRecorderSubsystem::ValidateImportedPath(const FString& BaseDir, const FString& RelativePath, FString& OutFullPath)
{
	FString CleanRel = RelativePath.TrimStartAndEnd();
	CleanRel.ReplaceInline(TEXT("\\"), TEXT("/"));

	if (CleanRel.StartsWith(TEXT("/")) || CleanRel.StartsWith(TEXT("\\")) ||
		CleanRel.Contains(TEXT("..")) || CleanRel.Contains(TEXT(":")))
	{
		return false;
	}

	FString NormalizedBase = FPaths::ConvertRelativePathToFull(BaseDir);
	FPaths::NormalizeDirectoryName(NormalizedBase);

	FString Combined = FPaths::Combine(NormalizedBase, CleanRel);
	FPaths::CollapseRelativeDirectories(Combined);

	if (!Combined.StartsWith(NormalizedBase))
	{
		return false;
	}

	OutFullPath = Combined;
	return true;
}

bool UDocPlaytestRecorderSubsystem::ExportIssueBundle(const FGuid& MarkerId, const FString& TargetDirectory, FString& OutExportPath, FString& OutError)
{
	FDocIssueMarker* Marker = Markers.Find(MarkerId);
	if (!Marker)
	{
		OutError = TEXT("Marker not found.");
		return false;
	}
	if (Marker->Status == EDocPlaytestMarkerStatus::Cancelled)
	{
		OutError = TEXT("Export was cancelled for this marker.");
		return false;
	}

	const TArray<FString> RedactedKeys = GetRedactedKeywords();
	IFileManager& FM = IFileManager::Get();

	// Create staging directory: .staging_<MarkerId> (only this directory is ever cleaned on failure)
	FString StagingDir = FPaths::Combine(TargetDirectory, FString::Printf(TEXT(".staging_%s"), *MarkerId.ToString()));
	FM.DeleteDirectory(*StagingDir, false, true);
	if (!FM.MakeDirectory(*StagingDir, true))
	{
		OutError = TEXT("Could not create staging directory.");
		return false;
	}
	auto Abort = [&FM, &StagingDir, &OutError](const FString& Why)
	{
		FM.DeleteDirectory(*StagingDir, false, true);
		OutError = Why;
		return false;
	};
	// Every write is checked; a failed write aborts the whole export.
	auto WriteChecked = [this](const FString& Content, const FString& Path)
	{
		return !bSimulateExportDiskFault && FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	};

	FDocPlaytestExportManifest Manifest;
	Manifest.SessionId = SessionId;
	Manifest.IssueId = MarkerId;
	Manifest.TesterNote = SanitizeAndRedact(Marker->TesterNote, RedactedKeys);
	Manifest.StartTime = Marker->Timestamp - FTimespan::FromSeconds(Marker->PreWindowSeconds);
	Manifest.EndTime = Marker->Timestamp + FTimespan::FromSeconds(Marker->PostWindowSeconds);

	// 1. Export Issue details: issue.json
	TSharedPtr<FJsonObject> IssueObj = MakeShared<FJsonObject>();
	IssueObj->SetStringField(TEXT("marker_id"), Marker->MarkerId.ToString());
	IssueObj->SetStringField(TEXT("tester_note"), Manifest.TesterNote);
	IssueObj->SetStringField(TEXT("category"), Marker->Category);
	IssueObj->SetStringField(TEXT("timestamp"), Marker->Timestamp.ToIso8601());
	IssueObj->SetNumberField(TEXT("pre_window"), Marker->PreWindowSeconds);
	IssueObj->SetNumberField(TEXT("post_window"), Marker->PostWindowSeconds);

	FString IssueJson;
	TSharedRef<TJsonWriter<>> IssueWriter = TJsonWriterFactory<>::Create(&IssueJson);
	FJsonSerializer::Serialize(IssueObj.ToSharedRef(), IssueWriter);

	FString IssuePath = FPaths::Combine(StagingDir, TEXT("issue.json"));
	if (!WriteChecked(IssueJson, IssuePath))
	{
		return Abort(TEXT("Failed to write issue.json."));
	}
	Manifest.ExportedFiles.Add(TEXT("issue.json"), IFileManager::Get().FileSize(*IssuePath));
	Manifest.FileHashes.Add(TEXT("issue.json"), ComputeFileHash(IssueJson));

	// 2. Export Correlated Events: events.jsonl (DBG-02)
	double WindowStart = Marker->MonotonicSeconds - Marker->PreWindowSeconds;
	double WindowEnd = Marker->MonotonicSeconds + Marker->PostWindowSeconds;
	TArray<FDocPlaytestEvent> WindowEvents = GetEventsInTimeRange(WindowStart, WindowEnd);

	// Each line is serialized by the JSON writer, so categories and payloads are always escaped.
	FString EventsJsonl;
	for (const FDocPlaytestEvent& Evt : WindowEvents)
	{
		TSharedPtr<FJsonObject> Line = MakeShared<FJsonObject>();
		Line->SetStringField(TEXT("id"), Evt.EventId.ToString());
		Line->SetStringField(TEXT("time"), Evt.Timestamp.ToIso8601());
		Line->SetStringField(TEXT("cat"), Evt.Category);
		TSharedPtr<FJsonObject> Data;
		TSharedRef<TJsonReader<>> DataReader = TJsonReaderFactory<>::Create(Evt.PayloadJson);
		if (FJsonSerializer::Deserialize(DataReader, Data) && Data.IsValid())
		{
			Line->SetObjectField(TEXT("data"), Data);
		}
		else
		{
			Line->SetStringField(TEXT("data"), Evt.PayloadJson); // non-object payloads are kept as an escaped string
		}
		FString LineText;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> LineWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&LineText);
		FJsonSerializer::Serialize(Line.ToSharedRef(), LineWriter);
		EventsJsonl += LineText + TEXT("\n");
	}
	if (WindowEvents.Num() > 0)
	{
		// DBG-06: the manifest range describes the data actually exported
		Manifest.StartTime = WindowEvents[0].Timestamp;
		Manifest.EndTime = WindowEvents.Last().Timestamp;
	}

	FString EventsPath = FPaths::Combine(StagingDir, TEXT("events.jsonl"));
	if (!WriteChecked(EventsJsonl, EventsPath))
	{
		return Abort(TEXT("Failed to write events.jsonl."));
	}
	Manifest.ExportedFiles.Add(TEXT("events.jsonl"), IFileManager::Get().FileSize(*EventsPath));
	Manifest.FileHashes.Add(TEXT("events.jsonl"), ComputeFileHash(EventsJsonl));
	Manifest.TotalEvents = WindowEvents.Num();
	Manifest.EvictedEvents = EvictedEventCount;

	// 3. Export Snapshots from registered providers (DBG-04: ProviderFailure)
	FString SnapshotsDir = FPaths::Combine(StagingDir, TEXT("snapshots"));
	IFileManager::Get().MakeDirectory(*SnapshotsDir, true);

	for (auto& Pair : RegisteredProviders)
	{
		if (UObject* Obj = Pair.Value.Get())
		{
			if (IDocPlaytestEvidenceProvider* Provider = Cast<IDocPlaytestEvidenceProvider>(Obj))
			{
				FDocDiagnosticSnapshot Snapshot;
				FString ProviderError;
				if (Provider->CaptureSnapshot(Snapshot, ProviderError))
				{
					// Provider names become file names: keep them to a safe character set.
					FString SafeName = Provider->GetProviderName();
					for (TCHAR& Ch : SafeName.GetCharArray())
					{
						if (Ch != 0 && !FChar::IsAlnum(Ch) && Ch != TEXT('_') && Ch != TEXT('-')) { Ch = TEXT('_'); }
					}
					const FString RedactedSnapshot = SanitizeAndRedact(Snapshot.DataJson, RedactedKeys); // DBG-03
					FString SnapshotFilename = FString::Printf(TEXT("snapshots/%s.json"), *SafeName);
					FString FullSnapshotPath = FPaths::Combine(StagingDir, SnapshotFilename);
					if (!WriteChecked(RedactedSnapshot, FullSnapshotPath))
					{
						return Abort(TEXT("Failed to write a provider snapshot."));
					}
					Manifest.ExportedFiles.Add(SnapshotFilename, IFileManager::Get().FileSize(*FullSnapshotPath));
					Manifest.FileHashes.Add(SnapshotFilename, ComputeFileHash(RedactedSnapshot));
				}
				else
				{
					// Bounded omission record (DBG-04)
					FDocDiagnosticOmission Omission;
					Omission.ProviderName = Provider->GetProviderName();
					Omission.Reason = ProviderError;
					Omission.Timestamp = FDateTime::UtcNow();
					Manifest.Omissions.Add(Omission);
				}
			}
		}
	}

	// 4. Export Manifest: manifest.json (DBG-06)
	FString ManifestJson = Manifest.ToJson();
	FString ManifestPath = FPaths::Combine(StagingDir, TEXT("manifest.json"));
	if (!WriteChecked(ManifestJson, ManifestPath))
	{
		return Abort(TEXT("Failed to write manifest.json."));
	}

	// 5. Commit: an existing bundle is set aside (not deleted) until the new one is in place.
	FString FinalBundleDir = FPaths::Combine(TargetDirectory, FString::Printf(TEXT("issue_%s"), *MarkerId.ToString()));
	FString PreviousDir = FPaths::Combine(TargetDirectory, FString::Printf(TEXT(".previous_%s"), *MarkerId.ToString()));
	const bool bHadPrevious = FPaths::DirectoryExists(FinalBundleDir);
	if (bHadPrevious)
	{
		FM.DeleteDirectory(*PreviousDir, false, true);
		if (!FM.Move(*PreviousDir, *FinalBundleDir, true, true, true))
		{
			return Abort(TEXT("Could not set aside the previous bundle; it was left untouched."));
		}
	}

	if (!FM.Move(*FinalBundleDir, *StagingDir, true, true, true))
	{
		if (bHadPrevious)
		{
			FM.Move(*FinalBundleDir, *PreviousDir, true, true, true); // put the old bundle back
		}
		return Abort(TEXT("Failed to commit final bundle directory; previous bundle preserved."));
	}
	if (bHadPrevious)
	{
		FM.DeleteDirectory(*PreviousDir, false, true);
	}

	Marker->Status = EDocPlaytestMarkerStatus::Exported;
	OutExportPath = FinalBundleDir;
	return true;
}

void UDocPlaytestRecorderSubsystem::CancelExport(const FGuid& MarkerId)
{
	FDocIssueMarker* Marker = Markers.Find(MarkerId);
	if (Marker)
	{
		Marker->Status = EDocPlaytestMarkerStatus::Cancelled;
	}
}
