#include "DocObjectiveSubsystem.h"
#include "DocQuestObjectivesLog.h"
#include "DocCoreTags.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocObjectiveSubsystem)

namespace DocQuestPrivate
{
	bool IsTerminal(EDocQuestState State)
	{
		return State == EDocQuestState::Completed || State == EDocQuestState::Failed || State == EDocQuestState::Cancelled;
	}

	bool IsObjectiveOpen(EDocObjectiveState State)
	{
		return State == EDocObjectiveState::Inactive || State == EDocObjectiveState::Active;
	}

	bool TagMatches(const FGameplayTag& Have, const FGameplayTag& Want, bool bExact)
	{
		if (!Want.IsValid())
		{
			return true;
		}
		if (!Have.IsValid())
		{
			return false;
		}
		return bExact ? Have == Want : Have.MatchesTag(Want);
	}

	bool FilterMatches(const FGameplayTag& EventTag, const FDocObjectiveFilter& F, const FDocQuestObservation& O)
	{
		return EventTag.IsValid()
			&& TagMatches(O.EventTag, EventTag, F.bExactEventTag)
			&& TagMatches(O.TargetTag, F.TargetTag, F.bExactTarget)
			&& TagMatches(O.SenderTag, F.SenderTag, F.bExactSender)
			&& O.PayloadTags.HasAll(F.RequiredPayloadTags);
	}

	bool UsesObservations(const FDocObjectiveDefinition& D)
	{
		return D.ProgressMode == EDocObjectiveProgressMode::Cumulative
			&& D.Evaluator != EDocObjectiveEvaluator::Wait && D.Evaluator != EDocObjectiveEvaluator::CustomCondition;
	}

	bool IsRetryable(EDocResultOutcome Outcome)
	{
		return !(Outcome == EDocResultOutcome::InvalidInput || Outcome == EDocResultOutcome::InvalidConfiguration
			|| Outcome == EDocResultOutcome::Unsupported || Outcome == EDocResultOutcome::PermissionDenied);
	}

	FString EnumName(EDocQuestState State)
	{
		return StaticEnum<EDocQuestState>()->GetNameStringByValue(static_cast<int64>(State));
	}
}

using namespace DocQuestPrivate;

static TWeakObjectPtr<UDocObjectiveSubsystem> GDocObjectiveTestOverride;

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocObjectiveSubsystem* UDocObjectiveSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocObjectiveSubsystem* Override = GDocObjectiveTestOverride.Get())
	{
		return Override;
	}
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UDocObjectiveSubsystem>() : nullptr;
}

void UDocObjectiveSubsystem::SetSubsystemOverrideForTesting(UDocObjectiveSubsystem* Override)
{
	GDocObjectiveTestOverride = Override;
}

void UDocObjectiveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocObjectiveSubsystem::Deinitialize()
{
	PendingEvents.Reset();
	PendingDeliveries.Reset();
	PendingAutoActivation.Reset();
	AttachedWorld.Reset();
	Super::Deinitialize();
}

bool UDocObjectiveSubsystem::HasAuthority() const
{
	const UWorld* World = AttachedWorld.Get();
	return !World || World->GetNetMode() != NM_Client;
}

const UDocQuestSettings* UDocObjectiveSubsystem::Settings() const
{
	return GetDefault<UDocQuestSettings>();
}

void UDocObjectiveSubsystem::AttachWorld(UWorld* World)
{
	if (!World || AttachedWorld.Get() == World)
	{
		return;
	}
	AttachedWorld = World;
	++WorldGeneration; // observations stamped for any previous world are now stale
	MarkIndexDirty();
}

void UDocObjectiveSubsystem::DetachWorld(UWorld* World)
{
	if (World && AttachedWorld.Get() == World)
	{
		AttachedWorld.Reset();
		++WorldGeneration; // records are retained; no actor or bus pointers are kept
	}
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void UDocObjectiveSubsystem::QueueQuestChange(const FDocQuestOwnerRecord& R, const FString& Cause)
{
	FDocQuestChange C;
	C.Owner = R.Owner;
	C.QuestId = R.QuestId;
	C.QuestInstanceId = R.Current.QuestInstanceId;
	C.QuestState = R.Current.State;
	C.StageId = R.Current.StageId;
	C.Revision = R.Current.Revision;
	C.Cause = Cause;
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocObjectiveSubsystem>(this), C]()
	{
		if (UDocObjectiveSubsystem* This = WeakThis.Get())
		{
			This->OnQuestChangedNative.Broadcast(C);
			This->OnQuestChanged.Broadcast(C);
		}
	});
}

void UDocObjectiveSubsystem::QueueObjectiveChange(const FDocQuestOwnerRecord& R, const FDocObjectiveRuntimeState& O, const FString& Cause)
{
	FDocQuestChange C;
	C.Owner = R.Owner;
	C.QuestId = R.QuestId;
	C.QuestInstanceId = R.Current.QuestInstanceId;
	C.QuestState = R.Current.State;
	C.StageId = R.Current.StageId;
	C.ObjectiveId = O.ObjectiveId;
	C.ObjectiveState = O.State;
	C.Count = O.Count;
	C.TargetCount = O.TargetCount;
	C.Revision = R.Current.Revision;
	C.Cause = Cause;
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocObjectiveSubsystem>(this), C]()
	{
		if (UDocObjectiveSubsystem* This = WeakThis.Get())
		{
			This->OnObjectiveChangedNative.Broadcast(C);
			This->OnObjectiveChanged.Broadcast(C);
		}
	});
}

void UDocObjectiveSubsystem::QueueRewardChange(const FDocQuestOwnerRecord& R)
{
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocObjectiveSubsystem>(this), Owner = R.Owner, QuestId = R.QuestId, bPending = R.Current.bRewardDeliveryPending]()
	{
		if (UDocObjectiveSubsystem* This = WeakThis.Get())
		{
			This->OnRewardDeliveryChangedNative.Broadcast(Owner, QuestId, bPending);
			This->OnRewardDeliveryChanged.Broadcast(Owner, QuestId, bPending);
		}
	});
}

void UDocObjectiveSubsystem::QueueTrackingChange(const FDocOwnerScope& Viewer)
{
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocObjectiveSubsystem>(this), Viewer]()
	{
		if (UDocObjectiveSubsystem* This = WeakThis.Get())
		{
			This->OnTrackingChangedNative.Broadcast(Viewer);
			This->OnTrackingChanged.Broadcast(Viewer);
		}
	});
}

void UDocObjectiveSubsystem::LeaveApi()
{
	if (--ApiDepth > 0)
	{
		return;
	}
	++ApiDepth; // nested calls from handlers/providers queue instead of flushing
	int32 Passes = 0;
	while ((PendingEvents.Num() > 0 || PendingDeliveries.Num() > 0 || (!bRestoring && PendingAutoActivation.Num() > 0)) && Passes++ < 64)
	{
		// 1. Publish the committed revision.
		while (PendingEvents.Num() > 0)
		{
			TArray<TFunction<void()>> Batch = MoveTemp(PendingEvents);
			PendingEvents.Reset();
			for (TFunction<void()>& Event : Batch)
			{
				Event();
			}
		}
		// 2. Deliver staged effect intents.
		if (PendingDeliveries.Num() > 0)
		{
			TArray<FDeliveryRef> Batch = MoveTemp(PendingDeliveries);
			PendingDeliveries.Reset();
			for (const FDeliveryRef& D : Batch)
			{
				DeliverIntent(D.Key, D.EffectKey);
			}
		}
		// 3. Bounded automatic activation (suppressed during restore).
		if (!bRestoring && PendingAutoActivation.Num() > 0)
		{
			TArray<FDocOwnerScope> Owners = PendingAutoActivation.Array();
			PendingAutoActivation.Reset();
			for (const FDocOwnerScope& Owner : Owners)
			{
				RunAutoActivation(Owner);
			}
		}
	}
	--ApiDepth;
}

// ---------------------------------------------------------------------------
// Definitions / providers
// ---------------------------------------------------------------------------

FDocSystemResult UDocObjectiveSubsystem::RegisterQuestDefinition(UDocQuestDefinition* Definition)
{
	FApiScope Scope(*this);
	if (!Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No definition"));
	}
	TArray<FString> Errors, Warnings;
	Definition->FindProblems(Errors, Warnings);
	if (!Errors.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Quest %s is invalid: %s"), *Definition->QuestId.ToString(), *FString::Join(Errors, TEXT("; "))));
	}
	if (const TObjectPtr<UDocQuestDefinition>* Existing = Definitions.Find(Definition->QuestId))
	{
		if (*Existing == Definition)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Already registered"));
		}
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Another definition already uses QuestId %s"), *Definition->QuestId.ToString()));
	}
	Definitions.Add(Definition->QuestId, Definition);
	DefinitionRefs.Add(Definition);
	MarkIndexDirty();
	for (const FDocOwnerScope& Owner : KnownOwners)
	{
		PendingAutoActivation.Add(Owner);
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocObjectiveSubsystem::RegisterObjectiveAsset(UDocObjectiveAsset* Objective)
{
	if (!Objective || Objective->Objective.ObjectiveId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No objective or ObjectiveId"));
	}
	const FName RecordId = Objective->GetRecordId();
	if (StandaloneRecordIds.Contains(Objective->Objective.ObjectiveId))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already registered"));
	}
	// Synthesized single-stage definition: same engine, own instance/owner keys.
	UDocQuestDefinition* Def = NewObject<UDocQuestDefinition>(this);
	Def->QuestId = RecordId;
	Def->ContentVersion = Objective->ContentVersion;
	Def->Title = Objective->Objective.DisplayName;
	Def->Description = Objective->Objective.Description;
	FDocQuestStage Stage;
	Stage.StageId = TEXT("Main");
	Stage.Objectives = { Objective->Objective };
	Stage.Objectives[0].bOptional = false;
	Def->Stages = { Stage };
	Def->Rewards = Objective->Rewards;
	Def->bRepeatable = Objective->bRepeatable;
	Def->OwnerPolicy = Objective->OwnerPolicy;
	Def->TimerClock = Objective->TimerClock;
	Def->bTrackable = Objective->Objective.TrackingMode != EDocObjectiveTrackingMode::NotTrackable;
	const FDocSystemResult R = RegisterQuestDefinition(Def);
	if (R.IsSuccess())
	{
		StandaloneRecordIds.Add(Objective->Objective.ObjectiveId, RecordId);
	}
	return R;
}

