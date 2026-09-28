#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/ActorComponent.h"
#include "DocScheduleTypes.h"
#include "DocNPCScheduleSubsystem.generated.h"

class UDocNPCScheduleComponent;

/** Schedule clock (calendar-aware). DocTime is preferred through a bridge; a manual clock ships in the base. */
class DOCNPCSCHEDULESRUNTIME_API IDocScheduleClock
{
public:
	virtual ~IDocScheduleClock() = default;
	virtual FName GetClockId() const = 0;
	virtual FDocScheduleCalendar GetCalendar() const = 0;
	/** Absolute seconds since the calendar epoch (day 0, second 0). */
	virtual double GetAbsoluteSeconds() const = 0;
};

/** Standalone manual clock (tests, hosts without DocTime). */
class DOCNPCSCHEDULESRUNTIME_API FDocManualScheduleClock final : public IDocScheduleClock
{
public:
	virtual FName GetClockId() const override { return TEXT("Manual"); }
	virtual FDocScheduleCalendar GetCalendar() const override { return Calendar; }
	virtual double GetAbsoluteSeconds() const override { return Now; }
	void SetAbsoluteSeconds(double Seconds) { Now = Seconds; }
	void Advance(double Seconds) { Now += Seconds; }
	FDocScheduleCalendar Calendar;
	double Now = 0.0;
};

/** Condition data for entry queries (context tags per NPC). Missing provider = Unavailable. */
class DOCNPCSCHEDULESRUNTIME_API IDocScheduleConditionProvider
{
public:
	virtual ~IDocScheduleConditionProvider() = default;
	virtual FDocConditionResult Evaluate(FName NPCId, const FGameplayTagQuery& Query) const = 0;
};

/**
 * Logical NPC schedules for one world (expansion handoff Section 8). Records exist
 * without actors; a component binds a loaded representation (the executor). Records are
 * evaluated only at indexed boundaries (entry start/end, override expiry, retry, arrival
 * deadline), so unloaded NPCs cost nothing between boundaries. Mutations require authority.
 */
