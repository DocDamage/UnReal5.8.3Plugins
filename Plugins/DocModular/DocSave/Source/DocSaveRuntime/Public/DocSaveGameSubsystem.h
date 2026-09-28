#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "DocSaveTypes.h"
#include "DocSaveStorage.h"
#include "DocSaveGameSubsystem.generated.h"

class UDocSaveableComponent;

namespace DocSavePrivate
{
	/** Result of a worker-side bounded read + envelope validation. */
	struct FLoadedFile
	{
		bool bFound = false;
		bool bValid = false;
		bool bUnsupported = false;
		FDocSaveHeader Header;
		TArray<uint8> Body;
		FString Error;
	};

	DOCSAVERUNTIME_API void ReadAndValidate(IDocSaveStorageBackend& Backend, const FString& Slot, const FDocSaveEnvelope::FLimits& Limits, bool bBackup, FLoadedFile& Out);

	/** Assign revision max(MinRevision, committed+1), build the envelope and write it. Worker-safe. */
	DOCSAVERUNTIME_API EDocResultOutcome CommitRevision(IDocSaveStorageBackend& Backend, const FString& Slot, FDocSaveHeader Header,
		const TArray<uint8>& Body, bool bCompress, int64 MinRevision, int64 MaxFileBytes, int64& OutRevision, FString& OutError);
}

/**
 * Versioned persistence coordinator (handoff Section 13). One per game instance:
 * keeps plain records across map travel, never old actors.
 *
 * Record store: every object ever registered has a record (live, streamed out,
 * virtualized, or tombstoned). A save combines live captures with retained
 * records, so unloaded content is saved too.
 *
 * Save pipeline: validate → assign revision → capture/encode on the game thread →
 * immutable bytes → worker: compress, integrity, write via backend → game thread:
 * publish result. Writes are serialized per slot; requests arriving while a
 * write is in flight are coalesced into the next write, and every caller is told
 * the revision that actually committed.
 *
 * Load pipeline: worker: read, bounds, integrity, decompress → game thread:
 * decode, migrate, stage records → restore barrier: apply to registered
 * participants, destroy tombstoned actors, spawn allowlisted runtime objects with
 * their saved identity → resolve references → release barrier → Ready.
 */
