#include "DocReactiveMaterialComponent.h"
#include "DocMaterialReactionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace DocMaterialPrivate
{
	static bool IsLiveState(EDocReactionState State)
	{
		return State == EDocReactionState::Active
			|| State == EDocReactionState::PendingDwell
			|| State == EDocReactionState::Outcompeted;
	}

	static bool FactsDiffer(const FDocMaterialState& A, const FDocMaterialState& B)
	{
		return A.MaterialId != B.MaterialId
			|| A.Moisture != B.Moisture
			|| A.Fuel != B.Fuel
			|| A.MaxFuel != B.MaxFuel
			|| A.PhaseFraction != B.PhaseFraction
			|| A.CharAmount != B.CharAmount
			|| A.StateTags != B.StateTags;
	}
}

UDocReactiveMaterialComponent::UDocReactiveMaterialComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocReactiveMaterialComponent::OnRegister()
{
	Super::OnRegister();

	if (MaterialProfile && CurrentState.MaterialId.IsNone())
	{
		InitializeFromProfile(MaterialProfile);
	}
	RegisterWithSubsystem();
}

void UDocReactiveMaterialComponent::OnUnregister()
{
	// Live exposure handles belong to their sources and do not survive teardown; material facts stay.
	ClearLiveExposure();
	UnregisterFromSubsystem();
	Super::OnUnregister();
}

void UDocReactiveMaterialComponent::BeginPlay()
{
	Super::BeginPlay();

	if (MaterialProfile && CurrentState.MaterialId.IsNone())
	{
		InitializeFromProfile(MaterialProfile);
	}
	RegisterWithSubsystem();
}

void UDocReactiveMaterialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearLiveExposure();
	UnregisterFromSubsystem();
	Super::EndPlay(EndPlayReason);
}

void UDocReactiveMaterialComponent::RegisterWithSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocMaterialReactionSubsystem* Subsystem = World->GetSubsystem<UDocMaterialReactionSubsystem>())
		{
			Subsystem->RegisterReactiveComponent(this);
		}
	}
}

void UDocReactiveMaterialComponent::UnregisterFromSubsystem()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocMaterialReactionSubsystem* Subsystem = World->GetSubsystem<UDocMaterialReactionSubsystem>())
		{
			Subsystem->UnregisterReactiveComponent(this);
		}
	}
}

bool UDocReactiveMaterialComponent::Reject(FName Reason)
{
	LastRejectReason = Reason;
	return false;
}

bool UDocReactiveMaterialComponent::CanMutate()
{
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		return Reject(TEXT("NoAuthority"));
	}
	if (BroadcastDepth > MaxNestedTransitionDepth)
	{
		return Reject(TEXT("RecursionLimit"));
	}
	return true;
}

bool UDocReactiveMaterialComponent::InitializeFromProfile(UDocMaterialProfile* NewProfile)
{
	if (!NewProfile)
	{
		return Reject(TEXT("NoProfile"));
	}

	FString Error;
	if (!NewProfile->ValidateProfile(Error))
	{
		return Reject(TEXT("InvalidProfile"));
	}

	MaterialProfile = NewProfile;
	const int64 PriorRevision = CurrentState.Revision;
	CurrentState = FDocMaterialState();
	CurrentState.MaterialId = NewProfile->MaterialId;
	CurrentState.Fuel = NewProfile->InitialFuel;
	CurrentState.MaxFuel = NewProfile->MaxFuel;
	CurrentState.Moisture = NewProfile->InitialMoisture;
	CurrentState.PhaseFraction = NewProfile->InitialPhaseFraction;
	CurrentState.CharAmount = 0.0f;
	CurrentState.StateTags = NewProfile->BaseMaterialTags;
	CurrentState.Revision = PriorRevision + 1;

	ActiveReactions.Empty();
	ExposureSources.Empty();
	SuppressionSources.Empty();
	LastRejectReason = NAME_None;

	++BroadcastDepth;
	OnMaterialStateChanged.Broadcast(CurrentState, TEXT("Initialized"));
	OnMaterialStateChangedNative.Broadcast(CurrentState, TEXT("Initialized"));
	--BroadcastDepth;

	return true;
}

// ---------------------------------------------------------------------------------------------
// Exposure
// ---------------------------------------------------------------------------------------------

bool UDocReactiveMaterialComponent::AddExposureSource(const FDocExposureSample& Sample)
{
	if (!IsRegistered())
	{
		return Reject(TEXT("NotRegistered"));
	}
	if (!CanMutate())
	{
		return false;
	}
	if (Sample.SourceId.IsNone() || !Sample.Channel.IsValid() || !FMath::IsFinite(Sample.Intensity)
		|| Sample.Intensity < 0.0f || Sample.Weight < 0.0f || Sample.Generation < 0)
	{
		return Reject(TEXT("InvalidSample"));
	}

	FDocExposureSample Stored = Sample;
	if (const FDocExposureSample* Existing = ExposureSources.Find(Sample.SourceId))
	{
		if (Sample.Sequence < Existing->Sequence)
		{
			return Reject(TEXT("StaleSequence"));
		}
		Stored.Revision = Existing->Revision + 1;
	}
	else
	{
		Stored.Revision = 1;
	}

	ExposureSources.Add(Sample.SourceId, Stored);
	return true;
}

