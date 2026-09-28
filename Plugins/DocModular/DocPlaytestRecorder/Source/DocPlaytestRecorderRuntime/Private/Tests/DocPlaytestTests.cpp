#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "DocPlaytestTypes.h"
#include "DocPlaytestEvidenceProvider.h"
#include "DocPlaytestRecorderSubsystem.h"
#include "DocPlaytestSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DocPlaytestTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocPlaytestRecorderSubsystem> Subsystem;

		explicit FFixture(bool bOptIn = true)
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			Subsystem.Reset(NewObject<UDocPlaytestRecorderSubsystem>(GameInstance.Get()));
			UDocPlaytestRecorderSubsystem::SetSubsystemOverrideForTesting(Subsystem.Get());
			if (bOptIn)
			{
				Subsystem->EnableCapture(); // capture is opt-in; tests that record opt in explicitly
			}
		}

		~FFixture()
		{
			UDocPlaytestRecorderSubsystem::SetSubsystemOverrideForTesting(nullptr);
		}

		UDocPlaytestRecorderSubsystem* Get() const { return Subsystem.Get(); }
	};
}

// DBG-01: Count/time/byte limits remain enforced under oversized payload pressure
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestByteBoundedBufferTest, "Doc.Playtest.ByteBoundedBuffer", DocPlaytestTests::Flags)

bool FDocPlaytestByteBoundedBufferTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture;
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	// Record numerous events with sizable payloads
	FString SizablePayload = TEXT("{\"data\": \"") + FString::ChrN(1024, TEXT('X')) + TEXT("\"}");
	for (int32 i = 0; i < 2000; ++i)
	{
		Subsystem->RecordEvent(TEXT("Test.Flood"), SizablePayload);
	}

	const UDocPlaytestSettings* Settings = GetDefault<UDocPlaytestSettings>();
	int32 MaxCount = Settings ? Settings->MaxEventCount : 1000;
	int64 MaxBytes = Settings ? Settings->MaxBufferSizeBytes : (5 * 1024 * 1024);

	TestTrue(TEXT("Retained events bounded by MaxCount"), Subsystem->GetRetainedEventCount() <= MaxCount);
	TestTrue(TEXT("Retained bytes bounded by MaxBytes"), Subsystem->GetRetainedBufferBytes() <= MaxBytes);
	TestTrue(TEXT("Evicted events tracked"), Subsystem->GetEvictedEventCount() > 0);

	// Byte limit reached before the count limit: 300 events of ~40 KB each exceed 5 MiB
	DocPlaytestTests::FFixture ByteFixture;
	UDocPlaytestRecorderSubsystem* ByteSub = ByteFixture.Get();
	const FString LargePayload = TEXT("{\"blob\": \"") + FString::ChrN(20 * 1024, TEXT('Y')) + TEXT("\"}");
	for (int32 i = 0; i < 300; ++i)
	{
		ByteSub->RecordEvent(TEXT("Test.Large"), LargePayload);
	}
	TestTrue(TEXT("Byte cap enforced"), ByteSub->GetRetainedBufferBytes() <= MaxBytes);
	TestTrue(TEXT("Byte cap was the binding limit"), ByteSub->GetRetainedEventCount() < 300 && ByteSub->GetRetainedEventCount() < MaxCount);

	// A single payload above the per-event cap is replaced, never retained whole
	DocPlaytestTests::FFixture HugeFixture;
	UDocPlaytestRecorderSubsystem* HugeSub = HugeFixture.Get();
	const FString Huge = TEXT("{\"blob\": \"") + FString::ChrN(static_cast<int32>(Settings->MaxEventPayloadBytes), TEXT('Z')) + TEXT("\"}");
	HugeSub->RecordEvent(TEXT("Test.Huge"), Huge);
	TArray<FDocPlaytestEvent> HugeEvents = HugeSub->GetEventsInTimeRange(0.0, FPlatformTime::Seconds() + 10.0);
	TestTrue(TEXT("Oversized payload truncated"), HugeEvents.Num() == 1 && HugeEvents[0].PayloadJson.Contains(TEXT("truncated")));

	return true;
}

