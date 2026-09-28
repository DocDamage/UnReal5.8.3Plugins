#include "DocSaveGameSubsystem.h"
#include "DocSaveableComponent.h"
#include "DocSaveStorage.h"
#include "DocSaveLog.h"
#include "DocCoreTags.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocSaveGameSubsystem)

namespace DocSavePrivate
{
	FDocSaveEnvelope::FLimits LimitsFromSettings()
	{
		const UDocSaveSettings* S = GetDefault<UDocSaveSettings>();
		FDocSaveEnvelope::FLimits L;
		L.MaxFileBytes = S->MaxFileBytes;
		L.MaxUncompressedBytes = S->MaxUncompressedBytes;
		return L;
	}
}

static TWeakObjectPtr<UDocSaveGameSubsystem> GDocSaveTestOverride;

void UDocSaveGameSubsystem::SetSubsystemOverrideForTesting(UDocSaveGameSubsystem* Override)
{
	GDocSaveTestOverride = Override;
}

void UDocSaveGameSubsystem::InitializeForTesting(TSharedPtr<IDocSaveStorageBackend> Backend)
{
	Storage = Backend;
	bShuttingDown = false;
}

UDocSaveGameSubsystem* UDocSaveGameSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocSaveGameSubsystem* Override = GDocSaveTestOverride.Get())
	{
		return Override;
	}
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UDocSaveGameSubsystem>() : nullptr;
}

void UDocSaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UDocSaveSettings* Settings = GetDefault<UDocSaveSettings>();
	Storage = MakeShared<FDocLocalFileSaveBackend>(FPaths::Combine(FPaths::ProjectSavedDir(), Settings->SaveFolder));
	for (const FString& Path : Settings->AllowedPayloadStructs)
	{
		AllowedStructs.Add(Path);
	}
}

void UDocSaveGameSubsystem::Deinitialize()
{
	bShuttingDown = true;
	Live.Reset();
	Super::Deinitialize();
}

void UDocSaveGameSubsystem::SetStorageBackendForTesting(TSharedPtr<IDocSaveStorageBackend> Backend)
{
	Storage = Backend;
}

FGuid UDocSaveGameSubsystem::NamespaceFor(const UWorld* World) const
{
	return World ? FGuid::NewDeterministicGuid(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())) : FGuid();
}

// ---------------------------------------------------------------------------
// Allowlists and payload codec
// ---------------------------------------------------------------------------

void UDocSaveGameSubsystem::AllowPayloadStruct(const UScriptStruct* Struct)
{
	if (Struct)
	{
		AllowedStructs.Add(Struct->GetPathName());
	}
}

void UDocSaveGameSubsystem::AllowRuntimeSpawnClass(TSubclassOf<AActor> Class)
{
	if (Class)
	{
		AllowedClasses.AddUnique(Class.Get());
	}
}

bool UDocSaveGameSubsystem::IsPayloadStructAllowed(const FString& TypeId) const
{
	return AllowedStructs.Contains(TypeId);
}