bool UDocReactiveMaterialComponent::UpdateExposureSource(FName SourceId, float NewIntensity)
{
	if (!CanMutate())
	{
		return false;
	}
	if (!FMath::IsFinite(NewIntensity))
	{
		return Reject(TEXT("InvalidSample"));
	}
	if (FDocExposureSample* Existing = ExposureSources.Find(SourceId))
	{
		Existing->Intensity = FMath::Max(0.0f, NewIntensity);
		Existing->Revision++;
		return true;
	}
	return Reject(TEXT("UnknownSource"));
}

bool UDocReactiveMaterialComponent::RemoveExposureSource(FName SourceId)
{
	return ExposureSources.Remove(SourceId) > 0;
}

int32 UDocReactiveMaterialComponent::RemoveExposureSourcesWithPrefix(const FString& Prefix)
{
	TArray<FName> ToRemove;
	for (const TPair<FName, FDocExposureSample>& Kvp : ExposureSources)
	{
		if (Kvp.Key.ToString().StartsWith(Prefix))
		{
			ToRemove.Add(Kvp.Key);
		}
	}
	for (const FName& Id : ToRemove)
	{
		ExposureSources.Remove(Id);
	}
	return ToRemove.Num();
}

void UDocReactiveMaterialComponent::ClearLiveExposure()
{
	ExposureSources.Empty();
}

float UDocReactiveMaterialComponent::ComputeChannel(const FGameplayTag& Channel, int32* OutMinGeneration) const
{
	if (OutMinGeneration)
	{
		*OutMinGeneration = 0;
	}
	if (!Channel.IsValid())
	{
		return 0.0f;
	}

	EDocChannelCombinationRule Rule = EDocChannelCombinationRule::Sum;
	float Cap = 100.0f;
	if (MaterialProfile)
	{
		MaterialProfile->ResolveChannel(Channel, Rule, Cap);
	}

	float Sum = 0.0f;
	float MaxValue = 0.0f;
	float WeightedSum = 0.0f;
	float WeightTotal = 0.0f;
	int32 MinGeneration = MAX_int32;

	for (const TPair<FName, FDocExposureSample>& Kvp : ExposureSources)
	{
		const FDocExposureSample& S = Kvp.Value;
		if (!S.Channel.MatchesTag(Channel))
		{
			continue;
		}
		const float Value = FMath::Clamp(S.Intensity, 0.0f, Cap);
		Sum += Value;
		MaxValue = FMath::Max(MaxValue, Value);
		if (S.Weight > 0.0f)
		{
			WeightedSum += Value * S.Weight;
			WeightTotal += S.Weight;
		}
		if (Value > 0.0f)
		{
			MinGeneration = FMath::Min(MinGeneration, S.Generation);
		}
	}

	if (OutMinGeneration)
	{
		*OutMinGeneration = (MinGeneration == MAX_int32) ? 0 : MinGeneration;
	}

	float Result = 0.0f;
	switch (Rule)
	{
	case EDocChannelCombinationRule::Max:
		Result = MaxValue;
		break;
	case EDocChannelCombinationRule::Weighted:
		Result = WeightTotal > 0.0f ? WeightedSum / WeightTotal : 0.0f;
		break;
	case EDocChannelCombinationRule::Sum:
	default:
		Result = Sum;
		break;
	}
	return FMath::Clamp(Result, 0.0f, Cap);
}

float UDocReactiveMaterialComponent::GetAggregatedChannelIntensity(FGameplayTag Channel) const
{
	return ComputeChannel(Channel);
}

// ---------------------------------------------------------------------------------------------
// Suppression
// ---------------------------------------------------------------------------------------------

void UDocReactiveMaterialComponent::SuppressReaction(FName ReactionId, FName SourceId, float DurationSeconds)
{
	if (ReactionId.IsNone() || SourceId.IsNone() || !CanMutate())
	{
		return;
	}

	const double Expiry = DurationSeconds > 0.0f ? SimTime + DurationSeconds : -1.0;
	SuppressionSources.FindOrAdd(ReactionId).Add(SourceId, Expiry);

	FDocReactionInstance* Inst = ActiveReactions.Find(ReactionId);
	if (Inst && Inst->State == EDocReactionState::Active)
	{
		FDocMaterialState Work = CurrentState;
		FPendingEvents Events;
		Events.Cause = TEXT("Suppressed");
		const UDocMaterialReactionDefinition* Def = MaterialProfile ? MaterialProfile->FindReaction(ReactionId) : nullptr;
		Events.Ended.Add(MakeEnd(Def, *Inst, EDocReactionState::Suppressed, TEXT("Suppressed"), Work));
		Commit(Work, Events);
	}
	else if (Inst && Inst->State == EDocReactionState::PendingDwell)
	{
		Inst->State = EDocReactionState::Suppressed;
		Inst->DwellProgress = 0.0f;
		Inst->StatusReason = TEXT("Suppressed");
	}
}