UCLASS()
class DOCSAVERUNTIME_API UDocSaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDocSaveGameSubsystem* Get(const UObject* WorldContextObject);

	// ---- Participants ----
	/** Returns false when refused (invalid/duplicate id, tombstoned — the owner is then destroyed). */
	bool RegisterSaveable(UDocSaveableComponent* Saveable);
	/** Keeps the latest record with lifecycle StreamedOut (or tombstone when destroyed persistently). */
	void UnregisterSaveable(UDocSaveableComponent* Saveable, EDocSaveLifecycle Lifecycle);

	/** Authoritative permanent destruction: writes a tombstone. Streaming/activation never do this implicitly. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	FDocSystemResult MarkPersistentlyDestroyed(const FDocPersistentObjectId& Id);

	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	FDocSystemResult MarkVirtualized(const FDocPersistentObjectId& Id, bool bVirtualized);

	UFUNCTION(BlueprintPure, Category = "Doc|Save")
	bool IsTombstoned(const FDocPersistentObjectId& Id) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Save")
	bool IsRestoring() const { return bRestoring; }

	// ---- Feature records (bridges/hosts store feature-owned payloads without Save knowing their meaning) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	void SetFeatureRecord(const FDocFeatureRecord& Record);

	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	bool GetFeatureRecord(FName FeatureId, const FDocOwnerScope& Owner, FDocFeatureRecord& OutRecord) const;

	// ---- Operations ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Save", meta = (WorldContext = "WorldContextObject"))
	FDocRequestHandle SaveToSlot(const UObject* WorldContextObject, const FString& Slot, EDocSaveSlotCategory Category);

	UFUNCTION(BlueprintCallable, Category = "Doc|Save", meta = (WorldContext = "WorldContextObject"))
	FDocRequestHandle LoadFromSlot(const UObject* WorldContextObject, const FString& Slot);

	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	bool DoesSlotExist(const FString& Slot) const;

	/** Permanently deletes the slot and its backup. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	FDocSystemResult DeleteSlot(const FString& Slot);

	UPROPERTY(BlueprintAssignable, Category = "Doc|Save")
	FDocSaveOperationEvent OnSaveCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Save")
	FDocSaveOperationEvent OnLoadCompleted;

	FDocSaveOperationNative OnSaveCompletedNative;
	FDocSaveOperationNative OnLoadCompletedNative;

	// ---- Migration ----
	using FMigrationFunc = TFunction<bool(int32 /*FromVersion*/, TArray<uint8>& /*InOutPayload*/)>;
	/** Register an upgrade step TypeId@FromVersion → FromVersion+1. */
	void RegisterMigration(const FString& TypeId, int32 FromVersion, FMigrationFunc Func);
	/** Latest version the running game understands for TypeId (payloads newer than this are rejected). */
	void RegisterCurrentVersion(const FString& TypeId, int32 Version);

	// ---- Allowlists (in addition to settings) ----
	void AllowPayloadStruct(const UScriptStruct* Struct);
	void AllowRuntimeSpawnClass(TSubclassOf<AActor> Class);
	bool IsPayloadStructAllowed(const FString& TypeId) const;
	bool IsSpawnClassAllowed(const UClass* Class) const;

	// ---- Encoding helpers used by participants ----
	/** Encode a payload for storage; fails (InvalidInput) for unregistered/disallowed struct types. */
	FDocSystemResult EncodePayload(const FDocSavePayload& Payload, FDocSaveComponentRecord& OutRecord) const;
	/** Decode + migrate a stored component record. */
	FDocSystemResult DecodePayload(const FDocSaveComponentRecord& Record, FDocSavePayload& OutPayload) const;

	// ---- Diagnostics / tests ----
	const TMap<FDocPersistentObjectId, FDocSaveObjectRecord>& GetRecords() const { return Records; }
	void SetStorageBackendForTesting(TSharedPtr<IDocSaveStorageBackend> Backend);
	/** Tests: make Get() return this instance (pass nullptr to clear). Tests create the subsystem with NewObject. */
	static void SetSubsystemOverrideForTesting(UDocSaveGameSubsystem* Override);
	/** Tests: minimal initialization for an instance created outside a subsystem collection. */
	void InitializeForTesting(TSharedPtr<IDocSaveStorageBackend> Backend);
	/** Run the synchronous parts of the save pipeline and write through the backend on this thread (tests). */
	FDocSystemResult SaveToSlotBlocking(UWorld* World, const FString& Slot, EDocSaveSlotCategory Category, int64& OutRevision);
	FDocSystemResult LoadFromSlotBlocking(UWorld* World, const FString& Slot, int64& OutRevision);
	int64 GetLastCommittedRevision(const FString& Slot) const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FPendingSave
	{
		TArray<FDocRequestHandle> Callers;
		EDocSaveSlotCategory Category = EDocSaveSlotCategory::Manual;
		TWeakObjectPtr<UWorld> World;
	};

	struct FSlotState
	{
		bool bWriteInFlight = false;
		int64 LastCommittedRevision = 0;
		TOptional<FPendingSave> Queued;
	};

	void CaptureLiveRecords();
	bool BuildSaveBody(const FString& Slot, UWorld* World, EDocSaveSlotCategory Category, TArray<uint8>& OutBody, FDocSaveHeader& OutHeaderTemplate);
	void StartWrite(const FString& Slot, FPendingSave&& Pending);
	void FinishWrite(const FString& Slot, TArray<FDocRequestHandle> Callers, EDocResultOutcome Outcome, const FString& Error, int64 Revision);
	FDocSystemResult ApplyLoaded(UWorld* World, const FString& Slot, const DocSavePrivate::FLoadedFile& Main, const DocSavePrivate::FLoadedFile& Backup, int64& OutRevision);
	bool DecodeAndValidateBody(const DocSavePrivate::FLoadedFile& File, FDocSaveBody& OutBody, FString& OutProblem) const;
	FDocSystemResult RestoreWorld(UWorld* World, FString& OutWarnings);
	FGuid NamespaceFor(const UWorld* World) const;
	void Broadcast(bool bSave, const FDocRequestHandle& Handle, const FDocSystemResult& Result, int64 Revision);

	TMap<FDocPersistentObjectId, FDocSaveObjectRecord> Records;
	TMap<FDocPersistentObjectId, TWeakObjectPtr<UDocSaveableComponent>> Live;
	TArray<FDocFeatureRecord> FeatureRecords;
	TMap<FString, FSlotState> Slots;
	TMap<FString, TMap<int32, FMigrationFunc>> Migrations;
	TMap<FString, int32> CurrentVersions;
	TSet<FString> AllowedStructs;
	TArray<TWeakObjectPtr<UClass>> AllowedClasses;
	TSharedPtr<IDocSaveStorageBackend> Storage;
	bool bRestoring = false;
	bool bShuttingDown = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocSaveActionResult, const FDocSystemResult&, Result, int64, Revision);

/** Blueprint async Save/Load node: exactly one of OnSuccess / OnFailure. */
UCLASS()
class DOCSAVERUNTIME_API UDocSaveAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Doc|Save", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UDocSaveAsyncAction* SaveGameToSlotAsync(UObject* WorldContextObject, const FString& Slot, EDocSaveSlotCategory Category);

	UFUNCTION(BlueprintCallable, Category = "Doc|Save", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UDocSaveAsyncAction* LoadGameFromSlotAsync(UObject* WorldContextObject, const FString& Slot);

	UPROPERTY(BlueprintAssignable) FDocSaveActionResult OnSuccess;
	UPROPERTY(BlueprintAssignable) FDocSaveActionResult OnFailure;

	virtual void Activate() override;

private:
	void HandleDone(FDocRequestHandle Operation, const FDocSystemResult& Result, int64 Revision);

	TWeakObjectPtr<UObject> WorldContext;
	FString Slot;
	EDocSaveSlotCategory Category = EDocSaveSlotCategory::Manual;
	bool bLoad = false;
	FDocRequestHandle Operation;
	FDelegateHandle Binding;
	TWeakObjectPtr<UDocSaveGameSubsystem> Subsystem;
};