const UDocQuestDefinition* UDocObjectiveSubsystem::FindDefinition(FName QuestId) const
{
	const TObjectPtr<UDocQuestDefinition>* Found = Definitions.Find(QuestId);
	return Found ? Found->Get() : nullptr;
}

void UDocObjectiveSubsystem::RegisterConditionProvider(FName ProviderId, TSharedPtr<IDocQuestConditionProvider> Provider)
{
	if (Provider.IsValid()) { ConditionProviders.Add(ProviderId, Provider); } else { ConditionProviders.Remove(ProviderId); }
}

void UDocObjectiveSubsystem::RegisterStateProvider(FName ProviderId, TSharedPtr<IDocQuestStateProvider> Provider)
{
	if (Provider.IsValid()) { StateProviders.Add(ProviderId, Provider); } else { StateProviders.Remove(ProviderId); }
}

void UDocObjectiveSubsystem::RegisterEffectProvider(FName ProviderId, TSharedPtr<IDocQuestEffectProvider> Provider)
{
	if (Provider.IsValid()) { EffectProviders.Add(ProviderId, Provider); } else { EffectProviders.Remove(ProviderId); }
}

void UDocObjectiveSubsystem::RegisterOwner(const FDocOwnerScope& Owner)
{
	if (!Owner.IsValid())
	{
		return;
	}
	FApiScope Scope(*this);
	KnownOwners.Add(Owner);
	PendingAutoActivation.Add(Owner);
}

const FDocObjectiveDefinition* UDocObjectiveSubsystem::FindObjectiveDef(const UDocQuestDefinition& Def, FName StageId, FName ObjectiveId) const
{
	const FDocQuestStage* Stage = Def.FindStage(StageId);
	return Stage ? Stage->FindObjective(ObjectiveId) : nullptr;
}

FDocSystemResult UDocObjectiveSubsystem::CheckOwnerPolicy(const UDocQuestDefinition& Def, const FDocOwnerScope& Owner) const
{
	if (!Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("A valid owner scope is required"));
	}
	EDocOwnerScopeKind Expected = EDocOwnerScopeKind::PlayerProfile;
	switch (Def.OwnerPolicy)
	{
	case EDocQuestOwnerPolicy::PerPlayer: Expected = EDocOwnerScopeKind::PlayerProfile; break;
	case EDocQuestOwnerPolicy::SharedWorld: Expected = EDocOwnerScopeKind::SharedWorld; break;
	case EDocQuestOwnerPolicy::Party: Expected = EDocOwnerScopeKind::Party; break;
	}
	// Shared-world and private-player records never merge implicitly: the scope kind must match the policy.
	if (Owner.Kind != Expected)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("Quest %s requires a %s owner scope"), *Def.QuestId.ToString(),
				*StaticEnum<EDocOwnerScopeKind>()->GetNameStringByValue(static_cast<int64>(Expected))),
			DocQuestTags::Error_Quest_OwnerPolicy);
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Conditions
// ---------------------------------------------------------------------------

FDocConditionResult UDocObjectiveSubsystem::EvaluateCondition(const FDocOwnerScope& Owner, FName QuestId, const FDocQuestCondition& C) const
{
	FDocConditionResult R;
	auto Bool = [](bool b, const FString& Why) { return b ? FDocConditionResult::Satisfied() : FDocConditionResult::Unsatisfied(DocQuestTags::Error_Quest_PrerequisitesUnmet, Why); };
	const FDocQuestOwnerRecord* Other = C.QuestId.IsNone() ? nullptr : Records.Find(FQuestKey{ Owner, C.QuestId });
	switch (C.Type)
	{
	case EDocQuestConditionType::QuestCompleted:
		R = Bool(Other && Other->TimesCompleted > 0, FString::Printf(TEXT("Quest %s not completed"), *C.QuestId.ToString()));
		break;
	case EDocQuestConditionType::QuestActive:
		R = Bool(Other && Other->Current.State == EDocQuestState::Active, FString::Printf(TEXT("Quest %s not active"), *C.QuestId.ToString()));
		break;
	case EDocQuestConditionType::QuestNotStarted:
		R = Bool(!Other, FString::Printf(TEXT("Quest %s already started"), *C.QuestId.ToString()));
		break;
	case EDocQuestConditionType::Provider:
	{
		const TSharedPtr<IDocQuestConditionProvider>* Provider = ConditionProviders.Find(C.ProviderId);
		if (!Provider || !Provider->IsValid())
		{
			return FDocConditionResult::Unavailable(DocQuestTags::Error_Quest_ProviderMissing,
				FString::Printf(TEXT("No condition provider '%s'"), *C.ProviderId.ToString()));
		}
		FDocQuestConditionQuery Query;
		Query.Condition = &C;
		Query.Owner = Owner;
		Query.QuestId = QuestId;
		R = (*Provider)->EvaluateQuestCondition(Query);
		break;
	}
	}
	if (C.bNegate && !R.IsUnavailable())
	{
		R.State = R.IsSatisfied() ? EDocConditionState::Unsatisfied : EDocConditionState::Satisfied;
	}
	return R;
}

FDocConditionResult UDocObjectiveSubsystem::EvaluateConditions(const FDocOwnerScope& Owner, FName QuestId, const TArray<FDocQuestCondition>& Conditions) const
{
	TArray<FDocConditionResult> Results;
	for (const FDocQuestCondition& C : Conditions)
	{
		Results.Add(EvaluateCondition(Owner, QuestId, C));
	}
	return FDocConditionResult::CombineAll(Results);
}

FDocConditionResult UDocObjectiveSubsystem::EvaluatePrerequisites(const FDocOwnerScope& Owner, FName QuestId) const
{
	const UDocQuestDefinition* Def = FindDefinition(QuestId);
	if (!Def)
	{
		return FDocConditionResult::Unavailable(DocQuestTags::Error_Quest_UnknownDefinition, FString::Printf(TEXT("Unknown quest %s"), *QuestId.ToString()));
	}
	return EvaluateConditions(Owner, QuestId, Def->Prerequisites);
}

// ---------------------------------------------------------------------------
// Activation and the stage machine
// ---------------------------------------------------------------------------

FDocSystemResult UDocObjectiveSubsystem::ActivateQuest(const FDocOwnerScope& Owner, FName QuestId, FGuid& OutQuestInstanceId)
{
	FApiScope Scope(*this);
	const UDocQuestDefinition* Def = FindDefinition(QuestId);
	if (!Def)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Unknown quest %s"), *QuestId.ToString()), DocQuestTags::Error_Quest_UnknownDefinition);
	}
	return ActivateInternal(Owner, *Def, StandaloneRecordIds.FindKey(QuestId) != nullptr, OutQuestInstanceId);
}

FDocSystemResult UDocObjectiveSubsystem::ActivateObjective(const FDocOwnerScope& Owner, FName ObjectiveId, FGuid& OutInstanceId)
{
	FApiScope Scope(*this);
	const FName* RecordId = StandaloneRecordIds.Find(ObjectiveId);
	const UDocQuestDefinition* Def = RecordId ? FindDefinition(*RecordId) : nullptr;
	if (!Def)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Unknown standalone objective %s"), *ObjectiveId.ToString()), DocQuestTags::Error_Quest_UnknownDefinition);
	}
	return ActivateInternal(Owner, *Def, true, OutInstanceId);
}