UCLASS()
class DOCNPCSCHEDULESRUNTIME_API UDocNPCScheduleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocNPCScheduleSubsystem* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule") FDocSystemResult RegisterNPC(FName NPCId, UDocNPCScheduleDefinition* Schedule);
	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule") FDocSystemResult UnregisterNPC(FName NPCId);
	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule") void RegisterScheduleDefinition(UDocNPCScheduleDefinition* Schedule);

	/** Bind/unbind a loaded representation. One logical record per NPC regardless of representation swaps. */
	FDocSystemResult BindRepresentation(FName NPCId, UDocNPCScheduleComponent* Component);
	void UnbindRepresentation(FName NPCId, UDocNPCScheduleComponent* Component);

	// ---- Overrides (owner-scoped) ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule", meta = (DefaultToSelf = "Owner"))
	FDocRequestHandle PushScheduleOverride(FName NPCId, UObject* Owner, EDocScheduleSource Source, int32 Priority, FGameplayTag ActivityTag,
		FDocScheduleTarget Target, float DurationSeconds, EDocOverrideResume Resume, EDocOverridePreemption Preemption, bool bDurable, FDocSystemResult& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule", meta = (DefaultToSelf = "Owner"))
	FDocSystemResult PopScheduleOverride(FDocRequestHandle Handle, UObject* Owner);

	/** Removes only Owner's overrides on the NPC. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule", meta = (DefaultToSelf = "Owner"))
	int32 ClearOverrides(FName NPCId, UObject* Owner);

	/** Privileged administrative clear of every override (separate from ClearOverrides). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule") int32 ClearAllOverridesAdmin(FName NPCId);

	// ---- Queries ----
	UFUNCTION(BlueprintPure, Category = "Doc|Schedule") FDocNPCScheduleState GetCurrentActivity(FName NPCId) const;
	UFUNCTION(BlueprintPure, Category = "Doc|Schedule") FDocExpectedLocation GetExpectedLocation(FName NPCId) const;
	UFUNCTION(BlueprintPure, Category = "Doc|Schedule") bool HasNPC(FName NPCId) const { return Records.Contains(NPCId); }

	/** Executor feedback. Stale request ids (late callbacks) are ignored. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule")
	FDocSystemResult ReportActivityStatus(FName NPCId, int64 RequestId, EDocActivityStatus Status, const FString& Reason);

	/** Representation provider validated a reactivation placement (or reports its observed location). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule")
	void ReportObservedLocation(FName NPCId, FVector Location, bool bPlacementValidated);

	// ---- Clock, providers ----
	void SetClock(TSharedPtr<IDocScheduleClock> InClock);
	IDocScheduleClock* GetClock() const { return Clock.Get(); }
	FDocManualScheduleClock* GetManualClock() const { return ManualClock.Get(); }
	void SetConditionProvider(TSharedPtr<IDocScheduleConditionProvider> Provider) { Conditions = Provider; }
	/** Force re-evaluation of every record (large time change). Bounded per tick. */
	void RequestFullReevaluation();

	// ---- Persistence ----
	FDocScheduleSaveData CaptureState() const;
	FDocSystemResult RestoreState(const FDocScheduleSaveData& Data);

	// ---- Diagnostics / tests ----
	int32 GetEvaluationCount() const { return EvaluationCount; }
	void AdvanceForTesting(float DeltaSeconds) { TickSchedules(); }
	/** Pure selection at an absolute time (no side effects). */
	FDocNPCScheduleState PreviewAt(FName NPCId, double AbsoluteSeconds) const;

	//~ USubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override { TickSchedules(); }
	virtual TStatId GetStatId() const override;

private:
	struct FOverride
	{
		FDocScheduleOverrideRecord Data;
		TWeakObjectPtr<UObject> Owner;
		bool bOwnerless = false;
		int32 Epoch = 0;
	};

	struct FDecision
	{
		FGameplayTag Activity;
		FName EntryId;
		EDocScheduleSource Source = EDocScheduleSource::Daily;
		int32 Priority = 0;
		FDocScheduleTarget Target;
		const FDocNPCScheduleEntry* Entry = nullptr;
		int64 OverrideId = 0;
		EDocOverridePreemption Preemption = EDocOverridePreemption::Preempt;
		float ArrivalTolerance = 150.f;
		bool bFallback = false;
		FString Reason;

		bool SameAs(const FDecision& O) const { return Activity == O.Activity && EntryId == O.EntryId && OverrideId == O.OverrideId && Target == O.Target; }
	};

	struct FRecord
	{
		FName NPCId;
		TObjectPtr<UDocNPCScheduleDefinition> Schedule;
		TArray<FOverride> Overrides;
		FDecision Desired;
		bool bHasDesired = false;
		FDocNPCActivityRequest Requested;
		EDocActivityStatus Status = EDocActivityStatus::None;
		FString StatusReason;
		double StatusSince = 0.0;
		int32 RetryCount = 0;
		double RetryAt = 0.0;
		bool bPendingDispatch = false;
		bool bRestoredDispatch = false;
		double LastEvaluated = 0.0;
		double NextEvaluation = 0.0;
		FDocExpectedLocation Expected;
		TWeakObjectPtr<UDocNPCScheduleComponent> Representation;
		int32 Epoch = 0;
	};

	struct FOverrideRef
	{
		FName NPCId;
		int64 OverrideId = 0;
	};

	bool HasAuthority() const;
	double Now() const;
	bool IsEntryActive(const FDocNPCScheduleEntry& Entry, double Absolute, const FDocScheduleCalendar& Calendar) const;
	bool DayMatches(const FDocNPCScheduleEntry& Entry, int64 Day, const FDocScheduleCalendar& Calendar) const;
	FDecision Select(const FRecord& Record, double Absolute) const;
	double NextBoundary(const FRecord& Record, double Absolute) const;
	void Evaluate(FRecord& Record, double Absolute);
	void Dispatch(FRecord& Record, double Absolute, FGameplayTag FallbackActivity);
	void EnqueueEvaluation(FRecord& Record, double When);
	void UpdateExpected(FRecord& Record);
	const UDocNPCActivityDefinition* ActivityFor(const FRecord& Record) const;
	void TickSchedules();

	TMap<FName, FRecord> Records;
	TMap<FName, TObjectPtr<UDocNPCScheduleDefinition>> Definitions;
	UPROPERTY() TArray<TObjectPtr<UDocNPCScheduleDefinition>> DefinitionRefs;
	TDocHandleTable<FOverrideRef> OverrideHandles;
	/** Min-heap of (time, npc) boundaries; stale entries are skipped. */
	TArray<TPair<double, FName>> Queue;
	TSharedPtr<IDocScheduleClock> Clock;
	TSharedPtr<FDocManualScheduleClock> ManualClock;
	TSharedPtr<IDocScheduleConditionProvider> Conditions;
	double LastTickTime = 0.0;
	int64 NextRequestId = 1;
	int64 NextOverrideId = 1;
	int64 NextPushOrder = 1;
	int32 EvaluationCount = 0;
	int32 GlobalEpoch = 0;
};

/** Binds a loaded NPC actor to its logical record and acts as the executor's API surface. */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCNPCSCHEDULESRUNTIME_API UDocNPCScheduleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocNPCScheduleComponent();

	/** Persistent logical NPC id (never an actor name that changes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") FName NPCId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Schedule") TObjectPtr<UDocNPCScheduleDefinition> Schedule;

	/** Executor hooks: AI/BT/StateTree/locomotion react here and report status back. */
	UPROPERTY(BlueprintAssignable, Category = "Doc|Schedule") FDocActivityRequestEvent OnActivityRequested;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Schedule") FDocActivityCancelEvent OnActivityCancelled;
	FDocActivityRequestNative OnActivityRequestedNative;
	FDocActivityCancelNative OnActivityCancelledNative;

	UFUNCTION(BlueprintCallable, Category = "Doc|Schedule")
	FDocSystemResult ReportStatus(int64 RequestId, EDocActivityStatus Status, const FString& Reason);

	UFUNCTION(BlueprintPure, Category = "Doc|Schedule")
	FDocNPCActivityRequest GetCurrentRequest() const { return CurrentRequest; }

	void DeliverRequest(const FDocNPCActivityRequest& Request);
	void DeliverCancel(int64 RequestId);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FDocNPCActivityRequest CurrentRequest;
};