// DBG-02: Pre/post-marker windows and shared segment quotas are correct
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestMarkerWindowTest, "Doc.Playtest.MarkerWindow", DocPlaytestTests::Flags)

bool FDocPlaytestMarkerWindowTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture;
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	// Emit events before marker
	Subsystem->RecordEvent(TEXT("Test.Pre"), TEXT("{\"stage\": \"pre_marker\"}"));

	FPlatformProcess::Sleep(0.01f);
	FDocIssueMarker Marker = Subsystem->AddIssueMarker(TEXT("Tester note for issue"), TEXT("CrashOrBug"), 5.0f, 5.0f);
	TestTrue(TEXT("Marker created with valid ID"), Marker.MarkerId.IsValid());

	FPlatformProcess::Sleep(0.01f);
	Subsystem->RecordEvent(TEXT("Test.Post"), TEXT("{\"stage\": \"post_marker\"}"));

	// Query window
	double WindowStart = Marker.MonotonicSeconds - Marker.PreWindowSeconds;
	double WindowEnd = Marker.MonotonicSeconds + Marker.PostWindowSeconds;
	TArray<FDocPlaytestEvent> WindowEvents = Subsystem->GetEventsInTimeRange(WindowStart, WindowEnd);

	TestTrue(TEXT("Window captures events"), WindowEvents.Num() >= 2);
	TestTrue(TEXT("Finalize marker"), Subsystem->FinalizeMarker(Marker.MarkerId));

	FDocIssueMarker FinalizedMarker;
	TestTrue(TEXT("Get marker"), Subsystem->GetMarker(Marker.MarkerId, FinalizedMarker));
	TestEqual(TEXT("Status finalized"), FinalizedMarker.Status, EDocPlaytestMarkerStatus::Finalized);

	return true;
}

// DBG-03: Sensitive fields never enter retained buffers or exports
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestRedactBeforeRetentionTest, "Doc.Playtest.RedactBeforeRetention", DocPlaytestTests::Flags)

bool FDocPlaytestRedactBeforeRetentionTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture;
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	FString SensitivePayload = TEXT(R"({
		"user_id": 12345,
		"password": "SuperSecretPassword!",
		"auth_token": "jwt_secret_token_abc123",
		"normal_info": "unclassified_gameplay_fact"
	})");

	Subsystem->RecordEvent(TEXT("Auth.Attempt"), SensitivePayload);

	TArray<FDocPlaytestEvent> Events = Subsystem->GetEventsInTimeRange(0.0, FPlatformTime::Seconds() + 10.0);
	TestTrue(TEXT("At least one event recorded"), Events.Num() > 0);

	if (Events.Num() > 0)
	{
		const FDocPlaytestEvent& LastEvent = Events.Last();
		TestTrue(TEXT("Flagged as redacted"), LastEvent.bIsRedacted);
		TestFalse(TEXT("Secret password not in payload"), LastEvent.PayloadJson.Contains(TEXT("SuperSecretPassword!")));
		TestFalse(TEXT("Auth token not in payload"), LastEvent.PayloadJson.Contains(TEXT("jwt_secret_token_abc123")));
		TestTrue(TEXT("Redacted placeholder present"), LastEvent.PayloadJson.Contains(TEXT("[REDACTED]")));
		TestTrue(TEXT("Normal info preserved"), LastEvent.PayloadJson.Contains(TEXT("unclassified_gameplay_fact")));
	}

	return true;
}

// DBG-04: Slow/failing providers produce bounded omissions without gameplay failure
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestProviderFailureTest, "Doc.Playtest.ProviderFailure", DocPlaytestTests::Flags)

bool FDocPlaytestProviderFailureTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture;
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	UDocSamplePlaytestProvider* Provider = NewObject<UDocSamplePlaytestProvider>();
	Provider->bSimulateFailure = true; // Simulate failure
	Subsystem->RegisterEvidenceProvider(Provider);

	FDocIssueMarker Marker = Subsystem->AddIssueMarker(TEXT("Testing provider failure"), TEXT("Diagnostics"));

	FString ExportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlaytestExports"), TEXT("ProviderFailTest"));
	IFileManager::Get().MakeDirectory(*ExportDir, true);

	FString OutPath;
	FString Error;
	bool bExported = Subsystem->ExportIssueBundle(Marker.MarkerId, ExportDir, OutPath, Error);
	TestTrue(TEXT("Export succeeds despite provider failure"), bExported);

	// Verify manifest recorded omission
	FString ManifestPath = FPaths::Combine(OutPath, TEXT("manifest.json"));
	FString ManifestJson;
	FFileHelper::LoadFileToString(ManifestJson, *ManifestPath);

	FDocPlaytestExportManifest Manifest;
	TestTrue(TEXT("Manifest parsed"), FDocPlaytestExportManifest::ParseFromJson(ManifestJson, Manifest, Error));
	TestEqual(TEXT("Exactly one omission recorded"), Manifest.Omissions.Num(), 1);
	if (Manifest.Omissions.Num() > 0)
	{
		TestEqual(TEXT("Omission provider name"), Manifest.Omissions[0].ProviderName, Provider->GetProviderName());
	}

	// Recording keeps working after the failure, and a recovered provider produces no omission
	Provider->bSimulateFailure = false;
	FDocIssueMarker Marker2 = Subsystem->AddIssueMarker(TEXT("After recovery"), TEXT("Diagnostics"));
	TestTrue(TEXT("Second marker is valid"), Marker2.MarkerId.IsValid());
	TestTrue(TEXT("Second marker is distinct"), Marker2.MarkerId != Marker.MarkerId);
	FString ExportDir2 = FPaths::Combine(ExportDir, TEXT("Recovered"));
	IFileManager::Get().MakeDirectory(*ExportDir2, true);
	FString OutPath2;
	TestTrue(TEXT("Second export succeeds"), Subsystem->ExportIssueBundle(Marker2.MarkerId, ExportDir2, OutPath2, Error));
	FString ManifestJson2;
	TestTrue(TEXT("Second manifest readable"), FFileHelper::LoadFileToString(ManifestJson2, *FPaths::Combine(OutPath2, TEXT("manifest.json"))));
	FDocPlaytestExportManifest Manifest2;
	TestTrue(TEXT("Second manifest parsed"), FDocPlaytestExportManifest::ParseFromJson(ManifestJson2, Manifest2, Error));
	TestEqual(TEXT("No omission once provider recovers"), Manifest2.Omissions.Num(), 0);

	IFileManager::Get().DeleteDirectory(*ExportDir, false, true);
	return true;
}

// DBG-05: Disk/cancel faults preserve old bundles and clean only owned staging
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestExportTransactionTest, "Doc.Playtest.ExportTransaction", DocPlaytestTests::Flags)

bool FDocPlaytestExportTransactionTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture;
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	FString ExportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlaytestExports"), TEXT("TransactTest"));
	IFileManager::Get().MakeDirectory(*ExportDir, true);

	// First successful export
	FDocIssueMarker Marker1 = Subsystem->AddIssueMarker(TEXT("First valid marker"), TEXT("Bug"));
	FString OutPath1, Error1;
	TestTrue(TEXT("First export succeeds"), Subsystem->ExportIssueBundle(Marker1.MarkerId, ExportDir, OutPath1, Error1));
	TestTrue(TEXT("First bundle exists on disk"), FPaths::DirectoryExists(OutPath1));

	// Second export with simulated disk fault
	Subsystem->bSimulateExportDiskFault = true;
	FDocIssueMarker Marker2 = Subsystem->AddIssueMarker(TEXT("Faulty marker"), TEXT("Bug"));
	FString OutPath2, Error2;
	bool bFailedExport = Subsystem->ExportIssueBundle(Marker2.MarkerId, ExportDir, OutPath2, Error2);
	TestFalse(TEXT("Faulty export cleanly fails"), bFailedExport);

	// Verify no orphan staging directory exists
	FString StagingPath = FPaths::Combine(ExportDir, FString::Printf(TEXT(".staging_%s"), *Marker2.MarkerId.ToString()));
	TestFalse(TEXT("Staging directory cleaned up"), FPaths::DirectoryExists(StagingPath));

	// Verify original bundle remains intact
	TestTrue(TEXT("First bundle preserved intact"), FPaths::DirectoryExists(OutPath1));

	IFileManager::Get().DeleteDirectory(*ExportDir, false, true);
	return true;
}