FDocSystemResult UDocObjectiveSubsystem::ActivateInternal(const FDocOwnerScope& Owner, const UDocQuestDefinition& Def, bool bStandalone, FGuid& OutInstanceId)
{
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Quest state changes on the authority"));
	}
	const FDocSystemResult Policy = CheckOwnerPolicy(Def, Owner);
	if (!Policy.IsSuccess())
	{
		return Policy;
	}
	const FQuestKey Key{ Owner, Def.QuestId };
	if (const FDocQuestOwnerRecord* Existing = Records.Find(Key))
	{
		if (Existing->bQuarantined)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Record is quarantined"), DocQuestTags::Error_Quest_Quarantined);
		}
		if (Existing->Current.State == EDocQuestState::Active)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Quest is already active"), DocQuestTags::Error_Quest_AlreadyActive);
		}
		if (IsTerminal(Existing->Current.State))
		{
			if (!Def.bRepeatable)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
					FString::Printf(TEXT("Quest ended (%s) and is not repeatable"), *EnumName(Existing->Current.State)), DocQuestTags::Error_Quest_NotRepeatable);
			}
			if (Def.MaxRepeats > 0 && Existing->Current.RepeatOrdinal >= Def.MaxRepeats)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Repeat limit reached"), DocQuestTags::Error_Quest_NotRepeatable);
			}
			if (Existing->Current.bRewardDeliveryPending)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Previous run still has pending rewards"), DocQuestTags::Error_Quest_RewardPending);
			}
		}
	}
	const FDocConditionResult Prereq = EvaluateConditions(Owner, Def.QuestId, Def.Prerequisites);
	if (Prereq.IsUnavailable())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, Prereq.Diagnostic, Prereq.ReasonTag);
	}
	if (!Prereq.IsSatisfied())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, Prereq.Diagnostic, DocQuestTags::Error_Quest_PrerequisitesUnmet);
	}

	FDocQuestOwnerRecord& R = Records.FindOrAdd(Key);
	const int32 NextOrdinal = R.Current.RepeatOrdinal + 1;
	R.Owner = Owner;
	R.QuestId = Def.QuestId;
	R.bStandalone = bStandalone;
	R.Current = FDocQuestRuntimeState();
	FDocQuestRuntimeState& Q = R.Current;
	Q.QuestId = Def.QuestId;
	Q.QuestInstanceId = FGuid::NewGuid(); // repeats never share progress, timers, dedup or effect keys
	Q.RepeatOrdinal = NextOrdinal;
	Q.Owner = Owner;
	Q.State = EDocQuestState::Active;
	Q.ContentVersion = Def.ContentVersion;
	Q.ActivatedAtUtcTicks = FDateTime::UtcNow().GetTicks();
	Q.Revision = 1;
	Q.LastTransitionCause = TEXT("Activated");
	OutInstanceId = Q.QuestInstanceId;
	QueueQuestChange(R, Q.LastTransitionCause);

	EnterStage(R, Def, Def.Stages[0].StageId, TEXT("Activated"));
	EvaluateQuest(R, Def, TEXT("Activated"), {});
	MarkIndexDirty();
	return FDocSystemResult::MakeSuccess();
}

void UDocObjectiveSubsystem::EnterStage(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, FName StageId, const FString& Cause)
{
	FDocQuestRuntimeState& Q = R.Current;
	const FDocQuestStage* Stage = Def.FindStage(StageId);
	Q.StageId = StageId;
	Q.Objectives.Reset();
	++Q.Revision;
	Q.LastTransitionCause = FString::Printf(TEXT("Entered stage %s (%s)"), *StageId.ToString(), *Cause);
	if (!Stage)
	{
		return;
	}
	for (const FDocObjectiveDefinition& O : Stage->Objectives)
	{
		FDocObjectiveRuntimeState S;
		S.ObjectiveId = O.ObjectiveId;
		S.bHidden = O.bHidden;
		S.bOptional = O.bOptional;
		S.TargetCount = FMath::Max(1, O.TargetCount);
		Q.Objectives.Add(S);
	}
	QueueQuestChange(R, Q.LastTransitionCause);
	ActivateEligible(R, Def, *Stage, Cause);
	MarkIndexDirty();
}

bool UDocObjectiveSubsystem::ActivateEligible(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocQuestStage& Stage, const FString& Cause)
{
	FDocQuestRuntimeState& Q = R.Current;
	if (Q.State != EDocQuestState::Active)
	{
		return false;
	}
	if (Stage.Activation == EDocStageActivation::Ordered
		&& Q.Objectives.ContainsByPredicate([](const FDocObjectiveRuntimeState& O) { return O.State == EDocObjectiveState::Active; }))
	{
		return false; // one at a time
	}
	bool bActivated = false;
	for (int32 i = 0; i < Q.Objectives.Num(); ++i)
	{
		if (Q.Objectives[i].State != EDocObjectiveState::Inactive || Q.Objectives[i].bQuarantined)
		{
			continue;
		}
		const FDocObjectiveDefinition* ODef = Stage.FindObjective(Q.Objectives[i].ObjectiveId);
		if (!ODef)
		{
			continue;
		}
		const FDocConditionResult Eligible = EvaluateConditions(R.Owner, R.QuestId, ODef->Conditions);
		if (!Eligible.IsSatisfied())
		{
			Q.Objectives[i].LastIgnoredReason = Eligible.IsUnavailable()
				? FString::Printf(TEXT("Eligibility unavailable: %s"), *Eligible.Diagnostic)
				: FString::Printf(TEXT("Not eligible: %s"), *Eligible.Diagnostic);
			continue;
		}
		ActivateObjective(R, Def, *ODef, i, Cause);
		bActivated = true;
		if (Stage.Activation == EDocStageActivation::Ordered)
		{
			break;
		}
	}
	return bActivated;
}

void UDocObjectiveSubsystem::ActivateObjective(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocObjectiveDefinition& ODef, int32 Index, const FString& Cause)
{
	FDocQuestRuntimeState& Q = R.Current;
	FDocObjectiveRuntimeState& O = Q.Objectives[Index];
	O.State = EDocObjectiveState::Active;
	O.ObjectiveInstanceId = FGuid::NewGuid();
	O.Count = 0;
	O.LastIgnoredReason.Reset();
	O.WaitRemaining = ODef.Evaluator == EDocObjectiveEvaluator::Wait ? FMath::Max(0.0, static_cast<double>(ODef.WaitSeconds)) : -1.0;
	O.LimitRemaining = ODef.TimeLimitSeconds > 0.f ? static_cast<double>(ODef.TimeLimitSeconds) : -1.0;
	++Q.Revision;
	QueueObjectiveChange(R, O, Cause);
	MarkIndexDirty();

	if (ODef.Evaluator == EDocObjectiveEvaluator::Wait && O.WaitRemaining <= 0.0)
	{
		CompleteObjective(R, Def, ODef, Index, TEXT("Wait of 0 seconds"));
	}
	else if (ODef.ProgressMode == EDocObjectiveProgressMode::CurrentState || ODef.Evaluator == EDocObjectiveEvaluator::CustomCondition)
	{
		RefreshObjectiveState(R, Def, ODef, Index, Cause);
	}
}

void UDocObjectiveSubsystem::CompleteObjective(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocObjectiveDefinition& ODef, int32 Index, const FString& Cause)
{
	FDocQuestRuntimeState& Q = R.Current;
	FDocObjectiveRuntimeState& O = Q.Objectives[Index];
	if (O.State != EDocObjectiveState::Active)
	{
		return;
	}
	O.State = EDocObjectiveState::Completed;
	O.WaitRemaining = -1.0;
	O.LimitRemaining = -1.0;
	++Q.TransitionOrdinal;
	++Q.Revision;
	QueueObjectiveChange(R, O, Cause);
	AddIntents(R, Def, ODef.CompletionActions, TEXT("ObjectiveCompletion"));
	MarkIndexDirty();
}

void UDocObjectiveSubsystem::FailObjective(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocObjectiveDefinition& ODef, int32 Index, const FString& Cause)
{
	FDocQuestRuntimeState& Q = R.Current;
	FDocObjectiveRuntimeState& O = Q.Objectives[Index];
	if (O.State != EDocObjectiveState::Active)
	{
		return;
	}
	O.State = EDocObjectiveState::Failed;
	O.WaitRemaining = -1.0;
	O.LimitRemaining = -1.0;
	++Q.TransitionOrdinal;
	++Q.Revision;
	QueueObjectiveChange(R, O, Cause);
	AddIntents(R, Def, ODef.FailureActions, TEXT("ObjectiveFailure"));
	MarkIndexDirty();
}

bool UDocObjectiveSubsystem::RefreshObjectiveState(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocObjectiveDefinition& ODef, int32 Index, const FString& Cause)
{
	FDocObjectiveRuntimeState& O = R.Current.Objectives[Index];
	if (O.State != EDocObjectiveState::Active)
	{
		return false;
	}
	if (ODef.Evaluator == EDocObjectiveEvaluator::CustomCondition)
	{
		FDocQuestCondition Condition;
		Condition.Type = EDocQuestConditionType::Provider;
		Condition.ProviderId = ODef.ProviderId;
		Condition.QueryId = ODef.StateQueryId;
		const FDocConditionResult Result = EvaluateCondition(R.Owner, R.QuestId, Condition);
		if (Result.IsSatisfied())
		{
			O.Count = O.TargetCount;
			CompleteObjective(R, Def, ODef, Index, Cause);
			return true;
		}
		O.LastIgnoredReason = Result.IsUnavailable() ? FString::Printf(TEXT("Provider unavailable: %s"), *Result.Diagnostic) : TEXT("Condition not satisfied");
		return false;
	}
	if (ODef.ProgressMode != EDocObjectiveProgressMode::CurrentState)
	{
		return false;
	}
	const TSharedPtr<IDocQuestStateProvider>* Provider = StateProviders.Find(ODef.ProviderId);
	int32 Value = 0;
	if (!Provider || !Provider->IsValid() || !(*Provider)->QueryCurrentCount(R.Owner, ODef, Value))
	{
		O.LastIgnoredReason = FString::Printf(TEXT("State provider '%s' unavailable"), *ODef.ProviderId.ToString());
		return false; // never guessed
	}
	Value = FMath::Clamp(Value, 0, O.TargetCount);
	if (!ODef.bAllowRegression && Value < O.Count)
	{
		Value = O.Count;
	}
	if (Value != O.Count)
	{
		O.Count = Value;
		++R.Current.Revision;
		QueueObjectiveChange(R, O, Cause);
	}
	if (O.Count >= O.TargetCount)
	{
		CompleteObjective(R, Def, ODef, Index, Cause);
		return true;
	}
	return false;
}

