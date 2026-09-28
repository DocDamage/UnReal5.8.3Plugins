#include "DocNPCScheduleSubsystem.h"
#include "DocNPCSchedulesLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocNPCScheduleSubsystem)

namespace DocScheduleTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Activity, "Activity", "DocNPCSchedules activities");
	UE_DEFINE_GAMEPLAY_TAG(Activity_Idle, "Activity.Idle");
	UE_DEFINE_GAMEPLAY_TAG(Activity_Sleep, "Activity.Sleep");
	UE_DEFINE_GAMEPLAY_TAG(Activity_Work, "Activity.Work");
	UE_DEFINE_GAMEPLAY_TAG(Activity_Eat, "Activity.Eat");
	UE_DEFINE_GAMEPLAY_TAG(Activity_Flee, "Activity.Flee");
}

namespace DocSchedulePrivate
{
	int64 PosMod(int64 A, int64 B) { const int64 R = A % B; return R < 0 ? R + B : R; }
}

// ---------------------------------------------------------------------------
// Definition validation
// ---------------------------------------------------------------------------

TArray<FString> UDocNPCScheduleDefinition::FindProblems() const
{
	TArray<FString> Problems;
	TSet<FName> Ids;
	for (const FDocNPCScheduleEntry& E : Entries)
	{
		bool bDuplicate = false;
		Ids.Add(E.EntryId, &bDuplicate);
		if (E.EntryId.IsNone() || bDuplicate) { Problems.Add(FString::Printf(TEXT("EntryId missing or duplicated (%s)"), *E.EntryId.ToString())); }
		if (!E.ActivityTag.IsValid()) { Problems.Add(FString::Printf(TEXT("%s: ActivityTag required"), *E.EntryId.ToString())); }
		if (!E.bAllDay && FMath::IsNearlyEqual(E.StartSeconds, E.EndSeconds)) { Problems.Add(FString::Printf(TEXT("%s: Start == End is empty (mark AllDay?)"), *E.EntryId.ToString())); }
		if (E.StartSeconds < 0.0 || E.EndSeconds < 0.0) { Problems.Add(FString::Printf(TEXT("%s: negative time"), *E.EntryId.ToString())); }
		if (!E.Conditions.IsEmpty() && !E.FallbackActivity.IsValid()) { Problems.Add(FString::Printf(TEXT("%s: conditions without FallbackActivity"), *E.EntryId.ToString())); }
	}
	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UDocNPCScheduleDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (const FString& Problem : FindProblems())
	{
		Context.AddError(FText::FromString(Problem));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocNPCScheduleSubsystem* UDocNPCScheduleSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocNPCScheduleSubsystem>() : nullptr;
}

bool UDocNPCScheduleSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocNPCScheduleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ManualClock = MakeShared<FDocManualScheduleClock>();
	Clock = ManualClock;
}

void UDocNPCScheduleSubsystem::Deinitialize()
{
	for (TPair<FName, FRecord>& Pair : Records)
	{
		if (UDocNPCScheduleComponent* Component = Pair.Value.Representation.Get())
		{
			Component->DeliverCancel(Pair.Value.Requested.RequestId); // executors release their own claims
		}
	}
	Records.Reset();
	Queue.Reset();
	OverrideHandles.Reset();
	Super::Deinitialize();
}

TStatId UDocNPCScheduleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocNPCScheduleSubsystem, STATGROUP_Tickables);
}

bool UDocNPCScheduleSubsystem::HasAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

double UDocNPCScheduleSubsystem::Now() const
{
	return Clock.IsValid() ? Clock->GetAbsoluteSeconds() : 0.0;
}

void UDocNPCScheduleSubsystem::SetClock(TSharedPtr<IDocScheduleClock> InClock)
{
	Clock = InClock.IsValid() ? InClock : StaticCastSharedPtr<IDocScheduleClock>(ManualClock);
	LastTickTime = Now();
	RequestFullReevaluation();
}

void UDocNPCScheduleSubsystem::RegisterScheduleDefinition(UDocNPCScheduleDefinition* Schedule)
{
	if (Schedule && !Schedule->ScheduleId.IsNone())
	{
		Definitions.Add(Schedule->ScheduleId, Schedule);
		DefinitionRefs.AddUnique(Schedule);
	}
}