bool UDocSaveGameSubsystem::IsSpawnClassAllowed(const UClass* Class) const
{
	if (!Class || !Class->IsChildOf(AActor::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
	{
		return false;
	}
	for (const TWeakObjectPtr<UClass>& Allowed : AllowedClasses)
	{
		if (Allowed.IsValid() && Class->IsChildOf(Allowed.Get()))
		{
			return true;
		}
	}
	for (const TSoftClassPtr<AActor>& Soft : GetDefault<UDocSaveSettings>()->AllowedRuntimeSpawnClasses)
	{
		const UClass* Allowed = Soft.Get();
		if (Allowed && Class->IsChildOf(Allowed))
		{
			return true;
		}
	}
	return false;
}

void UDocSaveGameSubsystem::RegisterMigration(const FString& TypeId, int32 FromVersion, FMigrationFunc Func)
{
	Migrations.FindOrAdd(TypeId).Add(FromVersion, MoveTemp(Func));
}

void UDocSaveGameSubsystem::RegisterCurrentVersion(const FString& TypeId, int32 Version)
{
	CurrentVersions.Add(TypeId, Version);
}

FDocSystemResult UDocSaveGameSubsystem::EncodePayload(const FDocSavePayload& Payload, FDocSaveComponentRecord& OutRecord) const
{
	const UScriptStruct* Struct = Payload.Data.GetScriptStruct();
	if (Payload.ComponentKey.IsNone() || !Struct)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Payload needs a ComponentKey and data"));
	}
	const FString TypeId = Struct->GetPathName();
	if (!IsPayloadStructAllowed(TypeId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied,
			FString::Printf(TEXT("Payload struct %s is not allowlisted (Project Settings → Doc Save, or AllowPayloadStruct)"), *TypeId));
	}
	OutRecord.ComponentKey = Payload.ComponentKey;
	OutRecord.TypeId = TypeId;
	OutRecord.Version = Payload.Version;
	DocCoreSerialization::EncodeStruct(Struct, Payload.Data.GetMemory(), OutRecord.Payload);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSaveGameSubsystem::DecodePayload(const FDocSaveComponentRecord& Record, FDocSavePayload& OutPayload) const
{
	if (!IsPayloadStructAllowed(Record.TypeId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, FString::Printf(TEXT("Payload type %s not allowlisted"), *Record.TypeId));
	}
	const UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *Record.TypeId);
	if (!Struct)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Payload type %s not found in this build"), *Record.TypeId));
	}

	TArray<uint8> Bytes = Record.Payload;
	int32 Version = Record.Version;
	const int32* Current = CurrentVersions.Find(Record.TypeId);
	if (Current)
	{
		if (Version > *Current)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
				FString::Printf(TEXT("%s version %d is newer than supported %d"), *Record.TypeId, Version, *Current));
		}
		const TMap<int32, FMigrationFunc>* Steps = Migrations.Find(Record.TypeId);
		while (Version < *Current)
		{
			const FMigrationFunc* Step = Steps ? Steps->Find(Version) : nullptr;
			if (!Step || !(*Step)(Version, Bytes))
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed,
					FString::Printf(TEXT("No working migration for %s from version %d"), *Record.TypeId, Version));
			}
			++Version;
		}
	}

	OutPayload.ComponentKey = Record.ComponentKey;
	OutPayload.Version = Version;
	OutPayload.Data.InitializeAs(Struct);
	if (!DocCoreSerialization::DecodeStruct(Struct, OutPayload.Data.GetMutableMemory(), Bytes))
	{
		OutPayload.Data.Reset();
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, FString::Printf(TEXT("Payload %s is corrupt"), *Record.TypeId));
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Participants and records
// ---------------------------------------------------------------------------

bool UDocSaveGameSubsystem::RegisterSaveable(UDocSaveableComponent* Saveable)
{
	if (!Saveable || bShuttingDown)
	{
		return false;
	}
	const FDocPersistentObjectId Id = Saveable->GetDocPersistentId_Implementation();
	if (!Id.IsValid())
	{
		UE_LOG(LogDocSave, Error, TEXT("%s has no valid persistent id; not registered"), *Saveable->GetPathName());
		return false;
	}
	if (const TWeakObjectPtr<UDocSaveableComponent>* Existing = Live.Find(Id))
	{
		if (Existing->IsValid() && Existing->Get() != Saveable)
		{
			// Visible failure: never silently assign a fresh id (that would hide broken continuity).
			UE_LOG(LogDocSave, Error, TEXT("Duplicate persistent id %s on %s and %s"), *Id.ToString(),
				*GetPathNameSafe(Existing->Get()), *Saveable->GetPathName());
			return false;
		}
	}

	FDocSaveObjectRecord* Record = Records.Find(Id);
	if (Record && Record->Lifecycle == EDocSaveLifecycle::IntentionallyDestroyed)
	{
		// Tombstoned: suppress the actor that tried to come back (e.g. streamed in again).
		if (AActor* Owner = Saveable->GetOwner())
		{
			Owner->SetActorHiddenInGame(true);
			Owner->SetActorEnableCollision(false);
			Owner->Destroy();
		}
		return false;
	}

	Live.Add(Id, Saveable);
	if (Record && !bRestoring)
	{
		// Streamed back in: re-apply the retained state exactly once.
		TArray<FString> Errors;
		Saveable->ApplyRecord(*Record, Errors);
		for (const FString& E : Errors) { UE_LOG(LogDocSave, Warning, TEXT("Stream-in restore: %s"), *E); }
	}
	FDocSaveObjectRecord& Updated = Records.FindOrAdd(Id);
	Updated.Id = Id;
	Updated.Lifecycle = EDocSaveLifecycle::Registered;
	Updated.bRuntimeSpawned = Saveable->IsRuntimeSpawned();
	return true;
}