bool UDocObjectiveSubsystem::IsStageComplete(const FDocQuestStage& Stage, const FDocQuestRuntimeState& Q) const
{
	int32 Required = 0;
	int32 Completed = 0;
	for (const FDocObjectiveRuntimeState& O : Q.Objectives)
	{
		if (O.bOptional || O.bQuarantined)
		{
			continue;
		}
		++Required;
		Completed += O.State == EDocObjectiveState::Completed ? 1 : 0;
	}
	if (Required == 0)
	{
		return Stage.bPassThrough; // accidental empty sets never complete (and fail validation)
	}
	return Stage.Completion == EDocStageCompletion::AllRequired ? Completed == Required : Completed > 0;
}

void UDocObjectiveSubsystem::CloseStage(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocQuestStage& Stage, bool bSucceeded, const FString& Cause)
{
	FDocQuestRuntimeState& Q = R.Current;
	for (FDocObjectiveRuntimeState& O : Q.Objectives)
	{
		if (!IsObjectiveOpen(O.State) || O.bQuarantined)
		{
			continue;
		}
		const bool bFailOptional = O.bOptional && O.State == EDocObjectiveState::Active && Stage.OptionalClose == EDocOptionalObjectiveClose::FailOnStageEnd;
		O.State = bFailOptional ? EDocObjectiveState::Failed : EDocObjectiveState::Cancelled;
		O.WaitRemaining = -1.0;
		O.LimitRemaining = -1.0;
		QueueObjectiveChange(R, O, FString::Printf(TEXT("Stage %s closed"), *Stage.StageId.ToString()));
	}
	if (bSucceeded)
	{
		Q.CompletedStages.AddUnique(Stage.StageId);
		++Q.TransitionOrdinal;
		AddIntents(R, Def, Stage.CompletionActions, TEXT("StageCompletion"));
	}
	++Q.Revision;
	MarkIndexDirty();
}

void UDocObjectiveSubsystem::EndQuest(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, EDocQuestState Final, const FString& Cause)
{
	FDocQuestRuntimeState& Q = R.Current;
	if (Q.State != EDocQuestState::Active)
	{
		return; // exactly one terminal transition
	}
	for (FDocObjectiveRuntimeState& O : Q.Objectives)
	{
		if (IsObjectiveOpen(O.State) && !O.bQuarantined)
		{
			O.State = EDocObjectiveState::Cancelled;
			O.WaitRemaining = -1.0;
			O.LimitRemaining = -1.0;
			QueueObjectiveChange(R, O, TEXT("Quest ended"));
		}
	}
	Q.State = Final;
	Q.EndedAtUtcTicks = FDateTime::UtcNow().GetTicks();
	Q.LastTransitionCause = Cause;
	++Q.Revision;
	switch (Final)
	{
	case EDocQuestState::Completed:
		++R.TimesCompleted;
		++Q.TransitionOrdinal;
		AddIntents(R, Def, Def.Rewards, TEXT("Reward"));
		break;
	case EDocQuestState::Failed: ++R.TimesFailed; break;
	case EDocQuestState::Cancelled: ++R.TimesCancelled; break;
	default: break;
	}
	RecomputeRewardPending(Q);
	QueueQuestChange(R, Cause);
	DropTrackingFor(R.Owner, R.QuestId);
	PendingAutoActivation.Add(R.Owner);
	MarkIndexDirty();
}

void UDocObjectiveSubsystem::EvaluateQuest(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FString& Cause, TArray<const FDocQuestFailureRule*> Rules)
{
	FDocQuestRuntimeState& Q = R.Current;
	int32 Guard = 64;
	while (Q.State == EDocQuestState::Active && Guard-- > 0)
	{
		const FDocQuestStage* Stage = Def.FindStage(Q.StageId);
		if (!Stage)
		{
			UE_LOG(LogDocQuestObjectives, Error, TEXT("Quest %s: current stage %s missing"), *R.QuestId.ToString(), *Q.StageId.ToString());
			return;
		}

		EFailKind Fail = EFailKind::None;
		FName BranchTo;
		auto ApplyFailure = [&](EDocQuestFailureAction Action, FName Branch)
		{
			switch (Action)
			{
			case EDocQuestFailureAction::FailQuest: Fail = EFailKind::Quest; break;
			case EDocQuestFailureAction::FailStage: if (Fail == EFailKind::None) { Fail = EFailKind::Stage; } break;
			case EDocQuestFailureAction::BranchToStage: if (Fail == EFailKind::None || Fail == EFailKind::Stage) { Fail = EFailKind::Branch; BranchTo = Branch; } break;
			case EDocQuestFailureAction::RemainActive: break;
			}
		};
		for (const FDocQuestFailureRule* Rule : Rules)
		{
			ApplyFailure(Rule->Action, Rule->BranchStageId);
			Q.LastTransitionCause = FString::Printf(TEXT("Failure rule %s"), *Rule->RuleId.ToString());
		}
		Rules.Reset();
		for (const FDocObjectiveRuntimeState& O : Q.Objectives)
		{
			if (!O.bOptional && !O.bQuarantined && O.State == EDocObjectiveState::Failed)
			{
				ApplyFailure(Stage->OnRequiredObjectiveFailed, Stage->FailureBranchStage);
			}
		}

		bool bComplete = IsStageComplete(*Stage, Q);
		bool bFailing = Fail != EFailKind::None;
		if (bFailing && bComplete)
		{
			// Simultaneous success and failure: authored precedence, one terminal transition.
			if (Def.FailurePrecedence == EDocQuestFailurePrecedence::FailureWins) { bComplete = false; } else { bFailing = false; }
		}

		if (bFailing)
		{
			if (Fail == EFailKind::Quest)
			{
				EndQuest(R, Def, EDocQuestState::Failed, FString::Printf(TEXT("Failed: %s"), *Cause));
				return;
			}
			const FName Target = Fail == EFailKind::Branch ? BranchTo : Stage->FailureBranchStage;
			if (Target.IsNone() || !Def.FindStage(Target))
			{
				EndQuest(R, Def, EDocQuestState::Failed, FString::Printf(TEXT("Stage %s failed: %s"), *Stage->StageId.ToString(), *Cause));
				return;
			}
			CloseStage(R, Def, *Stage, false, Cause);
			EnterStage(R, Def, Target, FString::Printf(TEXT("Failure branch (%s)"), *Cause));
			continue;
		}

		if (bComplete)
		{
			CloseStage(R, Def, *Stage, true, Cause);
			FName Next = Stage->NextStage;
			if (!Stage->bCompletesQuest && Next.IsNone())
			{
				const int32 Index = Def.FindStageIndex(Stage->StageId);
				if (Def.Stages.IsValidIndex(Index + 1))
				{
					Next = Def.Stages[Index + 1].StageId;
				}
			}
			if (Stage->bCompletesQuest || Next.IsNone())
			{
				EndQuest(R, Def, EDocQuestState::Completed, Cause);
				return;
			}
			EnterStage(R, Def, Next, Cause);
			continue;
		}

		if (ActivateEligible(R, Def, *Stage, Cause))
		{
			continue; // activation may have completed CurrentState objectives
		}
		return;
	}
	if (Guard <= 0)
	{
		UE_LOG(LogDocQuestObjectives, Warning, TEXT("Quest %s: evaluation guard reached (check stage links)"), *R.QuestId.ToString());
	}
}

void UDocObjectiveSubsystem::AddIntents(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const TArray<FDocQuestAction>& Actions, FName Source)
{
	FDocQuestRuntimeState& Q = R.Current;
	const bool bIsReward = Source == TEXT("Reward");
	for (const FDocQuestAction& Action : Actions)
	{
		FDocQuestRewardIntent Intent;
		Intent.Key.Owner = R.Owner;
		Intent.Key.CampaignEpoch = R.Owner.CampaignNamespace;
		Intent.Key.ProducerInstanceId = Q.QuestInstanceId;
		Intent.Key.TransitionOrdinal = Q.TransitionOrdinal;
		Intent.Key.ActionId = Action.ActionId;
		Intent.Action = Action;
		Intent.Source = Source;
		Q.Intents.Add(Intent);
		if (!bIsReward || Def.RewardPolicy == EDocQuestRewardPolicy::DeliverOnCompletion)
		{
			PendingDeliveries.Add(FDeliveryRef{ FQuestKey{ R.Owner, R.QuestId }, Intent.Key });
		}
	}
	RecomputeRewardPending(Q);
}

void UDocObjectiveSubsystem::RecomputeRewardPending(FDocQuestRuntimeState& Q) const
{
	Q.bRewardDeliveryPending = Q.Intents.ContainsByPredicate([](const FDocQuestRewardIntent& I)
	{
		return I.Source == TEXT("Reward") && I.State == EDocRewardIntentState::Pending;
	});
}

FDocSystemResult UDocObjectiveSubsystem::CancelQuest(const FDocOwnerScope& Owner, FName QuestId)
{
	FApiScope Scope(*this);
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Quest state changes on the authority"));
	}
	FDocQuestOwnerRecord* R = FindRecord(FQuestKey{ Owner, QuestId });
	const UDocQuestDefinition* Def = FindDefinition(QuestId);
	if (!R || !Def)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No such quest record"));
	}
	if (R->Current.State != EDocQuestState::Active)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Quest is %s; terminal states never change"), *EnumName(R->Current.State)), DocQuestTags::Error_Quest_Terminal);
	}
	if (!Def->bCancellable)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Quest is not cancellable"));
	}
	EndQuest(*R, *Def, EDocQuestState::Cancelled, TEXT("Cancelled"));
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Observations
// ---------------------------------------------------------------------------

