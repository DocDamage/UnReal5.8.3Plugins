#include "DocWorldActivationSubsystem.h"
#include "DocWorldActivationLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocWorldActivationSubsystem)

namespace DocActivationTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Activation, "Activation", "DocWorldActivation tiers (rank is explicit, not lexical)");
	UE_DEFINE_GAMEPLAY_TAG(Dormant, "Activation.Dormant");
	UE_DEFINE_GAMEPLAY_TAG(Representation, "Activation.Representation");
	UE_DEFINE_GAMEPLAY_TAG(Lightweight, "Activation.Lightweight");
	UE_DEFINE_GAMEPLAY_TAG(Active, "Activation.Active");
	UE_DEFINE_GAMEPLAY_TAG(Full, "Activation.Full");
}

// ---------------------------------------------------------------------------
// Policy
// ---------------------------------------------------------------------------

EDocActivationTier UDocActivationPolicy::EvaluateDistance(float DistanceCm, EDocActivationTier Current) const
{
	// Promotion: highest tier whose promote threshold contains the distance.
	EDocActivationTier Promote = FarTier;
	for (const FDocActivationBand& Band : Bands)
	{
		if (DistanceCm <= Band.PromoteWithinCm && Band.Tier > Promote)
		{
			Promote = Band.Tier;
		}
	}
	if (Promote >= Current)
	{
		return Promote;
	}
	// Demotion only once beyond the current band's demote threshold (hysteresis).
	if (const FDocActivationBand* CurrentBand = Bands.FindByPredicate([Current](const FDocActivationBand& B) { return B.Tier == Current; }))
	{
		if (DistanceCm <= CurrentBand->DemoteBeyondCm)
		{
			return Current;
		}
	}
	EDocActivationTier Result = FarTier;
	for (const FDocActivationBand& Band : Bands)
	{
		if (Band.Tier < Current && DistanceCm <= Band.DemoteBeyondCm && Band.Tier > Result)
		{
			Result = Band.Tier;
		}
	}
	return FMath::Max(Result, Promote);
}

#if WITH_EDITOR
EDataValidationResult UDocActivationPolicy::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (const FDocActivationBand& Band : Bands)
	{
		if (Band.DemoteBeyondCm < Band.PromoteWithinCm)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Band %s: DemoteBeyondCm must be >= PromoteWithinCm (hysteresis)"),
				*StaticEnum<EDocActivationTier>()->GetNameStringByValue((int64)Band.Tier))));
			Result = EDataValidationResult::Invalid;
		}
	}
	if (MinTier > MaxTier)
	{
		Context.AddError(FText::FromString(TEXT("MinTier is above MaxTier")));
		Result = EDataValidationResult::Invalid;
	}
	if (UnsupportedFallbackTier == EDocActivationTier::Representation)
	{
		Context.AddError(FText::FromString(TEXT("UnsupportedFallbackTier cannot itself require an adapter")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

UDocWorldActivationComponent::UDocWorldActivationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocWorldActivationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (LogicalId.IsNone() && GetOwner())
	{
		LogicalId = GetOwner()->GetFName();
	}
	if (UDocWorldActivationSubsystem* Activation = UDocWorldActivationSubsystem::Get(this))
	{
		Activation->Register(this);
	}
}

void UDocWorldActivationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocWorldActivationSubsystem* Activation = UDocWorldActivationSubsystem::Get(this))
	{
		Activation->Unregister(this); // unload/travel: never gameplay destruction
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// Subsystem lifetime
// ---------------------------------------------------------------------------

UDocWorldActivationSubsystem* UDocWorldActivationSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocWorldActivationSubsystem>() : nullptr;
}

bool UDocWorldActivationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocWorldActivationSubsystem::Deinitialize()
{
	for (TPair<FObjectKey, FRecord>& Pair : Records)
	{
		RestoreOwnedOverrides(Pair.Value);
	}
	Records.Reset();
	Order.Reset();
	PriorityQueue.Reset();
	TransitionQueue.Reset();
	Cells.Reset();
	Pins.Reset();
	Super::Deinitialize();
}

TStatId UDocWorldActivationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocWorldActivationSubsystem, STATGROUP_Tickables);
}