void UDocSaveGameSubsystem::UnregisterSaveable(UDocSaveableComponent* Saveable, EDocSaveLifecycle Lifecycle)
{
	if (!Saveable)
	{
		return;
	}
	const FDocPersistentObjectId Id = Saveable->GetDocPersistentId_Implementation();
	const TWeakObjectPtr<UDocSaveableComponent>* Existing = Live.Find(Id);
	if (!Existing || Existing->Get() != Saveable)
	{
		return;
	}
	Live.Remove(Id);

	FDocSaveObjectRecord* Record = Records.Find(Id);
	if (Record && Record->Lifecycle == EDocSaveLifecycle::IntentionallyDestroyed)
	{
		return; // tombstone wins
	}
	// Capture while the owner is still valid; unload is not destruction.
	FDocSaveObjectRecord Captured = Saveable->CaptureRecord();
	Captured.Lifecycle = Lifecycle;
	Records.Add(Id, MoveTemp(Captured));
}

FDocSystemResult UDocSaveGameSubsystem::MarkPersistentlyDestroyed(const FDocPersistentObjectId& Id)
{
	if (!Id.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid id"));
	}
	FDocSaveObjectRecord& Record = Records.FindOrAdd(Id);
	if (Record.Lifecycle == EDocSaveLifecycle::IntentionallyDestroyed)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already tombstoned"));
	}
	Record.Id = Id;
	Record.Lifecycle = EDocSaveLifecycle::IntentionallyDestroyed;
	Record.Components.Reset();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSaveGameSubsystem::MarkVirtualized(const FDocPersistentObjectId& Id, bool bVirtualized)
{
	FDocSaveObjectRecord* Record = Records.Find(Id);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No record"));
	}
	if (Record->Lifecycle == EDocSaveLifecycle::IntentionallyDestroyed)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Tombstoned"));
	}
	Record->Lifecycle = bVirtualized ? EDocSaveLifecycle::Virtualized : EDocSaveLifecycle::StreamedOut;
	return FDocSystemResult::MakeSuccess();
}

bool UDocSaveGameSubsystem::IsTombstoned(const FDocPersistentObjectId& Id) const
{
	const FDocSaveObjectRecord* Record = Records.Find(Id);
	return Record && Record->Lifecycle == EDocSaveLifecycle::IntentionallyDestroyed;
}

void UDocSaveGameSubsystem::SetFeatureRecord(const FDocFeatureRecord& Record)
{
	for (FDocFeatureRecord& Existing : FeatureRecords)
	{
		if (Existing.FeatureId == Record.FeatureId && Existing.Owner == Record.Owner)
		{
			Existing = Record;
			return;
		}
	}
	FeatureRecords.Add(Record);
}

bool UDocSaveGameSubsystem::GetFeatureRecord(FName FeatureId, const FDocOwnerScope& Owner, FDocFeatureRecord& OutRecord) const
{
	for (const FDocFeatureRecord& Existing : FeatureRecords)
	{
		if (Existing.FeatureId == FeatureId && Existing.Owner == Owner)
		{
			OutRecord = Existing;
			return true;
		}
	}
	return false;
}