bool UDocObjectiveSubsystem::CheckAndRecordDedup(FDocQuestRuntimeState& Q, const FDocQuestObservation& Obs, FString& OutReason) const
{
	if (Obs.Sequence > 0)
	{
		FDocQuestDedupSource* Src = Q.DedupSources.FindByPredicate([&Obs](const FDocQuestDedupSource& S) { return S.SourceId == Obs.SourceId; });
		if (!Src)
		{
			Src = &Q.DedupSources.AddDefaulted_GetRef();
			Src->SourceId = Obs.SourceId;
			Src->SourceEpoch = Obs.SourceEpoch;
		}
		if (Obs.SourceEpoch < Src->SourceEpoch)
		{
			OutReason = TEXT("observation from an older source epoch");
			return true;
		}
		if (Obs.SourceEpoch > Src->SourceEpoch)
		{
			Src->SourceEpoch = Obs.SourceEpoch;
			Src->HighWater = 0;
			Src->WindowBits = 0;
		}
		if (Obs.Sequence > Src->HighWater)
		{
			const int64 Shift = Obs.Sequence - Src->HighWater;
			Src->WindowBits = Shift >= 64 ? 0 : (Src->WindowBits << Shift);
			Src->WindowBits |= 1ull;
			Src->HighWater = Obs.Sequence;
			return false;
		}
		const int64 Behind = Src->HighWater - Obs.Sequence;
		if (Behind >= 64)
		{
			OutReason = TEXT("sequence outside the out-of-order window");
			return true;
		}
		const uint64 Bit = 1ull << Behind;
		if (Src->WindowBits & Bit)
		{
			OutReason = TEXT("duplicate sequence");
			return true;
		}
		Src->WindowBits |= Bit;
		return false;
	}
	if (Q.RecentEventIds.Contains(Obs.EventId))
	{
		OutReason = TEXT("duplicate event id");
		return true;
	}
	Q.RecentEventIds.Add(Obs.EventId);
	const int32 Window = Settings()->EventIdWindow;
	if (Q.RecentEventIds.Num() > Window)
	{
		Q.RecentEventIds.RemoveAt(0, Q.RecentEventIds.Num() - Window); // bounded: dedup is not indefinite
	}
	return false;
}

int32 UDocObjectiveSubsystem::ApplyToSnapshot(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocQuestObservation& Obs, const TSet<FGuid>& Snapshot)
{
	FDocQuestRuntimeState& Q = R.Current;
	int32 Advanced = 0;
	const FString Cause = FString::Printf(TEXT("%s from %s"), *Obs.EventTag.ToString(), *Obs.SourceId.ToString());
	for (int32 i = 0; i < Q.Objectives.Num() && Q.State == EDocQuestState::Active; ++i)
	{
		FDocObjectiveRuntimeState& O = Q.Objectives[i];
		if (O.State != EDocObjectiveState::Active || O.bQuarantined || !Snapshot.Contains(O.ObjectiveInstanceId))
		{
			continue;
		}
		const FDocObjectiveDefinition* ODef = FindObjectiveDef(Def, Q.StageId, O.ObjectiveId);
		if (!ODef || !UsesObservations(*ODef) || !FilterMatches(ODef->EventTag, ODef->Filter, Obs))
		{
			continue;
		}
		// Checked arithmetic: never overflows, clamps at the target.
		const int64 Sum = static_cast<int64>(O.Count) + static_cast<int64>(Obs.Amount);
		O.Count = static_cast<int32>(FMath::Min<int64>(Sum, O.TargetCount));
		O.LastMatchedEventId = Obs.EventId;
		O.LastIgnoredReason.Reset();
		++Q.Revision;
		++Advanced;
		QueueObjectiveChange(R, O, Cause);
		if (O.Count >= O.TargetCount)
		{
			CompleteObjective(R, Def, *ODef, i, Cause);
		}
	}
	return Advanced;
}

int32 UDocObjectiveSubsystem::ProcessForQuest(FDocQuestOwnerRecord& R, const UDocQuestDefinition& Def, const FDocQuestObservation& Obs, TArray<FString>& OutIgnored)
{
	FDocQuestRuntimeState& Q = R.Current;
	if (R.bQuarantined || Q.State != EDocQuestState::Active)
	{
		return 0;
	}
	if (Obs.bHistorical && Def.ReplayPolicy != EDocQuestReplayPolicy::AcceptHistorical)
	{
		OutIgnored.Add(FString::Printf(TEXT("%s: historical observation not accepted"), *R.QuestId.ToString()));
		return 0;
	}
	FString Why;
	if (CheckAndRecordDedup(Q, Obs, Why))
	{
		OutIgnored.Add(FString::Printf(TEXT("%s: %s"), *R.QuestId.ToString(), *Why));
		for (FDocObjectiveRuntimeState& O : Q.Objectives)
		{
			if (O.State == EDocObjectiveState::Active) { O.LastIgnoredReason = Why; }
		}
		return 0;
	}

	TArray<const FDocQuestFailureRule*> Rules;
	for (const FDocQuestFailureRule& Rule : Def.FailureRules)
	{
		if ((Rule.StageId.IsNone() || Rule.StageId == Q.StageId) && FilterMatches(Rule.EventTag, Rule.Filter, Obs))
		{
			Rules.Add(&Rule);
		}
	}

	// Recommended default: the event is processed against the objectives active at dispatch start only.
	TSet<FGuid> Seen;
	for (const FDocObjectiveRuntimeState& O : Q.Objectives)
	{
		if (O.State == EDocObjectiveState::Active) { Seen.Add(O.ObjectiveInstanceId); }
	}
	int32 Advanced = ApplyToSnapshot(R, Def, Obs, Seen);
	const FString Cause = FString::Printf(TEXT("%s from %s"), *Obs.EventTag.ToString(), *Obs.SourceId.ToString());
	if (Advanced > 0 || Rules.Num() > 0)
	{
		EvaluateQuest(R, Def, Cause, Rules);
	}

	// Authored cascade: newly activated objectives may consume the same event, bounded.
	if (Def.EvaluationMode == EDocQuestEvaluationMode::BoundedCascade)
	{
		for (int32 Depth = 0; Depth < Def.MaxCascadeDepth && Q.State == EDocQuestState::Active; ++Depth)
		{
			TSet<FGuid> Fresh;
			for (const FDocObjectiveRuntimeState& O : Q.Objectives)
			{
				if (O.State == EDocObjectiveState::Active && !Seen.Contains(O.ObjectiveInstanceId)) { Fresh.Add(O.ObjectiveInstanceId); }
			}
			if (Fresh.IsEmpty())
			{
				break;
			}
			Seen.Append(Fresh);
			const int32 More = ApplyToSnapshot(R, Def, Obs, Fresh);
			if (More == 0)
			{
				break;
			}
			Advanced += More;
			EvaluateQuest(R, Def, Cause + TEXT(" (cascade)"), {});
		}
	}
	if (Advanced == 0 && Rules.IsEmpty())
	{
		OutIgnored.Add(FString::Printf(TEXT("%s: no active objective matched the filters"), *R.QuestId.ToString()));
	}
	return Advanced + Rules.Num();
}

FDocSystemResult UDocObjectiveSubsystem::SubmitObservation(const FDocQuestObservation& Obs)
{
	FApiScope Scope(*this);
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Observations are processed on the authority"), DocQuestTags::Error_Quest_UntrustedSource);
	}
	if (!TrustedSources.Contains(Obs.SourceId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied,
			FString::Printf(TEXT("Source %s is not a trusted observation source"), *Obs.SourceId.ToString()), DocQuestTags::Error_Quest_UntrustedSource);
	}
	if (!Obs.Owner.IsValid() || !Obs.EventTag.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Observation needs an owner and an event tag"));
	}
	if (!Obs.EventId.IsValid() && Obs.Sequence <= 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Observation needs an EventId or a source sequence"));
	}
	if (Obs.Amount <= 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Amount must be positive (%d)"), Obs.Amount));
	}
	if (Obs.WorldGeneration != 0 && Obs.WorldGeneration != WorldGeneration)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Observation from world generation %d (current %d)"), Obs.WorldGeneration, WorldGeneration), DocQuestTags::Error_Quest_WrongWorld);
	}

	RebuildIndexIfDirty();
	TArray<FQuestKey> Candidates;
	if (const TMap<FGameplayTag, TArray<FQuestKey>>* OwnerIndex = EventIndex.Find(Obs.Owner))
	{
		const FGameplayTagContainer Parents = Obs.EventTag.GetGameplayTagParents();
		for (const FGameplayTag& Tag : Parents)
		{
			if (const TArray<FQuestKey>* Keys = OwnerIndex->Find(Tag))
			{
				for (const FQuestKey& K : *Keys) { Candidates.AddUnique(K); }
			}
		}
	}
	if (Candidates.IsEmpty())
	{
		return FDocSystemResult::MakeNoChange(TEXT("No active objective or rule listens for this event"));
	}
	Candidates.Sort([this](const FQuestKey& A, const FQuestKey& B)
	{
		const UDocQuestDefinition* DA = FindDefinition(A.QuestId);
		const UDocQuestDefinition* DB = FindDefinition(B.QuestId);
		const int32 PA = DA ? DA->TransitionPriority : 0;
		const int32 PB = DB ? DB->TransitionPriority : 0;
		return PA != PB ? PA > PB : A.QuestId.LexicalLess(B.QuestId);
	});

	int32 Advanced = 0;
	TArray<FString> Ignored;
	for (const FQuestKey& Key : Candidates)
	{
		FDocQuestOwnerRecord* R = FindRecord(Key);
		const UDocQuestDefinition* Def = FindDefinition(Key.QuestId);
		if (R && Def)
		{
			Advanced += ProcessForQuest(*R, *Def, Obs, Ignored);
		}
	}
	if (Advanced == 0)
	{
		return FDocSystemResult::MakeNoChange(FString::Join(Ignored, TEXT("; ")));
	}
	FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	Result.Diagnostic = FString::Printf(TEXT("%d change(s)%s%s"), Advanced, Ignored.IsEmpty() ? TEXT("") : TEXT("; ignored: "), *FString::Join(Ignored, TEXT("; ")));
	return Result;
}

