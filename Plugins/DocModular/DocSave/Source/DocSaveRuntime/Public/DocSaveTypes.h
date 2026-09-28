#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Interface.h"
#include "UObject/SoftObjectPath.h"
#include "GameFramework/Actor.h"
#include "DocPersistentObjectId.h"
#include "DocSharedTypes.h"
#include "DocSystemResult.h"
#include "DocRequestHandle.h"
#include "DocSaveTypes.generated.h"

UENUM(BlueprintType)
enum class EDocSaveSlotCategory : uint8
{
	Manual,
	Auto,
	Checkpoint,
	Quick,
	Profile,
	World
};

/** Lifecycle of a persistent object record (handoff 13.4). Unload is not destruction. */
UENUM(BlueprintType)
enum class EDocSaveLifecycle : uint8
{
	/** Live and registered. */
	Registered,
	/** Unloaded by streaming/travel; latest state retained. */
	StreamedOut,
	/** Virtualized by an activation/representation system; latest state retained. */
	Virtualized,
	/** Explicitly and authoritatively destroyed (tombstone). */
	IntentionallyDestroyed
};

/** One component's encoded state (handoff 13.3). */
USTRUCT(BlueprintType)
struct DOCSAVERUNTIME_API FDocSaveComponentRecord
{
	GENERATED_BODY()

	/** Stable key of the participant within its actor (never a display name that may change). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FName ComponentKey;
	/** Schema/type id: the payload struct path. Must be registered as allowed. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FString TypeId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int32 Version = 0;
	UPROPERTY() TArray<uint8> Payload;
};

/** Durable object record (handoff 13.3). */
USTRUCT(BlueprintType)
struct DOCSAVERUNTIME_API FDocSaveObjectRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FDocPersistentObjectId Id;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int32 RecordSchemaVersion = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") bool bRuntimeSpawned = false;
	/** Required for runtime-spawned objects; must be allowlisted to be spawned on load. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FSoftClassPath SpawnClass;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") bool bHasTransform = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FTransform Transform;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") bool bHasEnabledState = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") bool bEnabled = true;
	/** Chunk/level-instance key that owns the object, when known (informational). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FString OwningChunk;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") EDocSaveLifecycle Lifecycle = EDocSaveLifecycle::Registered;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") TArray<FDocSaveComponentRecord> Components;
};

/** Body of a save (encoded with DocCoreSerialization inside the envelope). */
USTRUCT()
struct DOCSAVERUNTIME_API FDocSaveBody
{
	GENERATED_BODY()

	UPROPERTY() TArray<FDocSaveObjectRecord> Objects;
	UPROPERTY() TArray<FDocFeatureRecord> FeatureRecords;
	UPROPERTY() TMap<FString, FString> Metadata;
};

/** Parsed envelope header (handoff 13.3). */
USTRUCT(BlueprintType)
struct DOCSAVERUNTIME_API FDocSaveHeader
{
	GENERATED_BODY()

	static constexpr uint32 MagicValue = 0x56534344; // "DCSV" little-endian
	static constexpr int32 CurrentFormatVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int32 FormatVersion = CurrentFormatVersion;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int32 SuiteVersion = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FString ProducerGameVersion;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FGuid WorldNamespace;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int64 SaveRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FDateTime Timestamp;
	/** 0 = none, 1 = zlib. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int32 CompressionMode = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int64 PayloadByteCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int64 UncompressedByteCount = 0;
	/** "CRC32" (detects corruption; not authentication). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") FString IntegrityAlgorithm = TEXT("CRC32");
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") int64 IntegrityValue = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Save") EDocSaveSlotCategory Category = EDocSaveSlotCategory::Manual;
};

/** A participant's payload for one component record. */
USTRUCT(BlueprintType)
struct DOCSAVERUNTIME_API FDocSavePayload
{
	GENERATED_BODY()

	/** Stable key within the actor. Required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Save") FName ComponentKey;
	/** Payload schema version (the participant's current version when capturing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Save") int32 Version = 1;
	/** Typed payload. Object references are not portable: store persistent ids or soft paths. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Save") FInstancedStruct Data;
};

/**
 * Save participant (actor or component). Capture must be side-effect free.
 * Restore runs under the restore barrier (UDocSaveGameSubsystem::IsRestoring()).
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UDocSaveParticipant : public UInterface
{
	GENERATED_BODY()
};

class DOCSAVERUNTIME_API IDocSaveParticipant
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Save")
	void CaptureDocSaveState(TArray<FDocSavePayload>& OutPayloads) const;

	/** Called once per restored payload whose ComponentKey this participant produced. Return false to report failure. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Save")
	bool RestoreDocSaveState(const FDocSavePayload& Payload);

	/** Second restore phase: resolve persistent-id references to other objects. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Save")
	void ResolveDocSaveReferences();
};

/** Project Settings → Plugins → Doc Save. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Save"))
class DOCSAVERUNTIME_API UDocSaveSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Written into every header for diagnostics and migrations. */
	UPROPERTY(Config, EditAnywhere, Category = "Save")
	FString GameVersion = TEXT("1.0");

	/** Runtime-spawned actors are recreated on load only if their class is (a subclass of) one of these. */
	UPROPERTY(Config, EditAnywhere, Category = "Security")
	TArray<TSoftClassPtr<AActor>> AllowedRuntimeSpawnClasses;

	/** Payload structs allowed in component records (script struct paths, e.g. /Script/MyGame.MyDoorState). */
	UPROPERTY(Config, EditAnywhere, Category = "Security")
	TArray<FString> AllowedPayloadStructs;

	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "1024"))
	int64 MaxFileBytes = 256LL * 1024 * 1024;

	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "1024"))
	int64 MaxUncompressedBytes = 512LL * 1024 * 1024;

	UPROPERTY(Config, EditAnywhere, Category = "Storage")
	bool bCompress = true;

	/** Sub-folder of the project's Saved directory. */
	UPROPERTY(Config, EditAnywhere, Category = "Storage")
	FString SaveFolder = TEXT("DocSave");
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDocSaveOperationEvent, FDocRequestHandle, Operation, const FDocSystemResult&, Result, int64, Revision);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDocSaveOperationNative, FDocRequestHandle /*Operation*/, const FDocSystemResult& /*Result*/, int64 /*Revision*/);