FDocSystemResult UDocNPCScheduleSubsystem::RegisterNPC(FName NPCId, UDocNPCScheduleDefinition* Schedule)
{
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Schedules are decided by the authority"));
	}
	if (NPCId.IsNone() || !Schedule)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("NPCId and schedule required"));
	}
	if (Records.Contains(NPCId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("NPC already registered (one logical record per NPC)"));
	}
	RegisterScheduleDefinition(Schedule);
	FRecord& Record = Records.Add(NPCId);
	Record.NPCId = NPCId;
	Record.Schedule = Schedule;
	Record.Epoch = GlobalEpoch;
	Evaluate(Record, Now());
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocNPCScheduleSubsystem::UnregisterNPC(FName NPCId)
{
	FRecord Removed;
	if (!Records.RemoveAndCopyValue(NPCId, Removed))
	{
		return FDocSystemResult::MakeNoChange();
	}
	if (UDocNPCScheduleComponent* Component = Removed.Representation.Get())
	{
		Component->DeliverCancel(Removed.Requested.RequestId);
	}
	OverrideHandles.RemoveIf([NPCId](const FDocRequestHandle&, const FOverrideRef& R) { return R.NPCId == NPCId; });
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Selection (pure)
// ---------------------------------------------------------------------------

bool UDocNPCScheduleSubsystem::DayMatches(const FDocNPCScheduleEntry& Entry, int64 Day, const FDocScheduleCalendar& Calendar) const
{
	if (Entry.SpecialDay >= 0 && Day != Entry.SpecialDay)
	{
		return false;
	}
	if (Entry.DaysOfWeek.Num() > 0 && !Entry.DaysOfWeek.Contains((int32)DocSchedulePrivate::PosMod(Day, FMath::Max(1, Calendar.DaysPerWeek))))
	{
		return false;
	}
	if (Entry.Seasons.Num() > 0)
	{
		const int64 SeasonDay = Day >= 0 ? Day / FMath::Max(1, Calendar.DaysPerSeason) : -((-Day + Calendar.DaysPerSeason - 1) / FMath::Max(1, Calendar.DaysPerSeason));
		if (!Entry.Seasons.Contains((int32)DocSchedulePrivate::PosMod(SeasonDay, FMath::Max(1, Calendar.SeasonsPerYear))))
		{
			return false;
		}
	}
	return true;
}

bool UDocNPCScheduleSubsystem::IsEntryActive(const FDocNPCScheduleEntry& Entry, double Absolute, const FDocScheduleCalendar& Calendar) const
{
	const double L = FMath::Max(1.0, Calendar.DayLengthSeconds);
	const int64 Day = (int64)FMath::FloorToDouble(Absolute / L);
	const double S = Absolute - Day * L;
	if (Entry.StartSeconds < 0.0 || Entry.EndSeconds < 0.0 || Entry.StartSeconds >= L + 1.e-6 || Entry.EndSeconds > L + 1.e-6)
	{
		return false;
	}
	if (Entry.bAllDay)
	{
		return DayMatches(Entry, Day, Calendar);
	}
	if (FMath::IsNearlyEqual(Entry.StartSeconds, Entry.EndSeconds))
	{
		return false; // empty
	}
	if (Entry.StartSeconds < Entry.EndSeconds)
	{
		return S >= Entry.StartSeconds && S < Entry.EndSeconds && DayMatches(Entry, Day, Calendar);
	}
	// Crosses midnight: the span belongs to the day it starts on (prior-day spans apply after midnight).
	return (S >= Entry.StartSeconds && DayMatches(Entry, Day, Calendar)) || (S < Entry.EndSeconds && DayMatches(Entry, Day - 1, Calendar));
}

UDocNPCScheduleSubsystem::FDecision UDocNPCScheduleSubsystem::Select(const FRecord& Record, double Absolute) const
{
	struct FCandidate
	{
		int32 Class = 0;
		int32 Priority = 0;
		bool bOverride = false;
		int64 Order = 0;
		FName Id;
		const FOverride* Override = nullptr;
		const FDocNPCScheduleEntry* Entry = nullptr;
	};
	TArray<FCandidate> Candidates;
	for (const FOverride& O : Record.Overrides)
	{
		const bool bExpired = O.Data.Expiry > 0.0 && Absolute >= O.Data.Expiry;
		const bool bOwnerGone = !O.bOwnerless && !O.Owner.IsValid();
		if (!bExpired && !bOwnerGone)
		{
			Candidates.Add({ (int32)O.Data.Source, O.Data.Priority, true, -O.Data.PushOrder /* newer first */, NAME_None, &O, nullptr });
		}
	}
	const FDocScheduleCalendar Calendar = Clock.IsValid() ? Clock->GetCalendar() : FDocScheduleCalendar();
	if (Record.Schedule)
	{
		for (int32 i = 0; i < Record.Schedule->Entries.Num(); ++i)
		{
			const FDocNPCScheduleEntry& E = Record.Schedule->Entries[i];
			if (E.ActivityTag.IsValid() && IsEntryActive(E, Absolute, Calendar))
			{
				Candidates.Add({ (int32)E.Source, E.Priority, false, i, E.EntryId, nullptr, &E });
			}
		}
	}
	// Override class → explicit priority → authored/push order → EntryId (never hash or spawn order).
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.Class != B.Class) { return A.Class > B.Class; }
		if (A.Priority != B.Priority) { return A.Priority > B.Priority; }
		if (A.bOverride != B.bOverride) { return A.bOverride; }
		if (A.Order != B.Order) { return A.Order < B.Order; }
		return A.Id.LexicalLess(B.Id);
	});

	FDecision D;
	for (const FCandidate& C : Candidates)
	{
		if (C.Override)
		{
			D.Activity = C.Override->Data.ActivityTag;
			D.Source = C.Override->Data.Source;
			D.Priority = C.Override->Data.Priority;
			D.Target = C.Override->Data.Target;
			D.OverrideId = C.Override->Data.OverrideId;
			D.Preemption = C.Override->Data.Preemption;
			return D;
		}
		const FDocNPCScheduleEntry& E = *C.Entry;
		if (!E.Conditions.IsEmpty())
		{
			const FDocConditionResult Condition = Conditions.IsValid() ? Conditions->Evaluate(Record.NPCId, E.Conditions)
				: FDocConditionResult::Unavailable(FGameplayTag(), TEXT("No condition provider"));
			if (Condition.State == EDocConditionState::Unsatisfied)
			{
				continue;
			}
			if (Condition.State == EDocConditionState::Unavailable)
			{
				if (!E.FallbackActivity.IsValid())
				{
					continue; // never select a potentially unsafe activity on missing data
				}
				D.Activity = E.FallbackActivity;
				D.EntryId = E.EntryId;
				D.Entry = &E;
				D.Source = E.Source;
				D.Priority = E.Priority;
				D.bFallback = true;
				D.Reason = Condition.Diagnostic.IsEmpty() ? FString(TEXT("Condition data unavailable")) : Condition.Diagnostic;
				return D;
			}
		}
		D.Activity = E.ActivityTag;
		D.EntryId = E.EntryId;
		D.Entry = &E;
		D.Source = E.Source;
		D.Priority = E.Priority;
		D.Target = E.Target;
		D.ArrivalTolerance = E.ArrivalTolerance;
		return D;
	}
	D.Activity = Record.Schedule && Record.Schedule->DefaultActivity.IsValid() ? Record.Schedule->DefaultActivity : DocScheduleTags::Activity_Idle;
	D.Reason = TEXT("No entry active: default activity");
	return D;
}