FDocSystemResult UDocObjectiveSubsystem::RefreshCurrentState(const FDocOwnerScope& Owner)
{
	FApiScope Scope(*this);
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Quest state changes on the authority"));
	}
	int32 Changed = 0;
	TArray<FQuestKey> Keys;
	for (const TPair<FQuestKey, FDocQuestOwnerRecord>& Pair : Records)
	{
		if (Pair.Key.Owner == Owner && Pair.Value.Current.State == EDocQuestState::Active && !Pair.Value.bQuarantined)
		{
			Keys.Add(Pair.Key);
		}
	}
	for (const FQuestKey& Key : Keys)
	{
		FDocQuestOwnerRecord* R = FindRecord(Key);
		const UDocQuestDefinition* Def = FindDefinition(Key.QuestId);
		if (!R || !Def)
		{
			continue;
		}
		const int64 Before = R->Current.Revision;
		for (int32 i = 0; i < R->Current.Objectives.Num(); ++i)
		{
			if (const FDocObjectiveDefinition* ODef = FindObjectiveDef(*Def, R->Current.StageId, R->Current.Objectives[i].ObjectiveId))
			{
				RefreshObjectiveState(*R, *Def, *ODef, i, TEXT("State refresh"));
			}
		}
		EvaluateQuest(*R, *Def, TEXT("State refresh"), {});
		Changed += R->Current.Revision != Before ? 1 : 0;
	}
	return Changed > 0 ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("No objective changed"));
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------

void UDocObjectiveSubsystem::AdvanceClock(EDocClockDomain Domain, double Seconds)
{
	if (Seconds <= 0.0)
	{
		return;
	}
	FApiScope Scope(*this);
	AdvanceTimers(Domain, Seconds, nullptr, false);
}

void UDocObjectiveSubsystem::ApplyOfflineElapsed(const FDocOwnerScope& Owner, double Seconds)
{
	if (Seconds <= 0.0)
	{
		return;
	}
	FApiScope Scope(*this);
	for (EDocClockDomain Domain : { EDocClockDomain::Simulation, EDocClockDomain::WorldGameplay, EDocClockDomain::RealTime, EDocClockDomain::WallClock })
	{
		AdvanceTimers(Domain, Seconds, &Owner, true);
	}
}

void UDocObjectiveSubsystem::AdvanceTimers(EDocClockDomain Domain, double Seconds, const FDocOwnerScope* OnlyOwner, bool bOfflineOnly)
{
	if (!HasAuthority())
	{
		return;
	}
	RebuildIndexIfDirty();
	const TArray<FTimerRef> Refs = Timers; // only active timers are indexed
	for (const FTimerRef& Ref : Refs)
	{
		if (Ref.Clock != Domain || (OnlyOwner && Ref.Key.Owner != *OnlyOwner))
		{
			continue;
		}
		FDocQuestOwnerRecord* R = FindRecord(Ref.Key);
		const UDocQuestDefinition* Def = FindDefinition(Ref.Key.QuestId);
		if (!R || !Def || R->Current.State != EDocQuestState::Active)
		{
			continue;
		}
		const int32 Index = R->Current.Objectives.IndexOfByPredicate([&Ref](const FDocObjectiveRuntimeState& O) { return O.ObjectiveInstanceId == Ref.ObjectiveInstanceId; });
		if (Index == INDEX_NONE || R->Current.Objectives[Index].State != EDocObjectiveState::Active)
		{
			continue;
		}
		const FDocObjectiveDefinition* ODef = FindObjectiveDef(*Def, R->Current.StageId, R->Current.Objectives[Index].ObjectiveId);
		if (!ODef || (bOfflineOnly && !ODef->bAllowOfflineProgress))
		{
			continue;
		}
		FDocObjectiveRuntimeState& O = R->Current.Objectives[Index];
		bool bChanged = false;
		if (O.WaitRemaining >= 0.0)
		{
			O.WaitRemaining = FMath::Max(0.0, O.WaitRemaining - Seconds);
			if (O.WaitRemaining <= 0.0)
			{
				CompleteObjective(*R, *Def, *ODef, Index, TEXT("Wait elapsed"));
				bChanged = true;
			}
		}
		if (!bChanged && O.LimitRemaining >= 0.0)
		{
			O.LimitRemaining = FMath::Max(0.0, O.LimitRemaining - Seconds);
			if (O.LimitRemaining <= 0.0)
			{
				FailObjective(*R, *Def, *ODef, Index, TEXT("Time limit expired"));
				bChanged = true;
			}
		}
		if (bChanged)
		{
			EvaluateQuest(*R, *Def, TEXT("Timer"), {});
		}
	}
}

// ---------------------------------------------------------------------------
// Delivery
// ---------------------------------------------------------------------------

IDocQuestEffectProvider* UDocObjectiveSubsystem::FindEffectProvider(const FDocQuestAction& Action) const
{
	FName ProviderId = Action.ProviderId;
	if (ProviderId.IsNone())
	{
		ProviderId = Action.Type == EDocQuestActionType::BroadcastEvent ? Settings()->EventProviderId
			: Action.Type == EDocQuestActionType::GrantTag ? Settings()->TagProviderId : NAME_None;
	}
	const TSharedPtr<IDocQuestEffectProvider>* Provider = EffectProviders.Find(ProviderId);
	return Provider ? Provider->Get() : nullptr;
}

FDocSystemResult UDocObjectiveSubsystem::DeliverIntent(const FQuestKey& Key, const FDocEffectKey& EffectKey)
{
	FDocQuestOwnerRecord* R = FindRecord(Key);
	FDocQuestRewardIntent* Intent = R ? R->Current.Intents.FindByPredicate([&EffectKey](const FDocQuestRewardIntent& I) { return I.Key == EffectKey; }) : nullptr;
	if (!Intent || Intent->State != EDocRewardIntentState::Pending)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Nothing to deliver"));
	}
	// Copy the request: the provider may call back into this subsystem.
	const FDocQuestAction Action = Intent->Action;
	FDocQuestEffectRequest Request;
	Request.Action = &Action;
	Request.Key = EffectKey;
	Request.Owner = Key.Owner;
	Request.QuestId = Key.QuestId;
	Request.QuestInstanceId = R->Current.QuestInstanceId;
	Request.Source = Intent->Source;

	FDocSystemResult Result;
	if (IDocQuestEffectProvider* Provider = FindEffectProvider(Action))
	{
		Result = Provider->DeliverEffect(Request);
	}
	else
	{
		Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable,
			FString::Printf(TEXT("No effect provider for action %s"), *Action.ActionId.ToString()), DocQuestTags::Error_Quest_ProviderMissing);
	}

	R = FindRecord(Key);
	Intent = R ? R->Current.Intents.FindByPredicate([&EffectKey](const FDocQuestRewardIntent& I) { return I.Key == EffectKey; }) : nullptr;
	if (!Intent)
	{
		return Result;
	}
	++Intent->Attempts;
	Intent->LastResult = Result;
	Intent->State = Result.IsSuccess() ? EDocRewardIntentState::Delivered
		: (IsRetryable(Result.Outcome) ? EDocRewardIntentState::Pending : EDocRewardIntentState::Failed);
	const bool bWasPending = R->Current.bRewardDeliveryPending;
	RecomputeRewardPending(R->Current);
	if (Intent->Source == TEXT("Reward") || bWasPending != R->Current.bRewardDeliveryPending)
	{
		QueueRewardChange(*R);
	}
	return Result;
}

FDocSystemResult UDocObjectiveSubsystem::RetryRewardDelivery(const FDocOwnerScope& Owner, FName QuestId)
{
	FApiScope Scope(*this);
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Rewards are delivered on the authority"));
	}
	const FQuestKey Key{ Owner, QuestId };
	const FDocQuestOwnerRecord* R = FindRecord(Key);
	if (!R)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No such quest record"));
	}
	TArray<FDocEffectKey> Pending;
	for (const FDocQuestRewardIntent& I : R->Current.Intents)
	{
		if (I.State == EDocRewardIntentState::Pending) { Pending.Add(I.Key); }
	}
	if (Pending.IsEmpty())
	{
		return FDocSystemResult::MakeNoChange(TEXT("No pending intents"));
	}
	TArray<FString> Failures;
	for (const FDocEffectKey& EffectKey : Pending)
	{
		const FDocSystemResult Result = DeliverIntent(Key, EffectKey);
		if (!Result.IsSuccess())
		{
			Failures.Add(FString::Printf(TEXT("%s: %s"), *EffectKey.ActionId.ToString(), *Result.Diagnostic));
		}
	}
	if (!Failures.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, FString::Join(Failures, TEXT("; ")), DocQuestTags::Error_Quest_RewardPending);
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Queries / tracking
// ---------------------------------------------------------------------------

bool UDocObjectiveSubsystem::GetQuestSnapshot(const FDocOwnerScope& Owner, FName QuestId, FDocQuestOwnerRecord& OutRecord) const
{
	if (const FDocQuestOwnerRecord* R = Records.Find(FQuestKey{ Owner, QuestId }))
	{
		OutRecord = *R;
		return true;
	}
	return false;
}