void UDocSaveGameSubsystem::CaptureLiveRecords()
{
	for (const TPair<FDocPersistentObjectId, TWeakObjectPtr<UDocSaveableComponent>>& Pair : Live)
	{
		const UDocSaveableComponent* Saveable = Pair.Value.Get();
		if (!Saveable || IsTombstoned(Pair.Key))
		{
			continue;
		}
		FDocSaveObjectRecord Captured = Saveable->CaptureRecord();
		Captured.Lifecycle = EDocSaveLifecycle::Registered;
		Records.Add(Pair.Key, MoveTemp(Captured));
	}
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

bool UDocSaveGameSubsystem::BuildSaveBody(const FString& Slot, UWorld* World, EDocSaveSlotCategory Category, TArray<uint8>& OutBody, FDocSaveHeader& OutHeaderTemplate)
{
	// Coordinated capture barrier: all live participants captured in this one game-thread pass.
	CaptureLiveRecords();

	FDocSaveBody Body;
	Records.GenerateValueArray(Body.Objects);
	Body.Objects.Sort([](const FDocSaveObjectRecord& A, const FDocSaveObjectRecord& B) { return A.Id.ToString() < B.Id.ToString(); });
	Body.FeatureRecords = FeatureRecords;
	Body.Metadata.Add(TEXT("Slot"), Slot);
	DocCoreSerialization::Encode(Body, OutBody);

	OutHeaderTemplate = FDocSaveHeader();
	OutHeaderTemplate.ProducerGameVersion = GetDefault<UDocSaveSettings>()->GameVersion;
	OutHeaderTemplate.WorldNamespace = NamespaceFor(World);
	OutHeaderTemplate.Timestamp = FDateTime::UtcNow();
	OutHeaderTemplate.Category = Category;
	return OutBody.Num() > 0;
}

EDocResultOutcome DocSavePrivate::CommitRevision(IDocSaveStorageBackend& Backend, const FString& Slot, FDocSaveHeader Header,
	const TArray<uint8>& Body, bool bCompress, int64 MinRevision, int64 MaxFileBytes, int64& OutRevision, FString& OutError)
{
	// Thread-safe: byte arrays and the backend only. Writes to one slot are serialized by the
	// caller, so reading the committed header here and writing the next revision cannot race
	// within this process. The revision is always above whatever the slot already holds, so an
	// older capture can never replace a newer committed save (including saves from earlier sessions).
	int64 Existing = 0;
	{
		TArray<uint8> Current;
		FString Ignored;
		FDocSaveHeader CurrentHeader;
		if (Backend.ReadSlot(Slot, Current, MaxFileBytes, Ignored) && FDocSaveEnvelope::ReadHeader(Current, CurrentHeader, Ignored))
		{
			Existing = CurrentHeader.SaveRevision;
		}
	}
	Header.SaveRevision = FMath::Max(MinRevision, Existing + 1);
	TArray<uint8> File;
	if (!FDocSaveEnvelope::Write(Header, Body, bCompress, File, OutError))
	{
		return EDocResultOutcome::Failed;
	}
	if (File.Num() > MaxFileBytes)
	{
		OutError = TEXT("Save exceeds MaxFileBytes");
		return EDocResultOutcome::Failed;
	}
	if (!Backend.WriteSlot(Slot, File, OutError))
	{
		return EDocResultOutcome::Failed;
	}
	OutRevision = Header.SaveRevision;
	return EDocResultOutcome::Succeeded;
}

FDocRequestHandle UDocSaveGameSubsystem::SaveToSlot(const UObject* WorldContextObject, const FString& Slot, EDocSaveSlotCategory Category)
{
	const FDocRequestHandle Handle = FDocHandleAllocator::MakeHandle(FDocHandleAllocator::NextOperationId(), 1);
	if (!FDocLocalFileSaveBackend::IsValidSlotName(Slot))
	{
		Broadcast(true, Handle, FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid slot name")), 0);
		return Handle;
	}
	if (bRestoring)
	{
		Broadcast(true, Handle, FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Cannot save while restoring")), 0);
		return Handle;
	}
	if (!Storage.IsValid())
	{
		Broadcast(true, Handle, FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No storage backend")), 0);
		return Handle;
	}
	FSlotState& State = Slots.FindOrAdd(Slot);
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (State.bWriteInFlight)
	{
		// Coalesce: one more write after the current one; every caller learns the revision it gets.
		if (!State.Queued.IsSet())
		{
			State.Queued.Emplace();
		}
		State.Queued->Callers.Add(Handle);
		State.Queued->Category = Category;
		State.Queued->World = World;
		return Handle;
	}
	FPendingSave Pending;
	Pending.Callers.Add(Handle);
	Pending.Category = Category;
	Pending.World = World;
	StartWrite(Slot, MoveTemp(Pending));
	return Handle;
}

void UDocSaveGameSubsystem::StartWrite(const FString& Slot, FPendingSave&& Pending)
{
	FSlotState& State = Slots.FindOrAdd(Slot);
	TArray<uint8> Body;
	FDocSaveHeader Header;
	if (!BuildSaveBody(Slot, Pending.World.Get(), Pending.Category, Body, Header))
	{
		FinishWrite(Slot, MoveTemp(Pending.Callers), EDocResultOutcome::Failed, TEXT("Capture produced no data"), 0);
		return;
	}
	State.bWriteInFlight = true;

	TWeakObjectPtr<UDocSaveGameSubsystem> WeakThis(this);
	TSharedPtr<IDocSaveStorageBackend> Backend = Storage;
	TArray<FDocRequestHandle> Callers = MoveTemp(Pending.Callers);
	const UDocSaveSettings* Settings = GetDefault<UDocSaveSettings>();
	const int64 MaxBytes = Settings->MaxFileBytes;
	const bool bCompress = Settings->bCompress;
	const int64 MinRevision = State.LastCommittedRevision + 1;
	// Immutable snapshot (Body, Header) crosses to the worker; no UObject is touched there.
	Async(EAsyncExecution::ThreadPool, [WeakThis, Backend, Slot, Body = MoveTemp(Body), Header, Callers = MoveTemp(Callers), MaxBytes, bCompress, MinRevision]() mutable
	{
		int64 Revision = 0;
		FString Error = TEXT("No storage backend");
		const EDocResultOutcome Outcome = Backend.IsValid()
			? DocSavePrivate::CommitRevision(*Backend, Slot, Header, Body, bCompress, MinRevision, MaxBytes, Revision, Error)
			: EDocResultOutcome::Unavailable;
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Slot, Callers = MoveTemp(Callers), Outcome, Error, Revision]() mutable
		{
			if (UDocSaveGameSubsystem* Self = WeakThis.Get())
			{
				Self->FinishWrite(Slot, MoveTemp(Callers), Outcome, Error, Revision);
			}
		});
	});
}

void UDocSaveGameSubsystem::FinishWrite(const FString& Slot, TArray<FDocRequestHandle> Callers, EDocResultOutcome Outcome, const FString& Error, int64 Revision)
{
	FSlotState& State = Slots.FindOrAdd(Slot);
	State.bWriteInFlight = false;
	const bool bOk = Outcome == EDocResultOutcome::Succeeded;
	if (bOk)
	{
		State.LastCommittedRevision = FMath::Max(State.LastCommittedRevision, Revision);
	}
	const FDocSystemResult Result = bOk ? FDocSystemResult::MakeSuccess()
		: FDocSystemResult::MakeFailure(Outcome, Error, DocCoreTags::Error_Storage);
	for (const FDocRequestHandle& Caller : Callers)
	{
		Broadcast(true, Caller, Result, bOk ? Revision : 0);
	}
	if (State.Queued.IsSet() && !bShuttingDown)
	{
		FPendingSave Next = MoveTemp(State.Queued.GetValue());
		State.Queued.Reset();
		StartWrite(Slot, MoveTemp(Next));
	}
}

FDocSystemResult UDocSaveGameSubsystem::SaveToSlotBlocking(UWorld* World, const FString& Slot, EDocSaveSlotCategory Category, int64& OutRevision)
{
	OutRevision = 0;
	if (!FDocLocalFileSaveBackend::IsValidSlotName(Slot))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid slot name"));
	}
	if (!Storage.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No storage backend"));
	}
	if (bRestoring)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Cannot save while restoring"));
	}
	FSlotState& State = Slots.FindOrAdd(Slot);
	if (State.bWriteInFlight)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("An async write to this slot is in flight"));
	}
	TArray<uint8> Body;
	FDocSaveHeader Header;
	if (!BuildSaveBody(Slot, World, Category, Body, Header))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Capture produced no data"));
	}
	const UDocSaveSettings* Settings = GetDefault<UDocSaveSettings>();
	FString Error;
	int64 Revision = 0;
	const EDocResultOutcome Outcome = DocSavePrivate::CommitRevision(*Storage, Slot, Header, Body, Settings->bCompress,
		State.LastCommittedRevision + 1, Settings->MaxFileBytes, Revision, Error);
	if (Outcome != EDocResultOutcome::Succeeded)
	{
		return FDocSystemResult::MakeFailure(Outcome, Error, DocCoreTags::Error_Storage);
	}
	State.LastCommittedRevision = Revision;
	OutRevision = Revision;
	return FDocSystemResult::MakeSuccess();
}