UDocWorldActivationSubsystem::FRecord* UDocWorldActivationSubsystem::FindRecord(const UDocWorldActivationComponent* Object)
{
	return Records.Find(FObjectKey(Object));
}

const UDocWorldActivationSubsystem::FRecord* UDocWorldActivationSubsystem::FindRecord(const UDocWorldActivationComponent* Object) const
{
	return Records.Find(FObjectKey(Object));
}

FIntVector UDocWorldActivationSubsystem::CellOf(const FVector& Location) const
{
	const double Size = GetDefault<UDocWorldActivationSettings>()->CellSizeCm;
	return FIntVector(FMath::FloorToInt(Location.X / Size), FMath::FloorToInt(Location.Y / Size), 0);
}

void UDocWorldActivationSubsystem::UpdateCell(const FObjectKey& Key, FRecord& Record)
{
	const UDocWorldActivationComponent* Object = Record.Object.Get();
	const AActor* Owner = Object ? Object->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}
	const FIntVector NewCell = CellOf(Owner->GetActorLocation());
	if (NewCell != Record.Cell || !Cells.Contains(NewCell) || !Cells[NewCell].Contains(Key))
	{
		if (TArray<FObjectKey>* Old = Cells.Find(Record.Cell)) { Old->Remove(Key); }
		Cells.FindOrAdd(NewCell).AddUnique(Key);
		Record.Cell = NewCell;
	}
}

// ---------------------------------------------------------------------------
// Registration, pins, sources
// ---------------------------------------------------------------------------

void UDocWorldActivationSubsystem::Register(UDocWorldActivationComponent* Object)
{
	if (!Object || FindRecord(Object))
	{
		return;
	}
	const FObjectKey Key(Object);
	FRecord& Record = Records.Add(Key);
	Record.Object = Object;
	Record.Current = Object->GetCurrentTier();
	Record.Desired = Record.Current;
	Record.TierEnteredAt = Clock;
	Record.NextEvaluation = Clock;
	Record.Cell = FIntVector(MAX_int32);
	UpdateCell(Key, Record);
	Order.Add(Key);
	PriorityQueue.AddUnique(Key);
	Stats.Registered = Records.Num();
}

void UDocWorldActivationSubsystem::RemoveRecord(const FObjectKey& Key)
{
	if (FRecord* Record = Records.Find(Key))
	{
		if (TArray<FObjectKey>* Cell = Cells.Find(Record->Cell)) { Cell->Remove(Key); }
	}
	Records.Remove(Key);
	Order.Remove(Key);
	PriorityQueue.Remove(Key);
	TransitionQueue.Remove(Key);
	Stats.Registered = Records.Num();
}

void UDocWorldActivationSubsystem::Unregister(UDocWorldActivationComponent* Object)
{
	FRecord* Record = FindRecord(Object);
	if (!Record)
	{
		return;
	}
	RestoreOwnedOverrides(*Record);
	Pins.RemoveIf([Object](const FDocRequestHandle&, const FPin& Pin) { return Pin.Object.Get() == Object || !Pin.Object.IsValid(); });
	RemoveRecord(FObjectKey(Object));
	++Stats.UnregisteredByUnload;
}

FDocRequestHandle UDocWorldActivationSubsystem::PinTier(UDocWorldActivationComponent* Object, EDocActivationTier MinTier, UObject* Owner, FName Reason, float DurationSeconds)
{
	if (!Object || !Owner || !FindRecord(Object))
	{
		return FDocRequestHandle();
	}
	FPin Pin;
	Pin.Object = Object;
	Pin.Owner = Owner;
	Pin.MinTier = MinTier;
	Pin.Reason = Reason;
	Pin.ExpiresAt = DurationSeconds > 0.f ? Clock + DurationSeconds : 0.0;
	const FDocRequestHandle Handle = Pins.Add(Owner, Pin);
	PriorityQueue.AddUnique(FObjectKey(Object));
	return Handle;
}