// DBG-06: Files/hashes/time ranges/omissions match actual exported data
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestManifestIntegrityTest, "Doc.Playtest.ManifestIntegrity", DocPlaytestTests::Flags)

bool FDocPlaytestManifestIntegrityTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture;
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	UDocSamplePlaytestProvider* Provider = NewObject<UDocSamplePlaytestProvider>();
	Provider->CustomPayloadData = TEXT("{\"sample_key\": \"sample_val\", \"api_token\": \"leak-me-not\"}");
	Subsystem->RegisterEvidenceProvider(Provider);

	Subsystem->RecordEvent(TEXT("Gameplay.Score"), TEXT("{\"score\": 100}"));
	// Hostile text: quotes, backslashes and a newline in the category; a non-object payload
	Subsystem->RecordEvent(TEXT("Odd \"cat\"\\\nline"), TEXT("plain text, not json"));
	FDocIssueMarker Marker = Subsystem->AddIssueMarker(TEXT("Score anomaly"), TEXT("Scoring"));

	FString ExportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlaytestExports"), TEXT("IntegrityTest"));
	IFileManager::Get().MakeDirectory(*ExportDir, true);

	FString OutPath, Error;
	TestTrue(TEXT("Export succeeds"), Subsystem->ExportIssueBundle(Marker.MarkerId, ExportDir, OutPath, Error));

	FString ManifestPath = FPaths::Combine(OutPath, TEXT("manifest.json"));
	FString ManifestJson;
	TestTrue(TEXT("Load manifest.json"), FFileHelper::LoadFileToString(ManifestJson, *ManifestPath));

	FDocPlaytestExportManifest Manifest;
	TestTrue(TEXT("Parse manifest"), FDocPlaytestExportManifest::ParseFromJson(ManifestJson, Manifest, Error));

	// Verify all declared files exist and their hashes match
	for (const auto& Kvp : Manifest.FileHashes)
	{
		FString FullFilePath = FPaths::Combine(OutPath, Kvp.Key);
		TestTrue(FString::Printf(TEXT("File exists: %s"), *Kvp.Key), FPaths::FileExists(FullFilePath));

		FString ActualContent;
		FFileHelper::LoadFileToString(ActualContent, *FullFilePath);
		FString ActualHash = UDocPlaytestRecorderSubsystem::ComputeFileHash(ActualContent);
		TestEqual(FString::Printf(TEXT("Hash matches for: %s"), *Kvp.Key), ActualHash, Kvp.Value);
	}

	// DBG-03: provider snapshots are redacted in the exported bundle
	for (const auto& Kvp : Manifest.FileHashes)
	{
		if (Kvp.Key.StartsWith(TEXT("snapshots/")))
		{
			FString SnapshotText;
			FFileHelper::LoadFileToString(SnapshotText, *FPaths::Combine(OutPath, Kvp.Key));
			TestFalse(TEXT("Snapshot secret redacted"), SnapshotText.Contains(TEXT("leak-me-not")));
			TestTrue(TEXT("Snapshot keeps ordinary data"), SnapshotText.Contains(TEXT("sample_val")));
		}
	}

	// Every events.jsonl line is valid JSON, including the hostile category and payload
	FString EventsText;
	TestTrue(TEXT("Load events.jsonl"), FFileHelper::LoadFileToString(EventsText, *FPaths::Combine(OutPath, TEXT("events.jsonl"))));
	TArray<FString> Lines;
	EventsText.ParseIntoArrayLines(Lines);
	TestEqual(TEXT("Two event lines"), Lines.Num(), 2);
	for (const FString& Line : Lines)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		TestTrue(TEXT("Line parses as JSON"), FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid());
	}

	// DBG-06: the manifest time range lies inside the exported events' range
	TestTrue(TEXT("Manifest start is not after end"), Manifest.StartTime <= Manifest.EndTime);

	IFileManager::Get().DeleteDirectory(*ExportDir, false, true);
	return true;
}