double UDocNPCScheduleSubsystem::NextBoundary(const FRecord& Record, double Absolute) const
{
	const FDocScheduleCalendar Calendar = Clock.IsValid() ? Clock->GetCalendar() : FDocScheduleCalendar();
	const double L = FMath::Max(1.0, Calendar.DayLengthSeconds);
	const int64 Day = (int64)FMath::FloorToDouble(Absolute / L);
	double Best = Absolute + L; // at least daily
	auto Consider = [&Best, Absolute](double T) { if (T > Absolute + 1.e-6 && T < Best) { Best = T; } };
	if (Record.Schedule)
	{
		const int32 Horizon = FMath::Max(1, Calendar.DaysPerWeek) + 1;
		for (const FDocNPCScheduleEntry& E : Record.Schedule->Entries)
		{
			for (int64 D = Day - 1; D <= Day + Horizon; ++D)
			{
				const double Base = D * L;
				if (E.bAllDay) { Consider(Base); Consider(Base + L); continue; }
				Consider(Base + E.StartSeconds);
				Consider(Base + E.EndSeconds + (E.StartSeconds > E.EndSeconds ? L : 0.0));
			}
		}
	}
	for (const FOverride& O : Record.Overrides)
	{
		if (O.Data.Expiry > 0.0) { Consider(O.Data.Expiry); }
	}
	if (Record.RetryAt > 0.0) { Consider(Record.RetryAt); }
	if (Record.Status == EDocActivityStatus::Travelling)
	{
		if (const UDocNPCActivityDefinition* Def = ActivityFor(Record); Def && Def->ArrivalDeadlineSeconds > 0.f)
		{
			Consider(Record.StatusSince + Def->ArrivalDeadlineSeconds);
		}
	}
	return Best;
}

const UDocNPCActivityDefinition* UDocNPCScheduleSubsystem::ActivityFor(const FRecord& Record) const
{
	return Record.Desired.Entry ? Record.Desired.Entry->Activity.Get() : nullptr;
}

// ---------------------------------------------------------------------------
// Evaluation and dispatch
// ---------------------------------------------------------------------------