FDocSystemResult UDocWorldActivationSubsystem::ReleasePin(FDocRequestHandle Handle, UObject* Owner)
{
	FPin Removed;
	switch (Pins.Validate(Handle, Owner))
	{
	case EDocHandleStatus::Active:
		Pins.Remove(Handle, Owner, &Removed);
		if (Removed.Object.IsValid()) { PriorityQueue.AddUnique(FObjectKey(Removed.Object.Get())); }
		return FDocSystemResult::MakeSuccess();
	case EDocHandleStatus::WrongScope:
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Pin belongs to another owner"));
	default:
		return FDocSystemResult::MakeNoChange(TEXT("Pin already released"));
	}
}

FDocSystemResult UDocWorldActivationSubsystem::SetManualTier(UDocWorldActivationComponent* Object, EDocActivationTier Tier)
{
	FRecord* Record = FindRecord(Object);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Object is not registered"));
	}
	if (!Object->Policy || Object->Policy->Kind != EDocActivationPolicyKind::Manual)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Object does not use a Manual policy"));
	}
	if (Record->Manual.IsSet() && Record->Manual.GetValue() == Tier)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Record->Manual = Tier;
	PriorityQueue.AddUnique(FObjectKey(Object));
	return FDocSystemResult::MakeSuccess();
}

void UDocWorldActivationSubsystem::AddRelevanceSource(USceneComponent* Source)
{
	if (Source)
	{
		ExtraSources.AddUnique(Source);
		RequestPriorityReevaluation(Source->GetComponentLocation());
	}
}

void UDocWorldActivationSubsystem::RemoveRelevanceSource(USceneComponent* Source)
{
	if (Source && ExtraSources.Remove(Source) > 0)
	{
		RequestPriorityReevaluation(Source->GetComponentLocation());
	}
}

void UDocWorldActivationSubsystem::RequestPriorityReevaluation(FVector Location)
{
	const UDocWorldActivationSettings* Settings = GetDefault<UDocWorldActivationSettings>();
	const int32 Reach = FMath::CeilToInt(Settings->PriorityRadiusCm / Settings->CellSizeCm);
	const FIntVector Center = CellOf(Location);
	for (int32 X = -Reach; X <= Reach; ++X)
	{
		for (int32 Y = -Reach; Y <= Reach; ++Y)
		{
			if (const TArray<FObjectKey>* Members = Cells.Find(Center + FIntVector(X, Y, 0)))
			{
				for (const FObjectKey& Key : *Members) { PriorityQueue.AddUnique(Key); }
			}
		}
	}
}

void UDocWorldActivationSubsystem::RegisterRepresentationAdapter(TSharedPtr<IDocActivationRepresentationAdapter> Adapter)
{
	if (Adapter.IsValid()) { Adapters.AddUnique(Adapter); }
}

void UDocWorldActivationSubsystem::UnregisterRepresentationAdapter(TSharedPtr<IDocActivationRepresentationAdapter> Adapter)
{
	Adapters.Remove(Adapter);
}

void UDocWorldActivationSubsystem::GatherSources(TArray<FVector>& Out) const
{
	Out.Reset();
	if (bUseSourceOverride)
	{
		Out = SourceOverride;
	}
	else if (const UWorld* World = GetWorld())
	{
		const bool bAll = GetDefault<UDocWorldActivationSettings>()->bUseAllPlayersAsSources;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (!PC || (!bAll && !PC->IsLocalController()))
			{
				continue;
			}
			if (const APawn* Pawn = PC->GetPawn())
			{
				Out.Add(Pawn->GetActorLocation());
			}
			else if (PC->IsLocalController())
			{
				FVector Location;
				FRotator Rotation;
				PC->GetPlayerViewPoint(Location, Rotation);
				Out.Add(Location);
			}
		}
	}
	for (const TWeakObjectPtr<USceneComponent>& Source : ExtraSources)
	{
		if (const USceneComponent* S = Source.Get()) { Out.Add(S->GetComponentLocation()); }
	}
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

