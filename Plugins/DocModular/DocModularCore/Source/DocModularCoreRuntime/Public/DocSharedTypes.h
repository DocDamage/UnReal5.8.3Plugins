#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Misc/Guid.h"
#include "DocOwnerScope.h"
#include "DocSharedTypes.generated.h"

class UScriptStruct;

/**
 * Pure condition outcome (expansion handoff 2.6). Unavailable is distinct from
 * Unsatisfied: a missing provider or unloaded data must never read as "false" or
 * as "satisfied".
 */
UENUM(BlueprintType)
enum class EDocConditionState : uint8
{
	Unavailable,
	Unsatisfied,
	Satisfied
};

USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocConditionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Condition")
	EDocConditionState State = EDocConditionState::Unavailable;

	/** Why (Doc.Error.* or a feature tag). Empty when Satisfied. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Condition")
	FGameplayTag ReasonTag;

	/** Localized reason safe to show players (optional). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Condition")
	FText UserReason;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Condition")
	FString Diagnostic;

	/** Revision of the evaluated state, when the provider has one (0 = unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Condition")
	int64 EvaluatedRevision = 0;

	bool IsSatisfied() const { return State == EDocConditionState::Satisfied; }
	bool IsUnavailable() const { return State == EDocConditionState::Unavailable; }

	static FDocConditionResult Satisfied(int64 Revision = 0)
	{
		FDocConditionResult R; R.State = EDocConditionState::Satisfied; R.EvaluatedRevision = Revision; return R;
	}
	static FDocConditionResult Unsatisfied(FGameplayTag Reason, FString InDiagnostic = FString(), FText InUserReason = FText::GetEmpty())
	{
		FDocConditionResult R; R.State = EDocConditionState::Unsatisfied; R.ReasonTag = Reason; R.Diagnostic = MoveTemp(InDiagnostic); R.UserReason = MoveTemp(InUserReason); return R;
	}
	static FDocConditionResult Unavailable(FGameplayTag Reason, FString InDiagnostic = FString())
	{
		FDocConditionResult R; R.State = EDocConditionState::Unavailable; R.ReasonTag = Reason; R.Diagnostic = MoveTemp(InDiagnostic); return R;
	}

	/** AND-combination: Unavailable dominates, then Unsatisfied. Keeps the first non-satisfied reason. */
	static FDocConditionResult CombineAll(const TArray<FDocConditionResult>& Results);
	/** OR-combination: any Satisfied wins; else Unsatisfied if any Unsatisfied; else Unavailable. */
	static FDocConditionResult CombineAny(const TArray<FDocConditionResult>& Results);
};

/** Clock a timer or duration is measured in (expansion handoff 2.4). */
UENUM(BlueprintType)
enum class EDocClockDomain : uint8
{
	/** Game simulation time owned by a feature (e.g. DocTime); pauses with the simulation. */
	Simulation,
	/** UWorld time seconds; pauses with the world and respects dilation. */
	WorldGameplay,
	/** Monotonic real time (FPlatformTime); ignores pause. */
	RealTime,
	/** Opt-in OS wall clock; only for explicitly enabled offline progress. */
	WallClock
};

/**
 * Revision of a mutable record inside an epoch. Restoring a save or replacing a
 * world changes the epoch, so a stale request cannot match a reused number.
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocRecordRevision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Revision")
	FGuid Epoch;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Revision")
	int64 Revision = 0;

	bool IsSet() const { return Epoch.IsValid(); }

	/** Start a fresh epoch at revision 0. */
	void NewEpoch() { Epoch = FGuid::NewGuid(); Revision = 0; }

	/** Increment; starts an epoch if none. Returns the new revision. */
	int64 Bump() { if (!Epoch.IsValid()) { Epoch = FGuid::NewGuid(); } return ++Revision; }

	friend bool operator==(const FDocRecordRevision& A, const FDocRecordRevision& B) { return A.Epoch == B.Epoch && A.Revision == B.Revision; }
	friend bool operator!=(const FDocRecordRevision& A, const FDocRecordRevision& B) { return !(A == B); }
	friend FArchive& operator<<(FArchive& Ar, FDocRecordRevision& R) { Ar << R.Epoch; Ar << R.Revision; return Ar; }
};

/**
 * Feature-owned persistence record (expansion 2.8). A feature encodes its own
 * payload and schema version; storage systems (DocSave bridges or a host provider)
 * store the bytes without interpreting them.
 */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocFeatureRecord
{
	GENERATED_BODY()

	/** Stable feature identifier, e.g. "DocQuestObjectives". */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Persistence")
	FName FeatureId;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Persistence")
	int32 SchemaVersion = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Persistence")
	FDocOwnerScope Owner;

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Persistence")
	FDocRecordRevision Revision;

	UPROPERTY()
	TArray<uint8> Payload;
};

namespace DocCoreSerialization
{
	/**
	 * Encode a USTRUCT value with tagged property serialization (tolerant of added
	 * or removed properties). Object references are written as path names and are
	 * not loaded on decode unless the object already exists; snapshot structs should
	 * store persistent IDs or soft paths, not object pointers.
	 */
	DOCMODULARCORERUNTIME_API void EncodeStruct(const UScriptStruct* Struct, const void* Value, TArray<uint8>& OutBytes);

	/** Decode bytes produced by EncodeStruct. Returns false on empty/truncated input (Value is then reset to defaults). */
	DOCMODULARCORERUNTIME_API bool DecodeStruct(const UScriptStruct* Struct, void* Value, const TArray<uint8>& Bytes);

	template <typename T>
	void Encode(const T& Value, TArray<uint8>& OutBytes) { EncodeStruct(T::StaticStruct(), &Value, OutBytes); }

	template <typename T>
	bool Decode(T& Value, const TArray<uint8>& Bytes) { return DecodeStruct(T::StaticStruct(), &Value, Bytes); }
}