void UDocNPCScheduleSubsystem::EnqueueEvaluation(FRecord& Record, double When)
{
	Record.NextEvaluation = When;
	Queue.HeapPush(TPair<double, FName>(When, Record.NPCId), [](const TPair<double, FName>& A, const TPair<double, FName>& B) { return A.Key < B.Key; });
}

void UDocNPCScheduleSubsystem::UpdateExpected(FRecord& Record)
{
	if (Record.Expected.Confidence == EDocLocationConfidence::Observed && Record.Representation.IsValid())
	{
		return; // a bound, observed actor is the better source
	}
	Record.Expected.bPlacementValidated = false;
	if (Record.Desired.Target.bHasLocation)
	{
		Record.Expected.Confidence = EDocLocationConfidence::ScheduledTarget;
		Record.Expected.Location = Record.Desired.Target.Location;
		Record.Expected.RegionId = Record.Desired.Target.RegionId;
	}
	else if (!Record.Desired.Target.RegionId.IsNone())
	{
		Record.Expected.Confidence = EDocLocationConfidence::RegionOnly;
		Record.Expected.RegionId = Record.Desired.Target.RegionId;
	}
	else
	{
		Record.Expected.Confidence = EDocLocationConfidence::Unknown; // no invented position
		Record.Expected.RegionId = NAME_None;
	}
}

void UDocNPCScheduleSubsystem::Evaluate(FRecord& Record, double Absolute)
{
	++EvaluationCount;
	Record.LastEvaluated = Absolute;

	// Drop expired/ownerless-transient overrides and their handles.
	Record.Overrides.RemoveAll([Absolute, &Record, this](const FOverride& O)
	{
		const bool bDrop = (O.Data.Expiry > 0.0 && Absolute >= O.Data.Expiry) || (!O.bOwnerless && !O.Owner.IsValid());
		if (bDrop)
		{
			const int64 Id = O.Data.OverrideId;
			const FName NPCId = Record.NPCId;
			OverrideHandles.RemoveIf([Id, NPCId](const FDocRequestHandle&, const FOverrideRef& R) { return R.NPCId == NPCId && R.OverrideId == Id; });
		}
		return bDrop;
	});

	const FDecision Decision = Select(Record, Absolute);
	const bool bChanged = !Record.bHasDesired || !Decision.SameAs(Record.Desired);
	if (bChanged)
	{
		Record.Desired = Decision;
		Record.bHasDesired = true;
		Record.RetryCount = 0;
		Record.RetryAt = 0.0;
		UpdateExpected(Record);
	}

	bool bRedispatch = bChanged || Record.bPendingDispatch;
	bool bUseFallback = false;
	if (!bChanged && Record.RetryAt > 0.0 && Absolute >= Record.RetryAt)
	{
		Record.RetryAt = 0.0;
		bRedispatch = true;
	}
	if (!bChanged && Record.Status == EDocActivityStatus::Travelling)
	{
		const UDocNPCActivityDefinition* Def = ActivityFor(Record);
		if (Def && Def->ArrivalDeadlineSeconds > 0.f && Absolute >= Record.StatusSince + Def->ArrivalDeadlineSeconds)
		{
			// A missed deadline triggers reselection/fallback, never a false arrival report.
			Record.StatusReason = TEXT("Arrival deadline missed");
			bRedispatch = true;
			bUseFallback = Def->FallbackActivity.IsValid();
		}
	}
	if (bRedispatch && HasAuthority())
	{
		Dispatch(Record, Absolute, bUseFallback ? ActivityFor(Record)->FallbackActivity : FGameplayTag());
	}
	EnqueueEvaluation(Record, NextBoundary(Record, Absolute));
}