EDocActivationTier UDocWorldActivationSubsystem::PinnedMinimum(const UDocWorldActivationComponent* Object, int32* OutCount) const
{
	EDocActivationTier Min = EDocActivationTier::Dormant;
	int32 Count = 0;
	Pins.ForEach([&](const FDocRequestHandle&, const FPin& Pin)
	{
		if (Pin.Object.Get() == Object && Pin.Owner.IsValid() && (Pin.ExpiresAt <= 0.0 || Clock < Pin.ExpiresAt))
		{
			++Count;
			Min = FMath::Max(Min, Pin.MinTier);
		}
	});
	if (OutCount) { *OutCount = Count; }
	return Min;
}

bool UDocWorldActivationSubsystem::IsTierSupported(const FRecord& Record, EDocActivationTier Tier, IDocActivationRepresentationAdapter** OutAdapter) const
{
	if (OutAdapter) { *OutAdapter = nullptr; }
	if (Tier != EDocActivationTier::Representation)
	{
		return true; // loaded-actor tiers are handled by the base adapter
	}
	for (const TSharedPtr<IDocActivationRepresentationAdapter>& Adapter : Adapters)
	{
		if (Adapter->SupportsTier(Record.Object.Get(), Tier))
		{
			if (OutAdapter) { *OutAdapter = Adapter.Get(); }
			return true;
		}
	}
	return false;
}

EDocActivationTier UDocWorldActivationSubsystem::ComputeDesired(FRecord& Record, const TArray<FVector>& Sources, bool& bOutBypassDwell)
{
	bOutBypassDwell = false;
	UDocWorldActivationComponent* Object = Record.Object.Get();
	const UDocActivationPolicy* Policy = Object->Policy;
	const AActor* Owner = Object->GetOwner();

	Record.NearestCm = -1.f;
	for (const FVector& Source : Sources)
	{
		const float D = (float)FVector::Dist(Source, Owner->GetActorLocation());
		Record.NearestCm = Record.NearestCm < 0.f ? D : FMath::Min(Record.NearestCm, D);
	}

	EDocActivationTier Tier = EDocActivationTier::Active;
	if (Policy)
	{
		switch (Policy->Kind)
		{
		case EDocActivationPolicyKind::AlwaysActive:
			Tier = EDocActivationTier::Active;
			break;
		case EDocActivationPolicyKind::Manual:
			Tier = Record.Manual.Get(Policy->DefaultTier);
			bOutBypassDwell = true; // explicit request
			break;
		case EDocActivationPolicyKind::DistanceBased:
			if (Record.NearestCm < 0.f)
			{
				Tier = Policy->DefaultTier;
				Record.LastDiagnostic = TEXT("No relevance sources; using DefaultTier");
			}
			else
			{
				Tier = Policy->EvaluateDistance(Record.NearestCm, Record.Current);
			}
			break;
		case EDocActivationPolicyKind::Custom:
		{
			UObject* Participant = nullptr;
			if (Cast<IDocActivationParticipant>(const_cast<AActor*>(Owner)) || (Owner && Owner->GetClass()->ImplementsInterface(UDocActivationParticipant::StaticClass())))
			{
				Participant = const_cast<AActor*>(Owner);
			}
			if (!Participant && Owner)
			{
				for (UActorComponent* Component : Owner->GetComponents())
				{
					if (Component && (Cast<IDocActivationParticipant>(Component) || Component->GetClass()->ImplementsInterface(UDocActivationParticipant::StaticClass())))
					{
						Participant = Component;
						break;
					}
				}
			}
			if (Participant)
			{
				Tier = IDocActivationParticipant::Execute_GetCustomDesiredTier(Participant, Record.Current);
			}
			else
			{
				Tier = Policy->DefaultTier;
				Record.LastDiagnostic = TEXT("Custom policy without an IDocActivationParticipant; using DefaultTier");
			}
			break;
		}
		case EDocActivationPolicyKind::VisibilityBased:
		case EDocActivationPolicyKind::InteractionBased:
			Tier = Policy->DefaultTier;
			Record.LastDiagnostic = TEXT("Policy needs a relevance/pin provider bridge; using DefaultTier");
			break;
		}
		Tier = FMath::Clamp(Tier, Policy->MinTier, Policy->MaxTier);
	}

	const EDocActivationTier PinMin = PinnedMinimum(Object);
	if (PinMin > Tier)
	{
		Tier = PinMin;
		bOutBypassDwell |= Tier > Record.Current; // pins raise immediately (safety)
	}

	if (!IsTierSupported(Record, Tier, nullptr))
	{
		const EDocActivationTier Fallback = Policy ? Policy->UnsupportedFallbackTier : EDocActivationTier::Active;
		Record.LastDiagnostic = FString::Printf(TEXT("%s unsupported (no representation adapter); using %s"),
			*StaticEnum<EDocActivationTier>()->GetNameStringByValue((int64)Tier), *StaticEnum<EDocActivationTier>()->GetNameStringByValue((int64)Fallback));
		Tier = IsTierSupported(Record, Fallback, nullptr) ? Fallback : EDocActivationTier::Active;
	}
	return Tier;
}

