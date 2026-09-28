#include "DocInteractionComponents.h"
#include "DocInteractionProviders.h"
#include "DocInteractionSubsystem.h"
#include "DocInteractionLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInteractionComponents)

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------

#if WITH_EDITOR
namespace DocInteractionValidation
{
	EDataValidationResult ValidateDefinitions(const TArray<FDocInteractionDefinition>& Definitions, FDataValidationContext& Context, const FString& Owner)
	{
		EDataValidationResult Result = EDataValidationResult::Valid;
		TSet<FName> Seen;
		for (int32 i = 0; i < Definitions.Num(); ++i)
		{
			const FDocInteractionDefinition& Def = Definitions[i];
			if (Def.DefinitionId.IsNone())
			{
				Context.AddError(FText::FromString(FString::Printf(TEXT("%s: definition %d has no DefinitionId"), *Owner, i)));
				Result = EDataValidationResult::Invalid;
			}
			else if (Seen.Contains(Def.DefinitionId))
			{
				Context.AddError(FText::FromString(FString::Printf(TEXT("%s: duplicate DefinitionId '%s'"), *Owner, *Def.DefinitionId.ToString())));
				Result = EDataValidationResult::Invalid;
			}
			Seen.Add(Def.DefinitionId);

			if (Def.Mode == EDocInteractionMode::HoldToComplete && Def.HoldDuration <= 0.f)
			{
				Context.AddWarning(FText::FromString(FString::Printf(TEXT("%s: '%s' is HoldToComplete with zero HoldDuration (behaves as Instant)"), *Owner, *Def.DefinitionId.ToString())));
			}
			if (Def.Mode == EDocInteractionMode::Repeated && Def.RepeatInterval <= 0.f)
			{
				Context.AddError(FText::FromString(FString::Printf(TEXT("%s: '%s' Repeated needs RepeatInterval > 0"), *Owner, *Def.DefinitionId.ToString())));
				Result = EDataValidationResult::Invalid;
			}
			for (const TObjectPtr<UDocInteractionCondition>& Condition : Def.Conditions)
			{
				if (!Condition)
				{
					Context.AddError(FText::FromString(FString::Printf(TEXT("%s: '%s' has an empty condition slot"), *Owner, *Def.DefinitionId.ToString())));
					Result = EDataValidationResult::Invalid;
				}
			}
			for (const TObjectPtr<UDocInteractionAction>& Action : Def.Actions)
			{
				if (!Action)
				{
					Context.AddError(FText::FromString(FString::Printf(TEXT("%s: '%s' has an empty action slot"), *Owner, *Def.DefinitionId.ToString())));
					Result = EDataValidationResult::Invalid;
				}
			}
		}
		return Result;
	}
}

EDataValidationResult UDocInteractionProfile::IsDataValid(FDataValidationContext& Context) const
{
	return CombineDataValidationResults(Super::IsDataValid(Context), DocInteractionValidation::ValidateDefinitions(Definitions, Context, GetName()));
}

EDataValidationResult UDocInteractableComponent::IsDataValid(FDataValidationContext& Context) const
{
	return CombineDataValidationResults(Super::IsDataValid(Context), DocInteractionValidation::ValidateDefinitions(Definitions, Context, GetPathName()));
}
#endif

// ---------------------------------------------------------------------------
// UDocInteractableComponent
// ---------------------------------------------------------------------------

UDocInteractableComponent::UDocInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FVector UDocInteractableComponent::GetInteractionLocation() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorTransform().TransformPosition(InteractionPointOffset) : InteractionPointOffset;
}

const FDocInteractionDefinition* UDocInteractableComponent::FindDefinition(FName DefinitionId) const
{
	for (const FDocInteractionDefinition& Def : Definitions)
	{
		if (Def.DefinitionId == DefinitionId)
		{
			return &Def;
		}
	}
	if (Profile)
	{
		for (const FDocInteractionDefinition& Def : Profile->Definitions)
		{
			if (Def.DefinitionId == DefinitionId)
			{
				return &Def;
			}
		}
	}
	return nullptr;
}

void UDocInteractableComponent::GetEffectiveDefinitions(TArray<const FDocInteractionDefinition*>& Out) const
{
	Out.Reset();
	TSet<FName> Seen;
	for (const FDocInteractionDefinition& Def : Definitions)
	{
		Seen.Add(Def.DefinitionId);
		Out.Add(&Def);
	}
	if (Profile)
	{
		for (const FDocInteractionDefinition& Def : Profile->Definitions)
		{
			if (!Seen.Contains(Def.DefinitionId))
			{
				Out.Add(&Def);
			}
		}
	}
	Out.StableSort([](const FDocInteractionDefinition& A, const FDocInteractionDefinition& B) { return A.Priority > B.Priority; });
}