void UDocNPCScheduleSubsystem::Dispatch(FRecord& Record, double Absolute, FGameplayTag FallbackActivity)
{
	UDocNPCScheduleComponent* Component = Record.Representation.Get();
	if (!Component)
	{
		Record.bPendingDispatch = false; // offline: desired state only, no executor, no per-second work
		return;
	}
	const bool bRunning = Record.Status == EDocActivityStatus::Requested || Record.Status == EDocActivityStatus::Accepted
		|| Record.Status == EDocActivityStatus::Travelling || Record.Status == EDocActivityStatus::Started;
	if (bRunning && !Record.Requested.bInterruptible && !FallbackActivity.IsValid())
	{
		const bool bPreempt = Record.Desired.OverrideId != 0 && Record.Desired.Preemption == EDocOverridePreemption::Preempt;
		if (!bPreempt)
		{
			Record.bPendingDispatch = true; // dispatch when the non-interruptible activity ends
			return;
		}
	}
	if (bRunning)
	{
		Component->DeliverCancel(Record.Requested.RequestId); // executor releases only its own claims
	}
	const UDocNPCActivityDefinition* Def = ActivityFor(Record);
	FDocNPCActivityRequest Request;
	Request.NPCId = Record.NPCId;
	Request.RequestId = NextRequestId++;
	Request.Revision = Record.Requested.Revision + 1;
	Request.ActivityTag = FallbackActivity.IsValid() ? FallbackActivity : Record.Desired.Activity;
	Request.Target = FallbackActivity.IsValid() ? FDocScheduleTarget() : Record.Desired.Target;
	Request.ArrivalTolerance = Record.Desired.ArrivalTolerance;
	Request.Priority = Record.Desired.Priority;
	Request.StartTime = Absolute;
	Request.ExpiryTime = NextBoundary(Record, Absolute);
	Request.bInterruptible = FallbackActivity.IsValid() || !Def || Def->bInterruptible;
	Request.bIsFallback = FallbackActivity.IsValid() || Record.Desired.bFallback;
	Request.bRestored = Record.bRestoredDispatch;
	Record.bRestoredDispatch = false;
	Record.Requested = Request;
	Record.Status = EDocActivityStatus::Requested;
	Record.StatusSince = Absolute;
	Record.StatusReason.Reset();
	Record.bPendingDispatch = false;
	Component->DeliverRequest(Request);
}

FDocSystemResult UDocNPCScheduleSubsystem::ReportActivityStatus(FName NPCId, int64 RequestId, EDocActivityStatus Status, const FString& Reason)
{
	FRecord* Record = Records.Find(NPCId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown NPC"));
	}
	if (RequestId != Record->Requested.RequestId || RequestId == 0)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Stale request id: late callback ignored"));
	}
	const double T = Now();
	Record->Status = Status;
	Record->StatusSince = T;
	Record->StatusReason = Reason;
	switch (Status)
	{
	case EDocActivityStatus::Travelling:
		EnqueueEvaluation(*Record, NextBoundary(*Record, T)); // includes the arrival deadline
		break;
	case EDocActivityStatus::Completed:
	case EDocActivityStatus::Cancelled:
		if (Record->bPendingDispatch) { Dispatch(*Record, T, FGameplayTag()); }
		break;
	case EDocActivityStatus::Failed:
	case EDocActivityStatus::Unavailable:
	{
		if (Record->Requested.bIsFallback)
		{
			break; // fallback failed too: wait for the next boundary (no busy loop)
		}
		const UDocNPCActivityDefinition* Def = ActivityFor(*Record);
		const UDocNPCSchedulesSettings* Settings = GetDefault<UDocNPCSchedulesSettings>();
		const int32 MaxRetries = Def ? Def->MaxRetries : Settings->DefaultMaxRetries;
		const float Backoff = Def ? Def->RetryBackoffSeconds : Settings->DefaultRetryBackoffSeconds;
		if (Record->RetryCount < MaxRetries)
		{
			++Record->RetryCount;
			Record->RetryAt = T + Backoff * FMath::Pow(2.f, (float)(Record->RetryCount - 1)); // bounded exponential backoff
			EnqueueEvaluation(*Record, FMath::Min(Record->RetryAt, NextBoundary(*Record, T)));
		}
		else
		{
			FGameplayTag Fallback = Def && Def->FallbackActivity.IsValid() ? Def->FallbackActivity
				: (Record->Desired.Entry && Record->Desired.Entry->FallbackActivity.IsValid() ? Record->Desired.Entry->FallbackActivity
					: (Record->Schedule ? Record->Schedule->DefaultActivity : FGameplayTag()));
			if (Fallback.IsValid() && Fallback != Record->Requested.ActivityTag)
			{
				Dispatch(*Record, T, Fallback);
			}
		}
		break;
	}
	default:
		break;
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocNPCScheduleSubsystem::ReportObservedLocation(FName NPCId, FVector Location, bool bPlacementValidated)
{
	if (FRecord* Record = Records.Find(NPCId))
	{
		Record->Expected.Confidence = EDocLocationConfidence::Observed;
		Record->Expected.Location = Location;
		Record->Expected.bPlacementValidated = bPlacementValidated;
	}
}

// ---------------------------------------------------------------------------
// Representation
// ---------------------------------------------------------------------------

FDocSystemResult UDocNPCScheduleSubsystem::BindRepresentation(FName NPCId, UDocNPCScheduleComponent* Component)
{
	FRecord* Record = Records.Find(NPCId);
	if (!Record || !Component)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Register the NPC before binding"));
	}
	if (UDocNPCScheduleComponent* Old = Record->Representation.Get(); Old && Old != Component)
	{
		Old->DeliverCancel(Record->Requested.RequestId); // representation swap: one record, old executor released
	}
	Record->Representation = Component;
	Record->Status = EDocActivityStatus::None;
	Record->Requested = FDocNPCActivityRequest();
	Record->bPendingDispatch = true;
	Evaluate(*Record, Now()); // fresh request for the new representation
	return FDocSystemResult::MakeSuccess();
}