int64 UDocSaveGameSubsystem::GetLastCommittedRevision(const FString& Slot) const
{
	const FSlotState* State = Slots.Find(Slot);
	return State ? State->LastCommittedRevision : 0;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

FDocRequestHandle UDocSaveGameSubsystem::LoadFromSlot(const UObject* WorldContextObject, const FString& Slot)
{
	const FDocRequestHandle Handle = FDocHandleAllocator::MakeHandle(FDocHandleAllocator::NextOperationId(), 1);
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!FDocLocalFileSaveBackend::IsValidSlotName(Slot) || !World || !Storage.IsValid())
	{
		Broadcast(false, Handle, FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid slot, world or storage")), 0);
		return Handle;
	}
	if (bRestoring)
	{
		Broadcast(false, Handle, FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("A restore is already running")), 0);
		return Handle;
	}

	TWeakObjectPtr<UDocSaveGameSubsystem> WeakThis(this);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	TSharedPtr<IDocSaveStorageBackend> Backend = Storage;
	const FDocSaveEnvelope::FLimits Limits = DocSavePrivate::LimitsFromSettings();
	Async(EAsyncExecution::ThreadPool, [WeakThis, WeakWorld, Backend, Slot, Handle, Limits]()
	{
		// Worker: bounded reads and envelope validation (no UObjects).
		DocSavePrivate::FLoadedFile Main;
		DocSavePrivate::FLoadedFile Backup;
		DocSavePrivate::ReadAndValidate(*Backend, Slot, Limits, /*bBackup*/ false, Main);
		if (!Main.bValid)
		{
			DocSavePrivate::ReadAndValidate(*Backend, Slot, Limits, /*bBackup*/ true, Backup);
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis, WeakWorld, Slot, Handle, Main = MoveTemp(Main), Backup = MoveTemp(Backup)]()
		{
			UDocSaveGameSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			UWorld* World = WeakWorld.Get();
			if (!World)
			{
				Self->Broadcast(false, Handle, FDocSystemResult::MakeFailure(EDocResultOutcome::Cancelled, TEXT("World changed during load")), 0);
				return;
			}
			int64 Revision = 0;
			const FDocSystemResult Result = Self->ApplyLoaded(World, Slot, Main, Backup, Revision);
			Self->Broadcast(false, Handle, Result, Revision);
		});
	});
	return Handle;
}