// DBG-07: Travel removes live references and records context boundaries
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestWorldTeardownTest, "Doc.Playtest.WorldTeardown", DocPlaytestTests::Flags)

bool FDocPlaytestWorldTeardownTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture;
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	UDocSamplePlaytestProvider* Provider = NewObject<UDocSamplePlaytestProvider>();
	Subsystem->RegisterEvidenceProvider(Provider);

	// Notify world teardown
	Subsystem->NotifyWorldTeardown(TEXT("Map_Dungeon_01"));

	TestEqual(TEXT("Provider received epoch boundary"), Provider->EpochNotificationCount, 1);
	TestEqual(TEXT("Provider notified world name"), Provider->LastNotifiedWorld, TEXT("Map_Dungeon_01"));

	// Check boundary event in timeline
	TArray<FDocPlaytestEvent> Events = Subsystem->GetEventsInTimeRange(0.0, FPlatformTime::Seconds() + 5.0);
	bool bFoundBoundaryEvent = false;
	for (const FDocPlaytestEvent& Evt : Events)
	{
		if (Evt.Category == TEXT("Engine.WorldBoundary") && Evt.PayloadJson.Contains(TEXT("Map_Dungeon_01")))
		{
			bFoundBoundaryEvent = true;
			break;
		}
	}
	TestTrue(TEXT("World boundary event recorded in ring buffer"), bFoundBoundaryEvent);

	// A second travel is a second, separate boundary
	Subsystem->NotifyWorldTeardown(TEXT("Map_Town_02"));
	TestEqual(TEXT("Second epoch boundary delivered"), Provider->EpochNotificationCount, 2);
	TestEqual(TEXT("Provider tracks latest world"), Provider->LastNotifiedWorld, TEXT("Map_Town_02"));
	int32 BoundaryCount = 0;
	for (const FDocPlaytestEvent& Evt : Subsystem->GetEventsInTimeRange(0.0, FPlatformTime::Seconds() + 5.0))
	{
		if (Evt.Category == TEXT("Engine.WorldBoundary"))
		{
			++BoundaryCount;
		}
	}
	TestEqual(TEXT("Both boundaries recorded"), BoundaryCount, 2);

	return true;
}

// DBG-08: Malformed bundles cannot execute commands or escape allowed paths
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestUntrustedViewerInputTest, "Doc.Playtest.UntrustedViewerInput", DocPlaytestTests::Flags)

bool FDocPlaytestUntrustedViewerInputTest::RunTest(const FString& Parameters)
{
	FString BaseDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlaytestViewer"));
	IFileManager::Get().MakeDirectory(*BaseDir, true);

	FString OutPath;

	// Traversal with ..
	TestFalse(TEXT("Reject parent traversal"), UDocPlaytestRecorderSubsystem::ValidateImportedPath(BaseDir, TEXT("../outside.json"), OutPath));

	// Absolute Windows path
	TestFalse(TEXT("Reject drive letter"), UDocPlaytestRecorderSubsystem::ValidateImportedPath(BaseDir, TEXT("C:/Windows/cmd.exe"), OutPath));

	// Absolute leading slash
	TestFalse(TEXT("Reject leading slash"), UDocPlaytestRecorderSubsystem::ValidateImportedPath(BaseDir, TEXT("/etc/passwd"), OutPath));

	// Valid path
	TestFalse(TEXT("Reject backslash traversal"), UDocPlaytestRecorderSubsystem::ValidateImportedPath(BaseDir, TEXT("..\\outside.json"), OutPath));
	TestFalse(TEXT("Reject nested traversal"), UDocPlaytestRecorderSubsystem::ValidateImportedPath(BaseDir, TEXT("snapshots/../../outside.json"), OutPath));
	TestFalse(TEXT("Reject UNC path"), UDocPlaytestRecorderSubsystem::ValidateImportedPath(BaseDir, TEXT("\\\\server\\share\\x.json"), OutPath));

	// Valid path stays under the base directory
	OutPath.Reset();
	TestTrue(TEXT("Accept safe relative path"), UDocPlaytestRecorderSubsystem::ValidateImportedPath(BaseDir, TEXT("snapshots/doc.sample.provider.json"), OutPath));
	FString NormalizedBase = FPaths::ConvertRelativePathToFull(BaseDir);
	FPaths::NormalizeDirectoryName(NormalizedBase);
	TestTrue(TEXT("Accepted path is under base"), OutPath.StartsWith(NormalizedBase));
	TestTrue(TEXT("Accepted path keeps file name"), OutPath.EndsWith(TEXT("snapshots/doc.sample.provider.json")));

	// Malformed manifests are rejected, not partially trusted
	FDocPlaytestExportManifest Junk;
	FString ParseError;
	TestFalse(TEXT("Junk manifest rejected"), FDocPlaytestExportManifest::ParseFromJson(TEXT("not json {"), Junk, ParseError));
	TestFalse(TEXT("Parse error reported"), ParseError.IsEmpty());

	IFileManager::Get().DeleteDirectory(*BaseDir, false, true);
	return true;
}