void UDocNPCScheduleSubsystem::UnbindRepresentation(FName NPCId, UDocNPCScheduleComponent* Component)
{
	FRecord* Record = Records.Find(NPCId);
	if (!Record || Record->Representation.Get() != Component)
	{
		return;
	}
	if (Component)
	{
		Component->DeliverCancel(Record->Requested.RequestId); // live claims released on unload
	}
	Record->Representation.Reset();
	Record->Status = EDocActivityStatus::None;
	Record->Requested = FDocNPCActivityRequest();
	if (Record->Expected.Confidence == EDocLocationConfidence::Observed)
	{
		Record->Expected.bPlacementValidated = false;
	}
}

// ---------------------------------------------------------------------------
// Overrides
// ---------------------------------------------------------------------------

FDocRequestHandle UDocNPCScheduleSubsystem::PushScheduleOverride(FName NPCId, UObject* Owner, EDocScheduleSource Source, int32 Priority, FGameplayTag ActivityTag,
	FDocScheduleTarget Target, float DurationSeconds, EDocOverrideResume Resume, EDocOverridePreemption Preemption, bool bDurable, FDocSystemResult& OutResult)
{
	FRecord* Record = Records.Find(NPCId);
	if (!HasAuthority())
	{
		OutResult = FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Overrides are pushed on the authority"));
		return FDocRequestHandle();
	}
	if (!Record || !Owner || !ActivityTag.IsValid() || DurationSeconds < 0.f)
	{
		OutResult = FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Registered NPC, owner, activity and non-negative duration required"));
		return FDocRequestHandle();
	}
	const bool bRunning = Record->Status == EDocActivityStatus::Accepted || Record->Status == EDocActivityStatus::Travelling || Record->Status == EDocActivityStatus::Started;
	if (Preemption == EDocOverridePreemption::RejectIfBusy && bRunning && !Record->Requested.bInterruptible)
	{
		OutResult = FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("A non-interruptible activity is running"));
		return FDocRequestHandle();
	}
	const double T = Now();
	FOverride O;
	O.Data.OverrideId = NextOverrideId++;
	O.Data.Source = Source;
	O.Data.Priority = Priority;
	O.Data.ActivityTag = ActivityTag;
	O.Data.Target = Target;
	O.Data.Expiry = DurationSeconds > 0.f ? T + DurationSeconds : 0.0;
	O.Data.Resume = Resume;
	O.Data.Preemption = Preemption;
	O.Data.bDurable = bDurable;
	O.Data.PushOrder = NextPushOrder++;
	O.Owner = Owner;
	O.Epoch = Record->Epoch;
	Record->Overrides.Add(O);
	const FDocRequestHandle Handle = OverrideHandles.Add(Owner, FOverrideRef{ NPCId, O.Data.OverrideId });
	Evaluate(*Record, T);
	OutResult = FDocSystemResult::MakeSuccess();
	return Handle;
}

FDocSystemResult UDocNPCScheduleSubsystem::PopScheduleOverride(FDocRequestHandle Handle, UObject* Owner)
{
	switch (OverrideHandles.Validate(Handle, Owner))
	{
	case EDocHandleStatus::Active: break;
	case EDocHandleStatus::WrongScope: return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Override belongs to another owner"));
	default: return FDocSystemResult::MakeNoChange(TEXT("Override already ended (expired, popped or restored epoch)"));
	}
	FOverrideRef Ref;
	OverrideHandles.Remove(Handle, Owner, &Ref);
	FRecord* Record = Records.Find(Ref.NPCId);
	if (!Record)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Record->Overrides.RemoveAll([&Ref](const FOverride& O) { return O.Data.OverrideId == Ref.OverrideId; });
	// Recompute at the current time (never resume an outdated snapshot by default; ResumeInterrupted
	// resolves to the same result only if that entry is still active now).
	Evaluate(*Record, Now());
	return FDocSystemResult::MakeSuccess();
}