FDocSystemResult UDocSaveGameSubsystem::LoadFromSlotBlocking(UWorld* World, const FString& Slot, int64& OutRevision)
{
	OutRevision = 0;
	if (!Storage.IsValid() || !World)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No storage or world"));
	}
	if (!FDocLocalFileSaveBackend::IsValidSlotName(Slot))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid slot name"));
	}
	const FDocSaveEnvelope::FLimits Limits = DocSavePrivate::LimitsFromSettings();
	DocSavePrivate::FLoadedFile Main;
	DocSavePrivate::FLoadedFile Backup;
	DocSavePrivate::ReadAndValidate(*Storage, Slot, Limits, false, Main);
	if (!Main.bValid)
	{
		DocSavePrivate::ReadAndValidate(*Storage, Slot, Limits, true, Backup);
	}
	return ApplyLoaded(World, Slot, Main, Backup, OutRevision);
}

void DocSavePrivate::ReadAndValidate(IDocSaveStorageBackend& Backend, const FString& Slot, const FDocSaveEnvelope::FLimits& Limits, bool bBackup, FLoadedFile& Out)
{
	TArray<uint8> File;
	Out.bFound = bBackup ? Backend.ReadBackup(Slot, File, Limits.MaxFileBytes, Out.Error) : Backend.ReadSlot(Slot, File, Limits.MaxFileBytes, Out.Error);
	if (!Out.bFound)
	{
		return;
	}
	Out.bValid = FDocSaveEnvelope::Read(File, Limits, Out.Header, Out.Body, Out.Error);
	Out.bUnsupported = !Out.bValid && Out.Error.StartsWith(TEXT("Unsupported format"));
}

FDocSystemResult UDocSaveGameSubsystem::ApplyLoaded(UWorld* World, const FString& Slot, const DocSavePrivate::FLoadedFile& Main,
	const DocSavePrivate::FLoadedFile& Backup, int64& OutRevision)
{
	// Validation happens before any state changes: a bad file never clears progress.
	FDocSaveBody Body;
	FDocSaveHeader Header;
	FString Problem;
	bool bFromBackup = false;
	if (!DecodeAndValidateBody(Main, Body, Problem))
	{
		FString BackupProblem;
		if (!Backup.bValid || !DecodeAndValidateBody(Backup, Body, BackupProblem))
		{
			if (!Main.bFound)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, Main.Error);
			}
			const EDocResultOutcome Outcome = Main.bUnsupported ? EDocResultOutcome::Unsupported : EDocResultOutcome::Failed;
			return FDocSystemResult::MakeFailure(Outcome, Problem, DocCoreTags::Error_Storage);
		}
		UE_LOG(LogDocSave, Warning, TEXT("Slot %s: current save unusable (%s); loaded last-known-good backup"), *Slot, *Problem);
		Header = Backup.Header;
		bFromBackup = true;
	}
	else
	{
		Header = Main.Header;
	}

	// Stage: validate every record before replacing the store.
	TMap<FDocPersistentObjectId, FDocSaveObjectRecord> Staged;
	for (FDocSaveObjectRecord& Record : Body.Objects)
	{
		if (!Record.Id.IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Record with invalid id"));
		}
		if (Staged.Contains(Record.Id))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, FString::Printf(TEXT("Duplicate record id %s"), *Record.Id.ToString()));
		}
		Staged.Add(Record.Id, MoveTemp(Record));
	}

	Records = MoveTemp(Staged);
	FeatureRecords = MoveTemp(Body.FeatureRecords);
	FSlotState& State = Slots.FindOrAdd(Slot);
	State.LastCommittedRevision = FMath::Max(State.LastCommittedRevision, Header.SaveRevision);
	OutRevision = Header.SaveRevision;

	FString Warnings;
	FDocSystemResult Restore = RestoreWorld(World, Warnings);
	if (!Warnings.IsEmpty())
	{
		UE_LOG(LogDocSave, Warning, TEXT("Restore of %s rev %lld: %s"), *Slot, Header.SaveRevision, *Warnings);
	}
	if (bFromBackup && Restore.IsSuccess())
	{
		Restore.Diagnostic = FString::Printf(TEXT("Recovered from backup (current save unusable: %s)"), *Problem);
	}
	return Restore;
}