void UDocWorldActivationSubsystem::Evaluate(FRecord& Record, const TArray<FVector>& Sources)
{
	const UDocWorldActivationComponent* Object = Record.Object.Get();
	if (!Object || !Object->GetOwner())
	{
		return;
	}
	const FObjectKey Key(Object);
	UpdateCell(Key, Record);
	bool bBypassDwell = false;
	Record.Desired = ComputeDesired(Record, Sources, bBypassDwell);
	Record.NextEvaluation = Clock + GetDefault<UDocWorldActivationSettings>()->EvaluationIntervalSeconds;
	const float Dwell = Object->Policy ? Object->Policy->MinDwellSeconds : 0.f;
	if (Record.Desired != Record.Current && !Record.bQueued && (bBypassDwell || Clock - Record.TierEnteredAt >= Dwell))
	{
		Record.bQueued = true;
		TransitionQueue.Add(Key);
	}
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

bool UDocWorldActivationSubsystem::Transition(FRecord& Record, EDocActivationTier To)
{
	UDocWorldActivationComponent* Object = Record.Object.Get();
	const EDocActivationTier From = Record.Current;
	if (!Object || From == To)
	{
		return false;
	}
	IDocActivationRepresentationAdapter* Adapter = nullptr;
	IDocActivationRepresentationAdapter* FromAdapter = nullptr;
	IsTierSupported(Record, To, &Adapter);
	IsTierSupported(Record, From, &FromAdapter);
	IDocActivationRepresentationAdapter* Guarded = Adapter ? Adapter : FromAdapter;

	if (Guarded)
	{
		// Guard → prepare destination → validate → commit swap → release source; prior stays usable on failure.
		const FDocSystemResult Prepared = Guarded->PrepareTransition(Object, From, To);
		if (!Prepared.IsSuccess())
		{
			Record.LastDiagnostic = FString::Printf(TEXT("Prepare %s failed: %s (kept prior representation)"), *Guarded->GetAdapterName().ToString(), *Prepared.ToString());
			return false;
		}
		if (To != EDocActivationTier::Representation)
		{
			ApplyBaseTier(Record, To); // destination loaded-actor state before the swap commits
		}
		const FDocSystemResult Committed = Guarded->CommitTransition(Object, From, To);
		if (!Committed.IsSuccess())
		{
			Guarded->RollbackTransition(Object, From, To);
			if (From != EDocActivationTier::Representation) { ApplyBaseTier(Record, From); }
			Record.LastDiagnostic = FString::Printf(TEXT("Commit %s failed: %s (rolled back)"), *Guarded->GetAdapterName().ToString(), *Committed.ToString());
			return false;
		}
		Record.Representation = To == EDocActivationTier::Representation ? Guarded->GetAdapterName() : FName(TEXT("LoadedActor"));
	}
	else
	{
		ApplyBaseTier(Record, To);
	}
	Record.Current = To;
	Record.TierEnteredAt = Clock;
	++Record.Transitions;
	Object->SetCurrentTierInternal(To);
	NotifyTierChanged(Record, From, To);
	return true;
}

void UDocWorldActivationSubsystem::NotifyTierChanged(FRecord& Record, EDocActivationTier OldTier, EDocActivationTier NewTier)
{
	UDocWorldActivationComponent* Object = Record.Object.Get();
	AActor* Owner = Object ? Object->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}
	if (Cast<IDocActivationParticipant>(Owner) || Owner->GetClass()->ImplementsInterface(UDocActivationParticipant::StaticClass()))
	{
		IDocActivationParticipant::Execute_OnActivationTierChanged(Owner, OldTier, NewTier);
	}
	for (UActorComponent* Component : Owner->GetComponents())
	{
		if (Component && (Cast<IDocActivationParticipant>(Component) || Component->GetClass()->ImplementsInterface(UDocActivationParticipant::StaticClass())))
		{
			IDocActivationParticipant::Execute_OnActivationTierChanged(Component, OldTier, NewTier);
		}
	}
	OnTierChangedNative.Broadcast(Object, OldTier, NewTier);
	Object->OnTierChanged.Broadcast(Object, OldTier, NewTier);
}