int32 UDocNPCScheduleSubsystem::ClearOverrides(FName NPCId, UObject* Owner)
{
	FRecord* Record = Records.Find(NPCId);
	if (!Record || !Owner)
	{
		return 0;
	}
	TSet<int64> Ids;
	for (const FOverride& O : Record->Overrides) { if (O.Owner.Get() == Owner) { Ids.Add(O.Data.OverrideId); } }
	Record->Overrides.RemoveAll([&Ids](const FOverride& O) { return Ids.Contains(O.Data.OverrideId); });
	OverrideHandles.RemoveIf([&Ids, NPCId](const FDocRequestHandle&, const FOverrideRef& R) { return R.NPCId == NPCId && Ids.Contains(R.OverrideId); });
	if (Ids.Num() > 0) { Evaluate(*Record, Now()); }
	return Ids.Num();
}

int32 UDocNPCScheduleSubsystem::ClearAllOverridesAdmin(FName NPCId)
{
	FRecord* Record = Records.Find(NPCId);
	if (!Record)
	{
		return 0;
	}
	const int32 Count = Record->Overrides.Num();
	Record->Overrides.Reset();
	OverrideHandles.RemoveIf([NPCId](const FDocRequestHandle&, const FOverrideRef& R) { return R.NPCId == NPCId; });
	Evaluate(*Record, Now());
	return Count;
}

// ---------------------------------------------------------------------------
// Queries, tick
// ---------------------------------------------------------------------------

FDocNPCScheduleState UDocNPCScheduleSubsystem::GetCurrentActivity(FName NPCId) const
{
	FDocNPCScheduleState State;
	const FRecord* Record = Records.Find(NPCId);
	if (!Record)
	{
		return State;
	}
	State.NPCId = NPCId;
	State.DesiredActivity = Record->Desired.Activity;
	State.DesiredEntryId = Record->Desired.EntryId;
	State.DesiredSource = Record->Desired.Source;
	State.bDesiredFromOverride = Record->Desired.OverrideId != 0;
	State.DesiredTarget = Record->Desired.Target;
	State.RequestedActivity = Record->Requested.ActivityTag;
	State.RequestId = Record->Requested.RequestId;
	State.Status = Record->Status;
	State.StatusReason = Record->StatusReason;
	State.RetryCount = Record->RetryCount;
	State.OverrideCount = Record->Overrides.Num();
	State.LastEvaluated = Record->LastEvaluated;
	State.NextEvaluation = Record->NextEvaluation;
	State.bRepresentationBound = Record->Representation.IsValid();
	State.Epoch = Record->Epoch;
	return State;
}

FDocExpectedLocation UDocNPCScheduleSubsystem::GetExpectedLocation(FName NPCId) const
{
	const FRecord* Record = Records.Find(NPCId);
	return Record ? Record->Expected : FDocExpectedLocation();
}

FDocNPCScheduleState UDocNPCScheduleSubsystem::PreviewAt(FName NPCId, double AbsoluteSeconds) const
{
	FDocNPCScheduleState State;
	if (const FRecord* Record = Records.Find(NPCId))
	{
		const FDecision D = Select(*Record, AbsoluteSeconds);
		State.NPCId = NPCId;
		State.DesiredActivity = D.Activity;
		State.DesiredEntryId = D.EntryId;
		State.DesiredSource = D.Source;
		State.bDesiredFromOverride = D.OverrideId != 0;
		State.DesiredTarget = D.Target;
	}
	return State;
}

void UDocNPCScheduleSubsystem::RequestFullReevaluation()
{
	const double T = Now();
	for (TPair<FName, FRecord>& Pair : Records)
	{
		EnqueueEvaluation(Pair.Value, T);
	}
}