bool UDocSaveGameSubsystem::DecodeAndValidateBody(const DocSavePrivate::FLoadedFile& File, FDocSaveBody& OutBody, FString& OutProblem) const
{
	if (!File.bFound || !File.bValid)
	{
		OutProblem = File.Error;
		return false;
	}
	OutBody = FDocSaveBody();
	if (!DocCoreSerialization::Decode(OutBody, File.Body))
	{
		OutProblem = TEXT("Save body could not be decoded");
		return false;
	}
	return true;
}

FDocSystemResult UDocSaveGameSubsystem::RestoreWorld(UWorld* World, FString& OutWarnings)
{
	TGuardValue<bool> Barrier(bRestoring, true);
	const FGuid Namespace = NamespaceFor(World);
	TArray<FString> Errors;

	// 1) Registered participants: apply or suppress.
	TArray<TPair<FDocPersistentObjectId, TWeakObjectPtr<UDocSaveableComponent>>> LiveCopy;
	for (const TPair<FDocPersistentObjectId, TWeakObjectPtr<UDocSaveableComponent>>& Pair : Live) { LiveCopy.Add(Pair); }
	for (const TPair<FDocPersistentObjectId, TWeakObjectPtr<UDocSaveableComponent>>& Pair : LiveCopy)
	{
		UDocSaveableComponent* Saveable = Pair.Value.Get();
		if (!Saveable || Saveable->GetWorld() != World)
		{
			continue;
		}
		const FDocSaveObjectRecord* Record = Records.Find(Pair.Key);
		if (!Record)
		{
			continue; // object not in the save: leave as authored
		}
		if (Record->Lifecycle == EDocSaveLifecycle::IntentionallyDestroyed)
		{
			Live.Remove(Pair.Key);
			if (AActor* Owner = Saveable->GetOwner()) { Owner->Destroy(); }
			continue;
		}
		Saveable->ApplyRecord(*Record, Errors);
	}

	// 2) Runtime-spawned objects of this world that are not live: spawn with their saved identity.
	TArray<FDocSaveObjectRecord> ToSpawn;
	for (const TPair<FDocPersistentObjectId, FDocSaveObjectRecord>& Pair : Records)
	{
		const FDocSaveObjectRecord& Record = Pair.Value;
		if (Record.bRuntimeSpawned && Record.Id.WorldNamespace == Namespace && !Live.Contains(Pair.Key)
			&& Record.Lifecycle != EDocSaveLifecycle::IntentionallyDestroyed && Record.Lifecycle != EDocSaveLifecycle::Virtualized)
		{
			ToSpawn.Add(Record);
		}
	}
	for (const FDocSaveObjectRecord& Record : ToSpawn)
	{
		UClass* Class = Record.SpawnClass.ResolveClass();
		if (!Class)
		{
			// Approved classes are loaded before restore (see README); never load arbitrary classes from save data.
			Errors.Add(FString::Printf(TEXT("%s: class %s not loaded; skipped"), *Record.Id.ToString(), *Record.SpawnClass.ToString()));
			continue;
		}
		if (!IsSpawnClassAllowed(Class))
		{
			Errors.Add(FString::Printf(TEXT("%s: class %s not allowlisted; skipped"), *Record.Id.ToString(), *Class->GetPathName()));
			continue;
		}
		const FTransform Transform = Record.bHasTransform ? Record.Transform : FTransform::Identity;
		AActor* Actor = World->SpawnActorDeferred<AActor>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Actor)
		{
			Errors.Add(FString::Printf(TEXT("%s: spawn failed"), *Record.Id.ToString()));
			continue;
		}
		UDocSaveableComponent* Saveable = Actor->FindComponentByClass<UDocSaveableComponent>();
		if (!Saveable)
		{
			Actor->Destroy();
			Errors.Add(FString::Printf(TEXT("%s: class %s has no UDocSaveableComponent"), *Record.Id.ToString(), *Class->GetName()));
			continue;
		}
		Saveable->AssignPersistentId(Record.Id);
		Saveable->MarkRuntimeSpawned(true);
		Actor->FinishSpawning(Transform);
		if (!Actor->HasActorBegunPlay())
		{
			Actor->DispatchBeginPlay();
		}
		Saveable->ApplyRecord(Record, Errors);
	}

	// 3) Second phase: cross-references.
	for (const TPair<FDocPersistentObjectId, TWeakObjectPtr<UDocSaveableComponent>>& Pair : Live)
	{
		if (UDocSaveableComponent* Saveable = Pair.Value.Get())
		{
			if (Saveable->GetWorld() == World)
			{
				Saveable->ResolveReferences();
			}
		}
	}

	OutWarnings = FString::Join(Errors, TEXT("; "));
	return Errors.Num() == 0 ? FDocSystemResult::MakeSuccess()
		: FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, FString::Printf(TEXT("Restored with %d problem(s): %s"), Errors.Num(), *OutWarnings));
}