void UDocInteractableComponent::OnRegister()
{
	Super::OnRegister();
	if (!IsTemplate())
	{
		if (UDocInteractionSubsystem* Subsystem = UDocInteractionSubsystem::Get(this))
		{
			Subsystem->RegisterInteractable(this);
		}
	}
}

void UDocInteractableComponent::OnUnregister()
{
	if (!IsTemplate())
	{
		if (UDocInteractionSubsystem* Subsystem = UDocInteractionSubsystem::Get(this))
		{
			Subsystem->UnregisterInteractable(this);
		}
	}
	Super::OnUnregister();
}

// ---------------------------------------------------------------------------
// UDocInteractorComponent
// ---------------------------------------------------------------------------

UDocInteractorComponent::UDocInteractorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UDocInteractorComponent::AddIgnoredActor(AActor* Actor)
{
	if (Actor)
	{
		IgnoredActors.AddUnique(Actor);
	}
}

void UDocInteractorComponent::ClearIgnoredActors()
{
	IgnoredActors.Reset();
}

void UDocInteractorComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(bAutoDetect);
}

void UDocInteractorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelActiveInteraction();
	Candidates.Reset();
	Focused.Reset();
	Super::EndPlay(EndPlayReason);
}

void UDocInteractorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bAutoDetect)
	{
		return;
	}
	TimeSinceDetection += DeltaTime;
	if (TimeSinceDetection >= DetectionInterval)
	{
		TimeSinceDetection = 0.f;
		RefreshDetection();
	}
}

void UDocInteractorComponent::GetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	if (const AActor* Owner = GetOwner())
	{
		Owner->GetActorEyesViewPoint(OutLocation, OutRotation);
	}
	else
	{
		OutLocation = FVector::ZeroVector;
		OutRotation = FRotator::ZeroRotator;
	}
}

TArray<FDocInteractionCandidate> UDocInteractorComponent::QueryCandidates() const
{
	TArray<FDocInteractionCandidate> Raw;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Raw;
	}

	FDocInteractionQuery Query;
	Query.Interactor = this;
	Query.World = World;
	GetViewPoint(Query.ViewLocation, Query.ViewRotation);
	Query.IgnoredActors.Add(GetOwner());
	for (const TWeakObjectPtr<AActor>& Ignored : IgnoredActors)
	{
		if (Ignored.IsValid())
		{
			Query.IgnoredActors.Add(Ignored.Get());
		}
	}

	for (const TObjectPtr<UDocInteractionTargetProvider>& Provider : Providers)
	{
		if (Provider)
		{
			Provider->GatherCandidates(Query, Raw);
		}
	}

	// De-duplicate by interactable: keep the best score (first provider name kept for ties).
	TArray<FDocInteractionCandidate> Unique;
	for (const FDocInteractionCandidate& C : Raw)
	{
		FDocInteractionCandidate* Existing = Unique.FindByPredicate([&C](const FDocInteractionCandidate& U) { return U.Interactable == C.Interactable; });
		if (!Existing)
		{
			Unique.Add(C);
		}
		else if (C.Score > Existing->Score)
		{
			const FName KeepProvider = Existing->Provider;
			*Existing = C;
			Existing->Provider = KeepProvider;
		}
	}

	// Deterministic order: eligible, priority, score, distance, stable name.
	Unique.Sort([](const FDocInteractionCandidate& A, const FDocInteractionCandidate& B)
	{
		if (A.bEligible != B.bEligible) { return A.bEligible; }
		if (A.Priority != B.Priority) { return A.Priority > B.Priority; }
		if (!FMath::IsNearlyEqual(A.Score, B.Score)) { return A.Score > B.Score; }
		if (!FMath::IsNearlyEqual(A.Distance, B.Distance)) { return A.Distance < B.Distance; }
		const AActor* AA = A.Actor.Get();
		const AActor* BA = B.Actor.Get();
		return GetNameSafe(AA) < GetNameSafe(BA);
	});

	if (Unique.Num() > MaxCandidates)
	{
		Unique.SetNum(MaxCandidates);
	}
	return Unique;
}

void UDocInteractorComponent::RefreshDetection()
{
	UpdateFocus(QueryCandidates());
}