void UDocNPCScheduleSubsystem::TickSchedules()
{
	const double T = Now();
	if (T + 1.e-6 < LastTickTime)
	{
		// Backward jump: recompute desired state; irreversible effects are never undone or re-granted here.
		++GlobalEpoch;
		RequestFullReevaluation();
	}
	LastTickTime = T;
	const int32 Budget = GetDefault<UDocNPCSchedulesSettings>()->MaxEvaluationsPerTick;
	int32 Done = 0;
	auto Pred = [](const TPair<double, FName>& A, const TPair<double, FName>& B) { return A.Key < B.Key; };
	while (Queue.Num() > 0 && Queue.HeapTop().Key <= T && Done < Budget)
	{
		TPair<double, FName> Top;
		Queue.HeapPop(Top, Pred);
		FRecord* Record = Records.Find(Top.Value);
		if (!Record || !FMath::IsNearlyEqual(Record->NextEvaluation, Top.Key))
		{
			continue; // stale entry
		}
		Evaluate(*Record, T); // one coherent change for any forward jump size
		++Done;
	}
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

FDocScheduleSaveData UDocNPCScheduleSubsystem::CaptureState() const
{
	FDocScheduleSaveData Data;
	Data.ClockId = Clock.IsValid() ? Clock->GetClockId() : NAME_None;
	Data.ClockTime = Now();
	TArray<FName> Ids;
	Records.GetKeys(Ids);
	Ids.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	for (const FName& Id : Ids)
	{
		const FRecord& R = Records[Id];
		FDocNPCSaveRecord S;
		S.NPCId = Id;
		S.ScheduleId = R.Schedule ? R.Schedule->ScheduleId : NAME_None;
		S.ScheduleVersion = R.Schedule ? R.Schedule->Version : 0;
		S.DesiredActivity = R.Desired.Activity;
		S.DesiredEntryId = R.Desired.EntryId;
		S.Expected = R.Expected;
		S.LastEvaluated = R.LastEvaluated;
		for (const FOverride& O : R.Overrides)
		{
			if (O.Data.bDurable) { S.DurableOverrides.Add(O.Data); } // never request ids, claims or tasks
		}
		Data.Records.Add(S);
	}
	return Data;
}

FDocSystemResult UDocNPCScheduleSubsystem::RestoreState(const FDocScheduleSaveData& Data)
{
	if (Data.Version > 1)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Schedule state from a newer version"));
	}
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Restore on the authority"));
	}
	for (const FDocNPCSaveRecord& S : Data.Records)
	{
		if (!Definitions.Contains(S.ScheduleId))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Schedule %s for %s is not registered"), *S.ScheduleId.ToString(), *S.NPCId.ToString()));
		}
	}
	++GlobalEpoch; // old override handles become stale
	OverrideHandles.Reset();
	for (const FDocNPCSaveRecord& S : Data.Records)
	{
		FRecord& R = Records.FindOrAdd(S.NPCId);
		R.NPCId = S.NPCId;
		R.Schedule = Definitions[S.ScheduleId];
		R.Epoch = GlobalEpoch;
		R.Overrides.Reset();
		for (const FDocScheduleOverrideRecord& O : S.DurableOverrides)
		{
			FOverride Restored;
			Restored.Data = O;
			Restored.bOwnerless = true; // owner objects do not survive a save; admin clear or expiry ends it
			Restored.Epoch = GlobalEpoch;
			NextOverrideId = FMath::Max(NextOverrideId, O.OverrideId + 1);
			NextPushOrder = FMath::Max(NextPushOrder, O.PushOrder + 1);
			R.Overrides.Add(Restored);
		}
		R.Expected = S.Expected;
		R.LastEvaluated = S.LastEvaluated;
		R.bHasDesired = false;
		R.RetryCount = 0;
		R.RetryAt = 0.0;
		R.bRestoredDispatch = true; // fresh executor/target binding; start rewards suppressed
		if (UDocNPCScheduleComponent* Component = R.Representation.Get())
		{
			Component->DeliverCancel(R.Requested.RequestId);
		}
		R.Requested = FDocNPCActivityRequest();
		R.Status = EDocActivityStatus::None;
		Evaluate(R, Now());
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

UDocNPCScheduleComponent::UDocNPCScheduleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocNPCScheduleComponent::BeginPlay()
{
	Super::BeginPlay();
	UDocNPCScheduleSubsystem* Schedules = UDocNPCScheduleSubsystem::Get(this);
	if (!Schedules || NPCId.IsNone())
	{
		return;
	}
	if (!Schedules->HasNPC(NPCId) && Schedule)
	{
		Schedules->RegisterNPC(NPCId, Schedule);
	}
	Schedules->BindRepresentation(NPCId, this);
}

void UDocNPCScheduleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocNPCScheduleSubsystem* Schedules = UDocNPCScheduleSubsystem::Get(this))
	{
		Schedules->UnbindRepresentation(NPCId, this); // the logical record stays
	}
	Super::EndPlay(EndPlayReason);
}

FDocSystemResult UDocNPCScheduleComponent::ReportStatus(int64 RequestId, EDocActivityStatus Status, const FString& Reason)
{
	UDocNPCScheduleSubsystem* Schedules = UDocNPCScheduleSubsystem::Get(this);
	return Schedules ? Schedules->ReportActivityStatus(NPCId, RequestId, Status, Reason)
		: FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No schedule subsystem"));
}

void UDocNPCScheduleComponent::DeliverRequest(const FDocNPCActivityRequest& Request)
{
	CurrentRequest = Request;
	OnActivityRequestedNative.Broadcast(Request);
	OnActivityRequested.Broadcast(Request);
}

void UDocNPCScheduleComponent::DeliverCancel(int64 RequestId)
{
	if (RequestId == 0)
	{
		return;
	}
	OnActivityCancelledNative.Broadcast(NPCId, RequestId);
	OnActivityCancelled.Broadcast(RequestId);
}