void UDocWorldActivationSubsystem::ApplyBaseTier(FRecord& Record, EDocActivationTier Tier)
{
	UDocWorldActivationComponent* Object = Record.Object.Get();
	AActor* Owner = Object ? Object->GetOwner() : nullptr;
	if (!Owner)
	{
		return;
	}
	const FDocActivationTierSettings* Settings = Object->Profile ? Object->Profile->Find(Tier) : nullptr;
	if (!Settings)
	{
		RestoreOwnedOverrides(Record); // tier without settings = host-defined normal state
		return;
	}
	FOwnedOverride& O = Record.Override;
	const FDocActivationCapabilities& Caps = Object->Controlled;

	if (Caps.bActorTick && Owner->PrimaryActorTick.bCanEverTick)
	{
		const bool bNowEnabled = Owner->IsActorTickEnabled();
		const float NowInterval = Owner->GetActorTickInterval();
		if (!O.bHasTick || bNowEnabled != O.bSetTickEnabled || !FMath::IsNearlyEqual(NowInterval, O.SetTickInterval))
		{
			// First override, or the host changed it since: the host's value becomes the one to restore.
			O.bOrigTickEnabled = bNowEnabled;
			O.OrigTickInterval = NowInterval;
		}
		O.bSetTickEnabled = !Settings->bDisableTick;
		O.SetTickInterval = Settings->TickInterval;
		Owner->SetActorTickEnabled(O.bSetTickEnabled);
		Owner->SetActorTickInterval(O.SetTickInterval);
		O.bHasTick = true;
	}
	if (Caps.bComponentTick)
	{
		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (!Component || Component == Object || !Component->PrimaryComponentTick.bCanEverTick)
			{
				continue;
			}
			const TWeakObjectPtr<UActorComponent> Weak(Component);
			const float NowInterval = Component->GetComponentTickInterval();
			const bool bNowEnabled = Component->IsComponentTickEnabled();
			TPair<float, float>& Interval = O.ComponentIntervals.FindOrAdd(Weak, TPair<float, float>(NowInterval, NowInterval));
			TPair<bool, bool>& Enabled = O.ComponentTickEnabled.FindOrAdd(Weak, TPair<bool, bool>(bNowEnabled, bNowEnabled));
			if (!FMath::IsNearlyEqual(NowInterval, Interval.Value)) { Interval.Key = NowInterval; }
			if (bNowEnabled != Enabled.Value) { Enabled.Key = bNowEnabled; }
			Interval.Value = Settings->TickInterval;
			Enabled.Value = !Settings->bDisableTick && Enabled.Key;
			Component->SetComponentTickInterval(Interval.Value);
			Component->SetComponentTickEnabled(Enabled.Value);
		}
	}
	if (Caps.bVisibility)
	{
		const bool bNowHidden = Owner->IsHidden();
		if (!O.bHasHidden || bNowHidden != O.bSetHidden) { O.bOrigHidden = bNowHidden; }
		O.bSetHidden = Settings->bHidden || O.bOrigHidden;
		Owner->SetActorHiddenInGame(O.bSetHidden);
		O.bHasHidden = true;
	}
	if (Caps.bCollision)
	{
		const bool bNowCollision = Owner->GetActorEnableCollision();
		if (!O.bHasCollision || bNowCollision != O.bSetCollision) { O.bOrigCollision = bNowCollision; }
		O.bSetCollision = !Settings->bDisableCollision && O.bOrigCollision;
		Owner->SetActorEnableCollision(O.bSetCollision);
		O.bHasCollision = true;
	}
}