void UDocInteractorComponent::UpdateFocus(const TArray<FDocInteractionCandidate>& NewCandidates)
{
	Candidates = NewCandidates;

	UDocInteractableComponent* OldFocus = Focused.Get();
	const FDocInteractionCandidate* Best = Candidates.Num() > 0 && Candidates[0].bEligible ? &Candidates[0] : nullptr;
	const FDocInteractionCandidate* Current = OldFocus
		? Candidates.FindByPredicate([OldFocus](const FDocInteractionCandidate& C) { return C.Interactable.Get() == OldFocus && C.bEligible; })
		: nullptr;

	UDocInteractableComponent* NewFocus = nullptr;
	if (Best)
	{
		NewFocus = Best->Interactable.Get();
		// Hysteresis: keep the current focus unless the best is clearly better (same priority tier).
		if (Current && Current != Best && Current->Priority == Best->Priority && Best->Score < Current->Score + FocusSwitchScoreMargin)
		{
			NewFocus = OldFocus;
		}
	}

	if (NewFocus != OldFocus)
	{
		Focused = NewFocus;
		SelectedOption = 0;
		OnFocusChanged.Broadcast(NewFocus ? NewFocus->GetOwner() : nullptr, OldFocus ? OldFocus->GetOwner() : nullptr);
	}
}

AActor* UDocInteractorComponent::GetFocusedActor() const
{
	const UDocInteractableComponent* F = Focused.Get();
	return F ? F->GetOwner() : nullptr;
}

TArray<FDocInteractionOption> UDocInteractorComponent::GetFocusedOptions() const
{
	UDocInteractionSubsystem* Subsystem = UDocInteractionSubsystem::Get(this);
	UDocInteractableComponent* Target = Focused.Get();
	if (!Subsystem || !Target)
	{
		return {};
	}
	return Subsystem->GetAvailableInteractions(const_cast<UDocInteractorComponent*>(this), Target);
}

void UDocInteractorComponent::SelectNextOption()
{
	const int32 Count = GetFocusedOptions().Num();
	SelectedOption = Count > 0 ? (SelectedOption + 1) % Count : 0;
}

void UDocInteractorComponent::SelectPreviousOption()
{
	const int32 Count = GetFocusedOptions().Num();
	SelectedOption = Count > 0 ? (SelectedOption + Count - 1) % Count : 0;
}

FDocInteractionSessionInfo UDocInteractorComponent::BeginSelectedInteraction()
{
	FDocInteractionSessionInfo Info;
	UDocInteractionSubsystem* Subsystem = UDocInteractionSubsystem::Get(this);
	UDocInteractableComponent* Target = Focused.Get();
	const TArray<FDocInteractionOption> Options = GetFocusedOptions();
	if (!Subsystem || !Target || !Options.IsValidIndex(SelectedOption))
	{
		Info.State = EDocInteractionSessionState::Rejected;
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No focused interaction"));
		return Info;
	}
	return Subsystem->RequestInteraction(this, Target, Options[SelectedOption].DefinitionId);
}

void UDocInteractorComponent::EndActiveInteraction()
{
	if (UDocInteractionSubsystem* Subsystem = UDocInteractionSubsystem::Get(this))
	{
		if (ActiveSession.IsSet())
		{
			Subsystem->CompleteInteraction(ActiveSession);
		}
	}
}

FDocInteractionSessionInfo UDocInteractorComponent::RequestInteractionWithTarget(AActor* Target, FName DefinitionId)
{
	UDocInteractionSubsystem* Subsystem = UDocInteractionSubsystem::Get(this);
	UDocInteractableComponent* Interactable = Target ? Target->FindComponentByClass<UDocInteractableComponent>() : nullptr;
	if (!Subsystem || !Interactable)
	{
		FDocInteractionSessionInfo Info;
		Info.State = EDocInteractionSessionState::Rejected;
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("%s has no UDocInteractableComponent"), *GetNameSafe(Target)));
		return Info;
	}
	return Subsystem->RequestInteraction(this, Interactable, DefinitionId);
}

FDocSystemResult UDocInteractorComponent::CancelActiveInteraction()
{
	UDocInteractionSubsystem* Subsystem = UDocInteractionSubsystem::Get(this);
	if (!Subsystem || !ActiveSession.IsSet())
	{
		return FDocSystemResult::MakeNoChange(TEXT("No active session"));
	}
	return Subsystem->CancelInteraction(ActiveSession);
}

void UDocInteractorComponent::NotifySessionStarted(const FDocInteractionSessionInfo& Info)
{
	ActiveSession = Info.Handle;
	OnSessionStarted.Broadcast(Info);
}

void UDocInteractorComponent::NotifySessionProgress(const FDocInteractionSessionInfo& Info)
{
	OnSessionProgress.Broadcast(Info);
}

void UDocInteractorComponent::NotifySessionEnded(const FDocInteractionSessionInfo& Info)
{
	if (ActiveSession == Info.Handle)
	{
		ActiveSession.Invalidate();
	}
	OnSessionEnded.Broadcast(Info);
}