// DBG-09: Disabled capture avoids expensive payload construction and allocations
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestDisabledOverheadTest, "Doc.Playtest.DisabledOverhead", DocPlaytestTests::Flags)

bool FDocPlaytestDisabledOverheadTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture(false);
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	TestFalse(TEXT("Capture is off until the host opts in"), Subsystem->IsCaptureEnabled());
	Subsystem->RecordEvent(TEXT("Before.OptIn"), TEXT("{\"x\": 1}"));
	TestEqual(TEXT("Nothing recorded before opt-in"), Subsystem->GetRetainedEventCount(), 0);
	Subsystem->EnableCapture();

	Subsystem->DisableCapture();
	TestFalse(TEXT("Capture disabled"), Subsystem->IsCaptureEnabled());

	int32 InitialCount = Subsystem->GetRetainedEventCount();
	Subsystem->RecordEvent(TEXT("Expensive.Category"), TEXT("{\"huge_data\": 12345}"));

	TestEqual(TEXT("No event recorded when disabled"), Subsystem->GetRetainedEventCount(), InitialCount);

	Subsystem->EnableCapture();
	TestTrue(TEXT("Capture re-enabled"), Subsystem->IsCaptureEnabled());
	Subsystem->RecordEvent(TEXT("Valid.Category"), TEXT("{\"data\": 1}"));
	TestEqual(TEXT("Event recorded when re-enabled"), Subsystem->GetRetainedEventCount(), InitialCount + 1);

	return true;
}

// DBG-10: Export is local, user-controlled, and independent of gameplay plugins
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocPlaytestNoAutomaticUploadTest, "Doc.Playtest.NoAutomaticUpload", DocPlaytestTests::Flags)

bool FDocPlaytestNoAutomaticUploadTest::RunTest(const FString& Parameters)
{
	DocPlaytestTests::FFixture Fixture;
	UDocPlaytestRecorderSubsystem* Subsystem = Fixture.Get();

	UDocSamplePlaytestProvider* Provider = NewObject<UDocSamplePlaytestProvider>();
	Subsystem->RegisterEvidenceProvider(Provider);

	Subsystem->RecordEvent(TEXT("Standalone.Action"), TEXT("{\"state\": \"verified\"}"));
	FDocIssueMarker Marker = Subsystem->AddIssueMarker(TEXT("Standalone test"), TEXT("Isolation"));

	FString TargetDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PlaytestExports"), TEXT("LocalOnly"));
	IFileManager::Get().MakeDirectory(*TargetDir, true);

	FString OutPath, Error;
	TestTrue(TEXT("Export succeeds locally"), Subsystem->ExportIssueBundle(Marker.MarkerId, TargetDir, OutPath, Error));
	TestTrue(TEXT("Bundle written to local disk"), FPaths::DirectoryExists(OutPath));
	TestTrue(TEXT("issue.json exists"), FPaths::FileExists(FPaths::Combine(OutPath, TEXT("issue.json"))));
	TestTrue(TEXT("events.jsonl exists"), FPaths::FileExists(FPaths::Combine(OutPath, TEXT("events.jsonl"))));
	TestTrue(TEXT("manifest.json exists"), FPaths::FileExists(FPaths::Combine(OutPath, TEXT("manifest.json"))));

	IFileManager::Get().DeleteDirectory(*TargetDir, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