bool UDocSaveGameSubsystem::DoesSlotExist(const FString& Slot) const
{
	return Storage.IsValid() && Storage->SlotExists(Slot);
}

FDocSystemResult UDocSaveGameSubsystem::DeleteSlot(const FString& Slot)
{
	FString Error;
	if (!Storage.IsValid() || !Storage->DeleteSlot(Slot, Error))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, Error, DocCoreTags::Error_Storage);
	}
	Slots.Remove(Slot);
	return FDocSystemResult::MakeSuccess();
}

void UDocSaveGameSubsystem::Broadcast(bool bSave, const FDocRequestHandle& Handle, const FDocSystemResult& Result, int64 Revision)
{
	if (bSave)
	{
		OnSaveCompletedNative.Broadcast(Handle, Result, Revision);
		OnSaveCompleted.Broadcast(Handle, Result, Revision);
	}
	else
	{
		OnLoadCompletedNative.Broadcast(Handle, Result, Revision);
		OnLoadCompleted.Broadcast(Handle, Result, Revision);
	}
}

// ---------------------------------------------------------------------------
// Async Blueprint action
// ---------------------------------------------------------------------------

UDocSaveAsyncAction* UDocSaveAsyncAction::SaveGameToSlotAsync(UObject* WorldContextObject, const FString& InSlot, EDocSaveSlotCategory InCategory)
{
	UDocSaveAsyncAction* Action = NewObject<UDocSaveAsyncAction>();
	Action->WorldContext = WorldContextObject;
	Action->Slot = InSlot;
	Action->Category = InCategory;
	Action->bLoad = false;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

UDocSaveAsyncAction* UDocSaveAsyncAction::LoadGameFromSlotAsync(UObject* WorldContextObject, const FString& InSlot)
{
	UDocSaveAsyncAction* Action = NewObject<UDocSaveAsyncAction>();
	Action->WorldContext = WorldContextObject;
	Action->Slot = InSlot;
	Action->bLoad = true;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UDocSaveAsyncAction::Activate()
{
	UDocSaveGameSubsystem* Save = UDocSaveGameSubsystem::Get(WorldContext.Get());
	if (!Save)
	{
		OnFailure.Broadcast(FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No save subsystem")), 0);
		SetReadyToDestroy();
		return;
	}
	Subsystem = Save;
	FDocSaveOperationNative& Event = bLoad ? Save->OnLoadCompletedNative : Save->OnSaveCompletedNative;
	Binding = Event.AddUObject(this, &UDocSaveAsyncAction::HandleDone);
	Operation = bLoad ? Save->LoadFromSlot(WorldContext.Get(), Slot) : Save->SaveToSlot(WorldContext.Get(), Slot, Category);
}

void UDocSaveAsyncAction::HandleDone(FDocRequestHandle InOperation, const FDocSystemResult& Result, int64 Revision)
{
	if (InOperation != Operation && Operation.IsSet())
	{
		return;
	}
	if (UDocSaveGameSubsystem* Save = Subsystem.Get())
	{
		(bLoad ? Save->OnLoadCompletedNative : Save->OnSaveCompletedNative).Remove(Binding);
	}
	if (Result.IsSuccess()) { OnSuccess.Broadcast(Result, Revision); }
	else { OnFailure.Broadcast(Result, Revision); }
	SetReadyToDestroy();
}