TArray<FName> UDocObjectiveSubsystem::GetActiveQuests(const FDocOwnerScope& Owner) const
{
	TArray<FName> Out;
	for (const TPair<FQuestKey, FDocQuestOwnerRecord>& Pair : Records)
	{
		if (Pair.Key.Owner == Owner && Pair.Value.Current.State == EDocQuestState::Active && !Pair.Value.bQuarantined)
		{
			Out.Add(Pair.Key.QuestId);
		}
	}
	Out.Sort([](FName A, FName B) { return A.LexicalLess(B); });
	return Out;
}

FDocSystemResult UDocObjectiveSubsystem::SetTrackedQuest(const FDocOwnerScope& Viewer, const FDocOwnerScope& ProgressOwner, FName QuestId)
{
	FApiScope Scope(*this);
	if (!Viewer.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Viewer scope required"));
	}
	FDocQuestTrackingState& T = Tracking.FindOrAdd(Viewer);
	T.Viewer = Viewer;
	if (QuestId.IsNone())
	{
		if (!T.TrackedQuest.IsSet())
		{
			return FDocSystemResult::MakeNoChange(TEXT("Nothing tracked"));
		}
		T.TrackedQuest = FDocTrackedQuestRef(); // untracking never cancels the quest
		QueueTrackingChange(Viewer);
		return FDocSystemResult::MakeSuccess();
	}
	const FDocQuestOwnerRecord* R = Records.Find(FQuestKey{ ProgressOwner, QuestId });
	const UDocQuestDefinition* Def = FindDefinition(QuestId);
	if (!R || !Def || R->Current.State != EDocQuestState::Active)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No active quest to track"));
	}
	if (!Def->bTrackable)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Quest is not trackable"), DocQuestTags::Error_Quest_NotTrackable);
	}
	FDocTrackedQuestRef Ref;
	Ref.Owner = ProgressOwner;
	Ref.QuestId = QuestId;
	if (T.TrackedQuest == Ref)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already tracked"));
	}
	T.TrackedQuest = Ref;
	QueueTrackingChange(Viewer);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocObjectiveSubsystem::SetTrackedObjective(const FDocOwnerScope& Viewer, const FDocOwnerScope& ProgressOwner, FName QuestId, FName ObjectiveId, bool bTracked)
{
	FApiScope Scope(*this);
	if (!Viewer.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Viewer scope required"));
	}
	FDocTrackedQuestRef Ref;
	Ref.Owner = ProgressOwner;
	Ref.QuestId = QuestId;
	Ref.ObjectiveId = ObjectiveId;
	FDocQuestTrackingState& T = Tracking.FindOrAdd(Viewer);
	T.Viewer = Viewer;
	if (!bTracked)
	{
		return T.TrackedObjectives.Remove(Ref) > 0 ? (QueueTrackingChange(Viewer), FDocSystemResult::MakeSuccess()) : FDocSystemResult::MakeNoChange(TEXT("Not tracked"));
	}
	const FDocQuestOwnerRecord* R = Records.Find(FQuestKey{ ProgressOwner, QuestId });
	const UDocQuestDefinition* Def = FindDefinition(QuestId);
	const FDocObjectiveRuntimeState* O = R ? R->Current.FindObjective(ObjectiveId) : nullptr;
	const FDocObjectiveDefinition* ODef = (R && Def) ? FindObjectiveDef(*Def, R->Current.StageId, ObjectiveId) : nullptr;
	if (!O || !ODef || O->State != EDocObjectiveState::Active)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No active objective to track"));
	}
	if (ODef->TrackingMode == EDocObjectiveTrackingMode::NotTrackable || !Def->bTrackable)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Objective is not trackable"), DocQuestTags::Error_Quest_NotTrackable);
	}
	if (T.TrackedObjectives.Contains(Ref))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already tracked"));
	}
	T.TrackedObjectives.Add(Ref);
	QueueTrackingChange(Viewer);
	return FDocSystemResult::MakeSuccess();
}

FDocTrackedQuestRef UDocObjectiveSubsystem::GetTrackedQuest(const FDocOwnerScope& Viewer) const
{
	const FDocQuestTrackingState* T = Tracking.Find(Viewer);
	return T ? T->TrackedQuest : FDocTrackedQuestRef();
}

TArray<FDocTrackedQuestRef> UDocObjectiveSubsystem::GetTrackedObjectives(const FDocOwnerScope& Viewer) const
{
	TArray<FDocTrackedQuestRef> Out;
	const FDocQuestTrackingState* T = Tracking.Find(Viewer);
	if (!T)
	{
		return Out;
	}
	auto IsActive = [this](const FDocTrackedQuestRef& Ref)
	{
		const FDocQuestOwnerRecord* R = Records.Find(FQuestKey{ Ref.Owner, Ref.QuestId });
		const FDocObjectiveRuntimeState* O = R ? R->Current.FindObjective(Ref.ObjectiveId) : nullptr;
		return O && O->State == EDocObjectiveState::Active;
	};
	for (const FDocTrackedQuestRef& Ref : T->TrackedObjectives)
	{
		if (IsActive(Ref)) { Out.AddUnique(Ref); }
	}
	if (T->TrackedQuest.IsSet())
	{
		const FDocQuestOwnerRecord* R = Records.Find(FQuestKey{ T->TrackedQuest.Owner, T->TrackedQuest.QuestId });
		const UDocQuestDefinition* Def = FindDefinition(T->TrackedQuest.QuestId);
		if (R && Def && R->Current.State == EDocQuestState::Active)
		{
			for (const FDocObjectiveRuntimeState& O : R->Current.Objectives)
			{
				const FDocObjectiveDefinition* ODef = FindObjectiveDef(*Def, R->Current.StageId, O.ObjectiveId);
				if (ODef && O.State == EDocObjectiveState::Active && !O.bHidden && ODef->TrackingMode == EDocObjectiveTrackingMode::AutoWhenActive)
				{
					FDocTrackedQuestRef Ref = T->TrackedQuest;
					Ref.ObjectiveId = O.ObjectiveId;
					Out.AddUnique(Ref);
				}
			}
		}
	}
	return Out;
}

void UDocObjectiveSubsystem::DropTrackingFor(const FDocOwnerScope& Owner, FName QuestId)
{
	for (TPair<FDocOwnerScope, FDocQuestTrackingState>& Pair : Tracking)
	{
		bool bChanged = false;
		if (Pair.Value.TrackedQuest.Owner == Owner && Pair.Value.TrackedQuest.QuestId == QuestId)
		{
			Pair.Value.TrackedQuest = FDocTrackedQuestRef();
			bChanged = true;
		}
		bChanged |= Pair.Value.TrackedObjectives.RemoveAll([&](const FDocTrackedQuestRef& R) { return R.Owner == Owner && R.QuestId == QuestId; }) > 0;
		if (bChanged)
		{
			QueueTrackingChange(Pair.Key);
		}
	}
}

// ---------------------------------------------------------------------------
// Index and auto-activation
// ---------------------------------------------------------------------------

void UDocObjectiveSubsystem::RebuildIndexIfDirty()
{
	if (!bIndexDirty)
	{
		return;
	}
	bIndexDirty = false;
	EventIndex.Reset();
	Timers.Reset();
	for (const TPair<FQuestKey, FDocQuestOwnerRecord>& Pair : Records)
	{
		const FDocQuestOwnerRecord& R = Pair.Value;
		const UDocQuestDefinition* Def = FindDefinition(Pair.Key.QuestId);
		if (!Def || R.bQuarantined || R.Current.State != EDocQuestState::Active)
		{
			continue; // completed work is unsubscribed
		}
		TMap<FGameplayTag, TArray<FQuestKey>>& OwnerIndex = EventIndex.FindOrAdd(Pair.Key.Owner);
		for (const FDocObjectiveRuntimeState& O : R.Current.Objectives)
		{
			if (O.State != EDocObjectiveState::Active || O.bQuarantined)
			{
				continue;
			}
			const FDocObjectiveDefinition* ODef = FindObjectiveDef(*Def, R.Current.StageId, O.ObjectiveId);
			if (!ODef)
			{
				continue;
			}
			if (UsesObservations(*ODef) && ODef->EventTag.IsValid())
			{
				OwnerIndex.FindOrAdd(ODef->EventTag).AddUnique(Pair.Key);
			}
			if (O.WaitRemaining >= 0.0 || O.LimitRemaining >= 0.0)
			{
				Timers.Add(FTimerRef{ Pair.Key, O.ObjectiveInstanceId, Def->TimerClock });
			}
		}
		for (const FDocQuestFailureRule& Rule : Def->FailureRules)
		{
			if (Rule.EventTag.IsValid() && (Rule.StageId.IsNone() || Rule.StageId == R.Current.StageId))
			{
				OwnerIndex.FindOrAdd(Rule.EventTag).AddUnique(Pair.Key);
			}
		}
	}
}

int32 UDocObjectiveSubsystem::GetIndexedTagCountForTesting(const FDocOwnerScope& Owner) const
{
	const_cast<UDocObjectiveSubsystem*>(this)->RebuildIndexIfDirty();
	const TMap<FGameplayTag, TArray<FQuestKey>>* OwnerIndex = EventIndex.Find(Owner);
	return OwnerIndex ? OwnerIndex->Num() : 0;
}

int32 UDocObjectiveSubsystem::GetActiveTimerCountForTesting() const
{
	const_cast<UDocObjectiveSubsystem*>(this)->RebuildIndexIfDirty();
	return Timers.Num();
}