void UDocWorldActivationSubsystem::RestoreOwnedOverrides(FRecord& Record)
{
	FOwnedOverride& O = Record.Override;
	UDocWorldActivationComponent* Object = Record.Object.Get();
	AActor* Owner = Object ? Object->GetOwner() : nullptr;
	if (Owner && !Owner->IsActorBeingDestroyed())
	{
		// Reconcile: restore only values that still hold what we set; host changes win.
		if (O.bHasTick)
		{
			if (Owner->IsActorTickEnabled() == O.bSetTickEnabled) { Owner->SetActorTickEnabled(O.bOrigTickEnabled); }
			if (FMath::IsNearlyEqual(Owner->GetActorTickInterval(), O.SetTickInterval)) { Owner->SetActorTickInterval(O.OrigTickInterval); }
		}
		for (const TPair<TWeakObjectPtr<UActorComponent>, TPair<float, float>>& Pair : O.ComponentIntervals)
		{
			UActorComponent* Component = Pair.Key.Get();
			if (Component && FMath::IsNearlyEqual(Component->GetComponentTickInterval(), Pair.Value.Value)) { Component->SetComponentTickInterval(Pair.Value.Key); }
		}
		for (const TPair<TWeakObjectPtr<UActorComponent>, TPair<bool, bool>>& Pair : O.ComponentTickEnabled)
		{
			UActorComponent* Component = Pair.Key.Get();
			if (Component && Component->IsComponentTickEnabled() == Pair.Value.Value) { Component->SetComponentTickEnabled(Pair.Value.Key); }
		}
		if (O.bHasHidden && Owner->IsHidden() == O.bSetHidden) { Owner->SetActorHiddenInGame(O.bOrigHidden); }
		if (O.bHasCollision && Owner->GetActorEnableCollision() == O.bSetCollision) { Owner->SetActorEnableCollision(O.bOrigCollision); }
	}
	O = FOwnedOverride();
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void UDocWorldActivationSubsystem::TickActivation(float DeltaSeconds)
{
	const double Start = FPlatformTime::Seconds();
	Clock += FMath::Max(0.f, DeltaSeconds);
	const UDocWorldActivationSettings* Settings = GetDefault<UDocWorldActivationSettings>();

	// Pins whose owner died or that expired never pin forever.
	TArray<FObjectKey> Unpinned;
	Pins.RemoveIf([this, &Unpinned](const FDocRequestHandle&, const FPin& Pin)
	{
		const bool bDead = !Pin.Owner.IsValid() || !Pin.Object.IsValid() || (Pin.ExpiresAt > 0.0 && Clock >= Pin.ExpiresAt);
		if (bDead && Pin.Object.IsValid()) { Unpinned.Add(FObjectKey(Pin.Object.Get())); }
		return bDead;
	});
	for (const FObjectKey& Key : Unpinned) { PriorityQueue.AddUnique(Key); }

	// Objects that vanished without EndPlay.
	for (int32 i = Order.Num() - 1; i >= 0; --i)
	{
		const FRecord* Record = Records.Find(Order[i]);
		if (!Record || !Record->Object.IsValid()) { RemoveRecord(Order[i]); }
	}

	TArray<FVector> Sources;
	GatherSources(Sources);

	int32 Evaluations = 0;
	while (PriorityQueue.Num() > 0 && Evaluations < Settings->MaxEvaluationsPerTick)
	{
		const FObjectKey Key = PriorityQueue[0];
		PriorityQueue.RemoveAt(0);
		if (FRecord* Record = Records.Find(Key))
		{
			Evaluate(*Record, Sources);
			++Evaluations;
		}
	}
	for (int32 Step = 0; Step < Order.Num() && Evaluations < Settings->MaxEvaluationsPerTick; ++Step)
	{
		RoundRobin = Order.Num() > 0 ? RoundRobin % Order.Num() : 0;
		FRecord* Record = Records.Find(Order[RoundRobin]);
		++RoundRobin;
		if (Record && Clock >= Record->NextEvaluation)
		{
			Evaluate(*Record, Sources);
			++Evaluations;
		}
	}

	int32 Transitions = 0;
	while (TransitionQueue.Num() > 0 && Transitions < Settings->MaxTransitionsPerTick)
	{
		const FObjectKey Key = TransitionQueue[0];
		TransitionQueue.RemoveAt(0);
		FRecord* Record = Records.Find(Key);
		if (!Record)
		{
			continue;
		}
		Record->bQueued = false;
		if (Record->Desired != Record->Current)
		{
			if (Transition(*Record, Record->Desired))
			{
				++Transitions;
				RecentTransitions.Add(Clock);
			}
		}
	}

	RecentTransitions.RemoveAll([this](double T) { return T < Clock - 1.0; });
	Stats.Registered = Records.Num();
	Stats.EvaluationsLastTick = Evaluations;
	Stats.TransitionsLastTick = Transitions;
	Stats.PendingTransitions = TransitionQueue.Num();
	Stats.TransitionsPerSecond = RecentTransitions.Num();
	Stats.CountByTier.Reset();
	for (const TPair<FObjectKey, FRecord>& Pair : Records)
	{
		++Stats.CountByTier.FindOrAdd(Pair.Value.Current);
	}
	Stats.LastTickMilliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
}

FDocActivationDebugInfo UDocWorldActivationSubsystem::GetDebugInfo(const UDocWorldActivationComponent* Object) const
{
	FDocActivationDebugInfo Info;
	const FRecord* Record = FindRecord(Object);
	if (!Record)
	{
		Info.LastDiagnostic = TEXT("Not registered");
		return Info;
	}
	Info.LogicalId = Object->LogicalId;
	Info.Current = Record->Current;
	Info.Desired = Record->Desired;
	Info.Policy = Object->Policy ? Object->Policy->Kind : EDocActivationPolicyKind::AlwaysActive;
	Info.NearestSourceCm = Record->NearestCm;
	Info.PinnedMinTier = PinnedMinimum(Object, &Info.Pins);
	Info.Representation = Record->Representation;
	Info.bTransitionQueued = Record->bQueued;
	Info.Transitions = Record->Transitions;
	Info.LastDiagnostic = Record->LastDiagnostic;
	return Info;
}