void UDocReactiveMaterialComponent::UnsuppressReaction(FName ReactionId, FName SourceId)
{
	if (TMap<FName, double>* Sources = SuppressionSources.Find(ReactionId))
	{
		Sources->Remove(SourceId);
		if (Sources->Num() == 0)
		{
			SuppressionSources.Remove(ReactionId);
		}
	}
}

bool UDocReactiveMaterialComponent::IsReactionSuppressed(FName ReactionId) const
{
	const TMap<FName, double>* Sources = SuppressionSources.Find(ReactionId);
	if (!Sources)
	{
		return false;
	}
	for (const TPair<FName, double>& Kvp : *Sources)
	{
		if (Kvp.Value < 0.0 || Kvp.Value > SimTime)
		{
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------------------------

int32 UDocReactiveMaterialComponent::CountActive() const
{
	int32 Count = 0;
	for (const TPair<FName, FDocReactionInstance>& Kvp : ActiveReactions)
	{
		if (Kvp.Value.State == EDocReactionState::Active)
		{
			++Count;
		}
	}
	return Count;
}

FDocReactionTransition UDocReactiveMaterialComponent::MakeStart(const UDocMaterialReactionDefinition& Def, FDocReactionInstance& Inst, FName Cause, int32 Generation, FDocMaterialState& Work)
{
	Inst.ReactionId = Def.ReactionId;
	Inst.GroupId = Def.ExclusiveGroup;
	Inst.State = EDocReactionState::Active;
	Inst.DwellProgress = Def.DwellDuration;
	Inst.ActiveDuration = 0.0f;
	Inst.TransitionId = FGuid::NewGuid();
	Inst.ReceiptId = FGuid::NewGuid();
	Inst.Generation = Generation;
	Inst.StatusReason = Cause;
	Inst.ConsumedFuel = 0.0f;
	Inst.ConsumedMoisture = 0.0f;
	Inst.DeltaPhase = 0.0f;

	FDocReactionTransition T;
	T.TransitionId = Inst.TransitionId;
	T.ReceiptId = Inst.ReceiptId;
	T.ReactionId = Def.ReactionId;
	T.Cause = Cause;
	T.NewState = EDocReactionState::Active;
	T.Timestamp = SimTime;
	return T;
}

FDocReactionTransition UDocReactiveMaterialComponent::MakeEnd(const UDocMaterialReactionDefinition* Def, FDocReactionInstance& Inst, EDocReactionState NewState, FName Cause, FDocMaterialState& Work)
{
	FDocReactionTransition T;
	T.TransitionId = FGuid::NewGuid();
	T.ReceiptId = Inst.ReceiptId;
	T.ReactionId = Inst.ReactionId;
	T.Cause = Cause;
	T.NewState = NewState;
	T.ConsumedFuel = Inst.ConsumedFuel;
	T.ConsumedMoisture = Inst.ConsumedMoisture;
	T.DeltaPhase = Inst.DeltaPhase;
	T.Timestamp = SimTime;

	Inst.State = NewState;
	Inst.DwellProgress = 0.0f;
	Inst.StatusReason = Cause;
	return T;
}

void UDocReactiveMaterialComponent::Commit(FDocMaterialState& Work, FPendingEvents& Events)
{
	// Tags are derived: base tags plus the resulting tags of every Active reaction.
	FGameplayTagContainer Tags = MaterialProfile ? MaterialProfile->BaseMaterialTags : FGameplayTagContainer();
	if (MaterialProfile)
	{
		for (const TPair<FName, FDocReactionInstance>& Kvp : ActiveReactions)
		{
			if (Kvp.Value.State == EDocReactionState::Active)
			{
				if (const UDocMaterialReactionDefinition* Def = MaterialProfile->FindReaction(Kvp.Key))
				{
					Tags.AppendTags(Def->ResultingTags);
				}
			}
		}
	}
	Work.StateTags = Tags;

	const bool bFactsChanged = DocMaterialPrivate::FactsDiffer(Work, CurrentState);
	if (!bFactsChanged && Events.Started.Num() == 0 && Events.Ended.Num() == 0)
	{
		return;
	}

	Work.Revision = CurrentState.Revision + 1;
	Work.MaterialId = CurrentState.MaterialId;
	CurrentState = Work;

	for (FDocReactionTransition& T : Events.Ended)
	{
		T.MaterialRevision = CurrentState.Revision;
	}
	for (FDocReactionTransition& T : Events.Started)
	{
		T.MaterialRevision = CurrentState.Revision;
	}

	// State is committed; now tell listeners. A failing listener cannot roll anything back.
	++BroadcastDepth;
	for (const FDocReactionTransition& T : Events.Ended)
	{
		OnReactionEnded.Broadcast(T.ReactionId, T);
		OnReactionEndedNative.Broadcast(T.ReactionId, T);
	}
	for (const FDocReactionTransition& T : Events.Started)
	{
		OnReactionStarted.Broadcast(T.ReactionId, T);
		OnReactionStartedNative.Broadcast(T.ReactionId, T);
	}
	const FDocMaterialState Committed = CurrentState;
	OnMaterialStateChanged.Broadcast(Committed, Events.Cause);
	OnMaterialStateChangedNative.Broadcast(Committed, Events.Cause);
	--BroadcastDepth;
}

bool UDocReactiveMaterialComponent::RequestManualReaction(FName ReactionId, FName Cause)
{
	if (!CanMutate())
	{
		return false;
	}
	if (!MaterialProfile)
	{
		return Reject(TEXT("NoProfile"));
	}

	const UDocMaterialReactionDefinition* Def = MaterialProfile->FindReaction(ReactionId);
	if (!Def)
	{
		return Reject(TEXT("UnknownReaction"));
	}
	if (IsReactionSuppressed(ReactionId))
	{
		return Reject(TEXT("Suppressed"));
	}
	if (Def->bRequiresFuel && CurrentState.Fuel <= 0.0f)
	{
		return Reject(TEXT("FuelDepleted"));
	}
	if (Def->bRequiresMoisture && CurrentState.Moisture <= 0.0f)
	{
		return Reject(TEXT("MoistureDepleted"));
	}
	if (CurrentState.Moisture >= Def->InhibitAtMoisture)
	{
		return Reject(TEXT("MoistureInhibit"));
	}

	FDocReactionInstance* Existing = ActiveReactions.Find(ReactionId);
	if (Existing && Existing->State == EDocReactionState::Active)
	{
		LastRejectReason = NAME_None;
		return true; // already satisfied; no transition
	}

	// Exclusive group: an equal-or-higher priority active reaction keeps the resource.
	TArray<FName> Displaced;
	if (!Def->ExclusiveGroup.IsNone())
	{
		TArray<FName> Keys;
		ActiveReactions.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		for (const FName& Key : Keys)
		{
			const FDocReactionInstance& Other = ActiveReactions[Key];
			if (Key == ReactionId || Other.State != EDocReactionState::Active || Other.GroupId != Def->ExclusiveGroup)
			{
				continue;
			}
			const UDocMaterialReactionDefinition* OtherDef = MaterialProfile->FindReaction(Key);
			if (!OtherDef || OtherDef->Priority >= Def->Priority)
			{
				return Reject(TEXT("GroupOccupied"));
			}
			Displaced.Add(Key);
		}
	}

	if (CountActive() - Displaced.Num() >= MaxActiveReactions)
	{
		return Reject(TEXT("ActiveReactionLimit"));
	}

	const FName EffectiveCause = Cause.IsNone() ? FName(TEXT("ManualRequest")) : Cause;
	FDocMaterialState Work = CurrentState;
	FPendingEvents Events;
	Events.Cause = EffectiveCause;

	for (const FName& Key : Displaced)
	{
		FDocReactionInstance& Other = ActiveReactions[Key];
		Events.Ended.Add(MakeEnd(MaterialProfile->FindReaction(Key), Other, EDocReactionState::Outcompeted, TEXT("Outcompeted"), Work));
	}

	FDocReactionInstance& Inst = ActiveReactions.FindOrAdd(ReactionId);
	Events.Started.Add(MakeStart(*Def, Inst, EffectiveCause, 0, Work));
	LastRejectReason = NAME_None;
	Commit(Work, Events);
	return true;
}

void UDocReactiveMaterialComponent::ExtinguishReactions(FName Cause)
{
	if (!CanMutate())
	{
		return;
	}

	const FName EffectiveCause = Cause.IsNone() ? FName(TEXT("Extinguish")) : Cause;
	FDocMaterialState Work = CurrentState;
	FPendingEvents Events;
	Events.Cause = EffectiveCause;

	TArray<FName> Keys;
	ActiveReactions.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());
	for (const FName& Key : Keys)
	{
		FDocReactionInstance& Inst = ActiveReactions[Key];
		if (Inst.State == EDocReactionState::Active)
		{
			const UDocMaterialReactionDefinition* Def = MaterialProfile ? MaterialProfile->FindReaction(Key) : nullptr;
			Events.Ended.Add(MakeEnd(Def, Inst, EDocReactionState::Extinguished, EffectiveCause, Work));
		}
		else if (Inst.State == EDocReactionState::PendingDwell || Inst.State == EDocReactionState::Outcompeted)
		{
			Inst.State = EDocReactionState::Inactive;
			Inst.DwellProgress = 0.0f;
			Inst.StatusReason = EffectiveCause;
		}
	}

	Commit(Work, Events);
}

bool UDocReactiveMaterialComponent::IsReactionActive(FName ReactionId) const
{
	const FDocReactionInstance* Inst = ActiveReactions.Find(ReactionId);
	return Inst && Inst->State == EDocReactionState::Active;
}

TArray<FDocReactionInstance> UDocReactiveMaterialComponent::GetActiveReactions() const
{
	TArray<FDocReactionInstance> Result;
	for (const TPair<FName, FDocReactionInstance>& Kvp : ActiveReactions)
	{
		if (DocMaterialPrivate::IsLiveState(Kvp.Value.State))
		{
			Result.Add(Kvp.Value);
		}
	}
	Result.Sort([](const FDocReactionInstance& A, const FDocReactionInstance& B) { return A.ReactionId.LexicalLess(B.ReactionId); });
	return Result;
}

bool UDocReactiveMaterialComponent::GetReactionInstance(FName ReactionId, FDocReactionInstance& OutInstance) const
{
	if (const FDocReactionInstance* Inst = ActiveReactions.Find(ReactionId))
	{
		OutInstance = *Inst;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Step
// ---------------------------------------------------------------------------------------------

void UDocReactiveMaterialComponent::StepSimulation(float DeltaTime, double CurrentTime)
{
	if (DeltaTime <= 0.0f || !MaterialProfile)
	{
		return;
	}
	if (BroadcastDepth > 0)
	{
		Reject(TEXT("RecursionLimit"));
		return; // a listener may not advance the simulation re-entrantly
	}

	SimTime = FMath::Max(SimTime, CurrentTime);

	// 1. Expire exposure and suppression on the simulation clock.
	{
		TArray<FName> Expired;
		for (const TPair<FName, FDocExposureSample>& Kvp : ExposureSources)
		{
			if (Kvp.Value.ExpiryTimestamp > 0.0 && Kvp.Value.ExpiryTimestamp <= SimTime)
			{
				Expired.Add(Kvp.Key);
			}
		}
		for (const FName& Id : Expired)
		{
			ExposureSources.Remove(Id);
		}

		TArray<FName> EmptyReactions;
		for (TPair<FName, TMap<FName, double>>& ReactionKvp : SuppressionSources)
		{
			TArray<FName> ExpiredSources;
			for (const TPair<FName, double>& Kvp : ReactionKvp.Value)
			{
				if (Kvp.Value >= 0.0 && Kvp.Value <= SimTime)
				{
					ExpiredSources.Add(Kvp.Key);
				}
			}
			for (const FName& Id : ExpiredSources)
			{
				ReactionKvp.Value.Remove(Id);
			}
			if (ReactionKvp.Value.Num() == 0)
			{
				EmptyReactions.Add(ReactionKvp.Key);
			}
		}
		for (const FName& Id : EmptyReactions)
		{
			SuppressionSources.Remove(Id);
		}
	}

	// 2. Snapshot channel values at the step boundary. Changes made by listeners apply next step.
	const TArray<const UDocMaterialReactionDefinition*> Order = MaterialProfile->GetEvaluationOrder();
	TMap<FGameplayTag, TPair<float, int32>> ChannelSnapshot;
	for (const UDocMaterialReactionDefinition* Def : Order)
	{
		if (!ChannelSnapshot.Contains(Def->ExposureChannel))
		{
			int32 Generation = 0;
			const float Value = ComputeChannel(Def->ExposureChannel, &Generation);
			ChannelSnapshot.Add(Def->ExposureChannel, TPair<float, int32>(Value, Generation));
		}
	}

	FDocMaterialState Work = CurrentState;
	FPendingEvents Events;
	Events.Cause = TEXT("SimulationStep");
	TSet<FName> ClaimedGroups;
	int32 KeptActive = 0;

	// 3. Pass 0 = wetting-class reactions, pass 1 = everything else, evaluated against post-wetting facts.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bWettingPass = (Pass == 0);
		TArray<const UDocMaterialReactionDefinition*> Running;

		for (const UDocMaterialReactionDefinition* Def : Order)
		{
			if (Def->IsWettingClass() != bWettingPass)
			{
				continue;
			}

			const FName Id = Def->ReactionId;
			const TPair<float, int32>& Channel = ChannelSnapshot.FindChecked(Def->ExposureChannel);
			const float Value = Channel.Key;
			FDocReactionInstance* Inst = ActiveReactions.Find(Id);
			const bool bWasActive = Inst && Inst->State == EDocReactionState::Active;

			FName BlockReason = NAME_None;
			EDocReactionState BlockState = EDocReactionState::Inactive;
			if (IsReactionSuppressed(Id))
			{
				BlockReason = TEXT("Suppressed");
				BlockState = EDocReactionState::Suppressed;
			}
			else if (Def->bRequiresFuel && Work.Fuel <= 0.0f)
			{
				BlockReason = TEXT("FuelDepleted");
				BlockState = EDocReactionState::Depleted;
			}
			else if (Def->bRequiresMoisture && Work.Moisture <= 0.0f)
			{
				BlockReason = TEXT("MoistureDepleted");
				BlockState = EDocReactionState::Depleted;
			}
			else if (Work.Moisture >= Def->InhibitAtMoisture)
			{
				BlockReason = TEXT("MoistureInhibit");
				BlockState = EDocReactionState::Inhibited;
			}

			if (!BlockReason.IsNone())
			{
				if (bWasActive)
				{
					Events.Ended.Add(MakeEnd(Def, *Inst, BlockState, BlockReason, Work));
				}
				else if (Inst && (Inst->State == EDocReactionState::PendingDwell || Inst->State == EDocReactionState::Outcompeted))
				{
					Inst->State = EDocReactionState::Inactive;
					Inst->DwellProgress = 0.0f;
					Inst->StatusReason = BlockReason;
				}
				continue;
			}

			if (bWasActive)
			{
				if (!Def->bSelfSustaining && Value < Def->DeactivationThreshold)
				{
					Events.Ended.Add(MakeEnd(Def, *Inst, EDocReactionState::Inactive, TEXT("BelowThreshold"), Work));
					continue;
				}
				Running.Add(Def);
				continue;
			}

			if (Value >= Def->ActivationThreshold)
			{
				if (!Inst)
				{
					Inst = &ActiveReactions.Add(Id);
					Inst->ReactionId = Id;
					Inst->GroupId = Def->ExclusiveGroup;
				}
				Inst->DwellProgress += DeltaTime;
				if (Inst->DwellProgress + KINDA_SMALL_NUMBER >= Def->DwellDuration)
				{
					Running.Add(Def);
				}
				else
				{
					Inst->State = EDocReactionState::PendingDwell;
					Inst->StatusReason = TEXT("Dwell");
				}
			}
			else if (Inst && (Inst->State == EDocReactionState::PendingDwell || Inst->State == EDocReactionState::Outcompeted))
			{
				Inst->State = EDocReactionState::Inactive;
				Inst->DwellProgress = 0.0f;
				Inst->StatusReason = TEXT("BelowThreshold");
			}
		}

		// 4. Exclusive groups: first in priority order claims the group; the rest are outcompeted.
		TArray<const UDocMaterialReactionDefinition*> Winners;
		for (const UDocMaterialReactionDefinition* Def : Running)
		{
			FDocReactionInstance& Inst = ActiveReactions.FindChecked(Def->ReactionId);
			if (!Def->ExclusiveGroup.IsNone() && ClaimedGroups.Contains(Def->ExclusiveGroup))
			{
				if (Inst.State == EDocReactionState::Active)
				{
					Events.Ended.Add(MakeEnd(Def, Inst, EDocReactionState::Outcompeted, TEXT("Outcompeted"), Work));
				}
				Inst.State = EDocReactionState::Outcompeted;
				Inst.StatusReason = TEXT("Outcompeted");
				Inst.DwellProgress = Def->DwellDuration; // resumes without a second dwell once the group frees
				continue;
			}
			if (!Def->ExclusiveGroup.IsNone())
			{
				ClaimedGroups.Add(Def->ExclusiveGroup);
			}
			Winners.Add(Def);
		}

		// 5. Active-reaction limit: already-active winners keep their slots, new starts fill what is left.
		for (const UDocMaterialReactionDefinition* Def : Winners)
		{
			if (ActiveReactions.FindChecked(Def->ReactionId).State == EDocReactionState::Active)
			{
				++KeptActive;
			}
		}
		TArray<const UDocMaterialReactionDefinition*> Executing;
		for (const UDocMaterialReactionDefinition* Def : Winners)
		{
			FDocReactionInstance& Inst = ActiveReactions.FindChecked(Def->ReactionId);
			if (Inst.State != EDocReactionState::Active)
			{
				if (KeptActive >= MaxActiveReactions)
				{
					Inst.State = EDocReactionState::Inactive;
					Inst.DwellProgress = 0.0f;
					Inst.StatusReason = TEXT("ActiveReactionLimit");
					LastRejectReason = TEXT("ActiveReactionLimit");
					continue;
				}
				const int32 Generation = ChannelSnapshot.FindChecked(Def->ExposureChannel).Value;
				Events.Started.Add(MakeStart(*Def, Inst, TEXT("ThresholdMet"), Generation, Work));
				++KeptActive;
			}
			Executing.Add(Def);
		}

		// 6. Apply deltas from executing reactions to the working state, bounded to valid ranges.
		for (const UDocMaterialReactionDefinition* Def : Executing)
		{
			FDocReactionInstance& Inst = ActiveReactions.FindChecked(Def->ReactionId);
			Inst.ActiveDuration += DeltaTime;

			if (Def->FuelConsumptionRate > 0.0f)
			{
				const float Take = FMath::Min(Work.Fuel, Def->FuelConsumptionRate * DeltaTime);
				Work.Fuel = FMath::Max(0.0f, Work.Fuel - Take);
				Inst.ConsumedFuel += Take;
			}
			if (Def->MoistureConsumptionRate > 0.0f)
			{
				const float Take = FMath::Min(Work.Moisture, Def->MoistureConsumptionRate * DeltaTime);
				Work.Moisture = FMath::Max(0.0f, Work.Moisture - Take);
				Inst.ConsumedMoisture += Take;
			}
			if (Def->MoistureGainRate > 0.0f)
			{
				const float Add = FMath::Min(1.0f - Work.Moisture, Def->MoistureGainRate * DeltaTime);
				Work.Moisture = FMath::Clamp(Work.Moisture + FMath::Max(0.0f, Add), 0.0f, 1.0f);
				Inst.ConsumedMoisture -= FMath::Max(0.0f, Add);
			}
			if (Def->PhaseChangeRate != 0.0f)
			{
				const float Old = Work.PhaseFraction;
				Work.PhaseFraction = FMath::Clamp(Work.PhaseFraction + Def->PhaseChangeRate * DeltaTime, 0.0f, 1.0f);
				Inst.DeltaPhase += Work.PhaseFraction - Old;
			}
			if (Def->CharRate > 0.0f)
			{
				Work.CharAmount = FMath::Clamp(Work.CharAmount + Def->CharRate * DeltaTime, 0.0f, 1.0f);
			}
		}

		// 7. Reactions whose resource ran out this step end as Depleted.
		for (const UDocMaterialReactionDefinition* Def : Executing)
		{
			FDocReactionInstance& Inst = ActiveReactions.FindChecked(Def->ReactionId);
			if (Def->bRequiresFuel && Work.Fuel <= 0.0f)
			{
				Events.Ended.Add(MakeEnd(Def, Inst, EDocReactionState::Depleted, TEXT("FuelDepleted"), Work));
				--KeptActive;
			}
			else if (Def->bRequiresMoisture && Work.Moisture <= 0.0f)
			{
				Events.Ended.Add(MakeEnd(Def, Inst, EDocReactionState::Depleted, TEXT("MoistureDepleted"), Work));
				--KeptActive;
			}
		}
	}

	Commit(Work, Events);
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------

FDocMaterialSnapshot UDocReactiveMaterialComponent::CaptureSnapshot() const
{
	FDocMaterialSnapshot Snapshot;
	Snapshot.ContentVersion = MaterialProfile ? MaterialProfile->ContentVersion : 0;
	Snapshot.State = CurrentState;

	for (const TPair<FName, FDocReactionInstance>& Kvp : ActiveReactions)
	{
		if (DocMaterialPrivate::IsLiveState(Kvp.Value.State))
		{
			Snapshot.Reactions.Add(Kvp.Value);
		}
	}
	Snapshot.Reactions.Sort([](const FDocReactionInstance& A, const FDocReactionInstance& B) { return A.ReactionId.LexicalLess(B.ReactionId); });

	for (const TPair<FName, TMap<FName, double>>& ReactionKvp : SuppressionSources)
	{
		for (const TPair<FName, double>& Kvp : ReactionKvp.Value)
		{
			if (Kvp.Value >= 0.0 && Kvp.Value <= SimTime)
			{
				continue;
			}
			FDocSuppressionRecord Record;
			Record.ReactionId = ReactionKvp.Key;
			Record.SourceId = Kvp.Key;
			Record.RemainingSeconds = Kvp.Value < 0.0 ? -1.0 : Kvp.Value - SimTime;
			Snapshot.Suppressions.Add(Record);
		}
	}
	Snapshot.Suppressions.Sort([](const FDocSuppressionRecord& A, const FDocSuppressionRecord& B)
	{
		return A.ReactionId == B.ReactionId ? A.SourceId.LexicalLess(B.SourceId) : A.ReactionId.LexicalLess(B.ReactionId);
	});
	return Snapshot;
}

bool UDocReactiveMaterialComponent::RestoreSnapshot(const FDocMaterialSnapshot& Snapshot, FString& OutError)
{
	if (BroadcastDepth > 0)
	{
		OutError = TEXT("Restore is not allowed from an event handler.");
		return false;
	}
	if (!MaterialProfile)
	{
		OutError = TEXT("Component has no material profile.");
		return false;
	}
	if (Snapshot.SchemaVersion != FDocMaterialSnapshot::CurrentSchemaVersion)
	{
		OutError = FString::Printf(TEXT("Unsupported snapshot schema %d."), Snapshot.SchemaVersion);
		return false;
	}
	if (Snapshot.ContentVersion != MaterialProfile->ContentVersion)
	{
		OutError = FString::Printf(TEXT("Snapshot content version %d does not match profile %d."), Snapshot.ContentVersion, MaterialProfile->ContentVersion);
		return false;
	}

	const FDocMaterialState& S = Snapshot.State;
	if (S.MaterialId != MaterialProfile->MaterialId)
	{
		OutError = TEXT("Snapshot material id does not match the profile.");
		return false;
	}
	if (!FMath::IsNearlyEqual(S.MaxFuel, MaterialProfile->MaxFuel) || S.Fuel < 0.0f || S.Fuel > S.MaxFuel
		|| S.Moisture < 0.0f || S.Moisture > 1.0f || S.PhaseFraction < 0.0f || S.PhaseFraction > 1.0f
		|| S.CharAmount < 0.0f || S.CharAmount > 1.0f)
	{
		OutError = TEXT("Snapshot material facts are out of range.");
		return false;
	}

	TSet<FName> SeenReactions;
	TSet<FName> ActiveGroups;
	int32 ActiveCount = 0;
	for (const FDocReactionInstance& Inst : Snapshot.Reactions)
	{
		const UDocMaterialReactionDefinition* Def = MaterialProfile->FindReaction(Inst.ReactionId);
		if (!Def)
		{
			OutError = FString::Printf(TEXT("Snapshot reaction %s is not allowed by the profile."), *Inst.ReactionId.ToString());
			return false;
		}
		if (SeenReactions.Contains(Inst.ReactionId))
		{
			OutError = FString::Printf(TEXT("Snapshot reaction %s appears twice."), *Inst.ReactionId.ToString());
			return false;
		}
		SeenReactions.Add(Inst.ReactionId);

		if (!DocMaterialPrivate::IsLiveState(Inst.State) || Inst.DwellProgress < 0.0f || Inst.ActiveDuration < 0.0f || Inst.Generation < 0)
		{
			OutError = FString::Printf(TEXT("Snapshot reaction %s has invalid progress."), *Inst.ReactionId.ToString());
			return false;
		}
		if (Inst.State == EDocReactionState::Active)
		{
			++ActiveCount;
			if ((Def->bRequiresFuel && S.Fuel <= 0.0f) || (Def->bRequiresMoisture && S.Moisture <= 0.0f))
			{
				OutError = FString::Printf(TEXT("Snapshot reaction %s is active without its resource."), *Inst.ReactionId.ToString());
				return false;
			}
			if (!Def->ExclusiveGroup.IsNone())
			{
				if (ActiveGroups.Contains(Def->ExclusiveGroup))
				{
					OutError = FString::Printf(TEXT("Two active reactions share exclusive group %s."), *Def->ExclusiveGroup.ToString());
					return false;
				}
				ActiveGroups.Add(Def->ExclusiveGroup);
			}
		}
	}
	if (ActiveCount > MaxActiveReactions)
	{
		OutError = TEXT("Snapshot exceeds the active-reaction limit.");
		return false;
	}
	for (const FDocSuppressionRecord& Record : Snapshot.Suppressions)
	{
		if (Record.ReactionId.IsNone() || Record.SourceId.IsNone() || !MaterialProfile->FindReaction(Record.ReactionId))
		{
			OutError = TEXT("Snapshot contains an invalid suppression record.");
			return false;
		}
	}

	// Validated: apply facts first, then reaction instances. No start events, no consumption.
	const int64 NextRevision = FMath::Max(S.Revision, CurrentState.Revision) + 1;
	CurrentState = S;

	ActiveReactions.Empty();
	for (const FDocReactionInstance& Inst : Snapshot.Reactions)
	{
		FDocReactionInstance Copy = Inst;
		Copy.GroupId = MaterialProfile->FindReaction(Inst.ReactionId)->ExclusiveGroup;
		ActiveReactions.Add(Inst.ReactionId, Copy);
	}

	SuppressionSources.Empty();
	for (const FDocSuppressionRecord& Record : Snapshot.Suppressions)
	{
		const double Expiry = Record.RemainingSeconds < 0.0 ? -1.0 : SimTime + Record.RemainingSeconds;
		SuppressionSources.FindOrAdd(Record.ReactionId).Add(Record.SourceId, Expiry);
	}

	// Exposure samples are live handles owned by their sources; they re-register after restore.
	ExposureSources.Empty();

	FGameplayTagContainer Tags = MaterialProfile->BaseMaterialTags;
	for (const TPair<FName, FDocReactionInstance>& Kvp : ActiveReactions)
	{
		if (Kvp.Value.State == EDocReactionState::Active)
		{
			Tags.AppendTags(MaterialProfile->FindReaction(Kvp.Key)->ResultingTags);
		}
	}
	CurrentState.StateTags = Tags;
	CurrentState.Revision = NextRevision;
	LastRejectReason = NAME_None;

	// Presentation rebuilds from state; this is the only event a restore emits.
	++BroadcastDepth;
	const FDocMaterialState Committed = CurrentState;
	OnMaterialStateChanged.Broadcast(Committed, TEXT("Restored"));
	OnMaterialStateChangedNative.Broadcast(Committed, TEXT("Restored"));
	--BroadcastDepth;
	return true;
}