void UDocObjectiveSubsystem::RunAutoActivation(const FDocOwnerScope& Owner)
{
	const int32 MaxPasses = FMath::Max(1, Settings()->MaxAutoActivationPasses);
	TArray<FName> Ids;
	Definitions.GetKeys(Ids);
	Ids.Sort([](FName A, FName B) { return A.LexicalLess(B); });
	for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
	{
		bool bAny = false;
		for (FName Id : Ids)
		{
			const UDocQuestDefinition* Def = FindDefinition(Id);
			if (!Def || !Def->bAutoActivate || Records.Contains(FQuestKey{ Owner, Id }) || !CheckOwnerPolicy(*Def, Owner).IsSuccess())
			{
				continue; // automatic activation covers first activation only
			}
			if (EvaluateConditions(Owner, Id, Def->Prerequisites).IsSatisfied())
			{
				FGuid Ignored;
				bAny |= ActivateInternal(Owner, *Def, false, Ignored).IsSuccess();
			}
		}
		if (!bAny)
		{
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

FDocQuestSaveData UDocObjectiveSubsystem::CaptureState(bool bIncludeTracking) const
{
	FDocQuestSaveData Data;
	for (const TPair<FQuestKey, FDocQuestOwnerRecord>& Pair : Records)
	{
		if (Pair.Key.Owner.IsPersistable())
		{
			Data.Records.Add(Pair.Value); // quarantined records are preserved, not dropped
		}
	}
	Data.Records.Sort([](const FDocQuestOwnerRecord& A, const FDocQuestOwnerRecord& B)
	{
		const FString OA = A.Owner.ToString();
		const FString OB = B.Owner.ToString();
		return OA != OB ? OA < OB : A.QuestId.LexicalLess(B.QuestId);
	});
	if (bIncludeTracking)
	{
		for (const TPair<FDocOwnerScope, FDocQuestTrackingState>& Pair : Tracking)
		{
			if (Pair.Key.IsPersistable())
			{
				Data.Tracking.Add(Pair.Value);
			}
		}
	}
	return Data;
}

FDocSystemResult UDocObjectiveSubsystem::RestoreState(const FDocQuestSaveData& Data, bool bRestoreTracking)
{
	FApiScope Scope(*this);
	// 1. Validate the envelope and owners.
	if (Data.SchemaVersion <= 0 || Data.SchemaVersion > FDocQuestSaveData::CurrentSchemaVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Unsupported quest schema %d"), Data.SchemaVersion));
	}
	TSet<FQuestKey> Seen;
	for (const FDocQuestOwnerRecord& Rec : Data.Records)
	{
		if (Rec.QuestId.IsNone() || !Rec.Owner.IsPersistable() || Rec.Current.Owner != Rec.Owner)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Malformed record for %s"), *Rec.QuestId.ToString()));
		}
		bool bDup = false;
		Seen.Add(FQuestKey{ Rec.Owner, Rec.QuestId }, &bDup);
		if (bDup)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Duplicate record for %s"), *Rec.QuestId.ToString()));
		}
		if (Rec.Current.State != EDocQuestState::Inactive && !Rec.Current.QuestInstanceId.IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Record %s has no instance id"), *Rec.QuestId.ToString()));
		}
	}

	// 2-3. Migrate and stage detached records.
	bRestoring = true;
	TMap<FQuestKey, FDocQuestOwnerRecord> Staged;
	int32 Quarantined = 0;
	for (const FDocQuestOwnerRecord& Rec : Data.Records)
	{
		FDocQuestOwnerRecord R = Rec;
		R.bQuarantined = false;
		R.QuarantineReason.Reset();
		const UDocQuestDefinition* Def = FindDefinition(R.QuestId);
		auto Quarantine = [&R, &Quarantined](const FString& Why)
		{
			R.bQuarantined = true;
			R.QuarantineReason = Why;
			++Quarantined;
		};
		if (!Def)
		{
			Quarantine(FString::Printf(TEXT("Definition %s is not registered"), *R.QuestId.ToString()));
		}
		else if (R.Current.State == EDocQuestState::Active)
		{
			FName StageId = R.Current.StageId;
			if (!Def->FindStage(StageId))
			{
				if (const FName* Redirect = Def->StageRedirects.Find(StageId)) { StageId = *Redirect; }
			}
			const FDocQuestStage* Stage = Def->FindStage(StageId);
			if (!Stage)
			{
				// A missing critical stage never silently completes the quest.
				Quarantine(FString::Printf(TEXT("Stage %s no longer exists (content v%d, saved v%d)"), *R.Current.StageId.ToString(), Def->ContentVersion, R.Current.ContentVersion));
			}
			else
			{
				R.Current.StageId = StageId;
				for (FDocObjectiveRuntimeState& O : R.Current.Objectives)
				{
					FName ObjectiveId = O.ObjectiveId;
					if (!Stage->FindObjective(ObjectiveId))
					{
						if (const FName* Redirect = Def->ObjectiveRedirects.Find(ObjectiveId)) { ObjectiveId = *Redirect; }
					}
					const FDocObjectiveDefinition* ODef = Stage->FindObjective(ObjectiveId);
					if (!ODef)
					{
						if (O.bOptional)
						{
							O.bQuarantined = true; // optional work may be quarantined
							continue;
						}
						Quarantine(FString::Printf(TEXT("Required objective %s no longer exists"), *O.ObjectiveId.ToString()));
						break;
					}
					O.ObjectiveId = ObjectiveId;
					O.bOptional = ODef->bOptional;
					O.TargetCount = FMath::Max(1, ODef->TargetCount);
					O.Count = FMath::Clamp(O.Count, 0, O.TargetCount);
				}
				if (!R.bQuarantined)
				{
					for (const FDocObjectiveDefinition& ODef : Stage->Objectives)
					{
						if (!R.Current.FindObjective(ODef.ObjectiveId))
						{
							FDocObjectiveRuntimeState Added; // new content: starts inactive
							Added.ObjectiveId = ODef.ObjectiveId;
							Added.bHidden = ODef.bHidden;
							Added.bOptional = ODef.bOptional;
							Added.TargetCount = FMath::Max(1, ODef.TargetCount);
							R.Current.Objectives.Add(Added);
						}
					}
					R.Current.ContentVersion = Def->ContentVersion;
				}
			}
		}
		Staged.Add(FQuestKey{ R.Owner, R.QuestId }, MoveTemp(R));
	}

	// 4. Apply under the barrier.
	Records = MoveTemp(Staged);
	if (bRestoreTracking)
	{
		Tracking.Reset();
		for (const FDocQuestTrackingState& T : Data.Tracking)
		{
			if (T.Viewer.IsValid())
			{
				Tracking.Add(T.Viewer, T);
			}
		}
	}
	MarkIndexDirty();

	// 5. Coherent: activate newly added content in parallel stages (activation is not a replay).
	for (TPair<FQuestKey, FDocQuestOwnerRecord>& Pair : Records)
	{
		FDocQuestOwnerRecord& R = Pair.Value;
		const UDocQuestDefinition* Def = FindDefinition(R.QuestId);
		const FDocQuestStage* Stage = Def ? Def->FindStage(R.Current.StageId) : nullptr;
		if (Def && Stage && !R.bQuarantined && R.Current.State == EDocQuestState::Active && Stage->Activation == EDocStageActivation::Parallel)
		{
			ActivateEligible(R, *Def, *Stage, TEXT("Restored content"));
		}
	}
	bRestoring = false;
	RebuildIndexIfDirty();

	// 6. One refresh notification; no gameplay events, rewards or effects are replayed.
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocObjectiveSubsystem>(this)]()
	{
		if (UDocObjectiveSubsystem* This = WeakThis.Get())
		{
			This->OnStateRefreshedNative.Broadcast();
			This->OnStateRefreshed.Broadcast();
		}
	});
	for (const FDocOwnerScope& Owner : KnownOwners)
	{
		PendingAutoActivation.Add(Owner);
	}
	FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	Result.Diagnostic = FString::Printf(TEXT("%d record(s), %d quarantined"), Records.Num(), Quarantined);
	return Result;
}

// ---------------------------------------------------------------------------
// UDocObjectiveWorldFacade
// ---------------------------------------------------------------------------

bool UDocObjectiveWorldFacade::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocObjectiveWorldFacade::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UDocObjectiveSubsystem* Service = UDocObjectiveSubsystem::Get(&InWorld))
	{
		Service->AttachWorld(&InWorld);
	}
	LastRealSeconds = FPlatformTime::Seconds();
}

void UDocObjectiveWorldFacade::Deinitialize()
{
	if (UDocObjectiveSubsystem* Service = UDocObjectiveSubsystem::Get(GetWorld()))
	{
		Service->DetachWorld(GetWorld());
	}
	Super::Deinitialize();
}

void UDocObjectiveWorldFacade::Tick(float DeltaTime)
{
	UDocObjectiveSubsystem* Service = UDocObjectiveSubsystem::Get(GetWorld());
	if (!Service || !Service->IsAttachedTo(GetWorld()))
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const double RealDelta = LastRealSeconds >= 0.0 ? Now - LastRealSeconds : 0.0;
	LastRealSeconds = Now;
	Service->AdvanceClock(EDocClockDomain::WorldGameplay, DeltaTime);
	Service->AdvanceClock(EDocClockDomain::RealTime, RealDelta);
	if (GetDefault<UDocQuestSettings>()->bDriveSimulationFromWorldTime)
	{
		Service->AdvanceClock(EDocClockDomain::Simulation, DeltaTime);
	}
}

TStatId UDocObjectiveWorldFacade::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocObjectiveWorldFacade, STATGROUP_Tickables);
}
