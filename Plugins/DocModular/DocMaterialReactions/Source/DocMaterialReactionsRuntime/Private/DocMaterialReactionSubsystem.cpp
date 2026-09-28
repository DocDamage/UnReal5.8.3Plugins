#include "DocMaterialReactionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace DocMaterialSubsystemPrivate
{
	static TPair<FName, FName> NormalizePair(FName A, FName B)
	{
		return A.LexicalLess(B) ? TPair<FName, FName>(A, B) : TPair<FName, FName>(B, A);
	}

	static EDocResultOutcome OutcomeForReason(FName Reason)
	{
		static const FName NoAuthority(TEXT("NoAuthority"));
		static const FName NotRegistered(TEXT("NotRegistered"));
		static const FName InvalidSample(TEXT("InvalidSample"));
		static const FName StaleSequence(TEXT("StaleSequence"));
		static const FName UnknownSource(TEXT("UnknownSource"));
		static const FName UnknownReaction(TEXT("UnknownReaction"));
		static const FName GroupOccupied(TEXT("GroupOccupied"));
		static const FName NoProfile(TEXT("NoProfile"));

		if (Reason == NoAuthority) { return EDocResultOutcome::PermissionDenied; }
		if (Reason == NotRegistered || Reason == UnknownSource || Reason == UnknownReaction) { return EDocResultOutcome::NotFound; }
		if (Reason == InvalidSample) { return EDocResultOutcome::InvalidInput; }
		if (Reason == StaleSequence || Reason == GroupOccupied) { return EDocResultOutcome::Conflict; }
		if (Reason == NoProfile) { return EDocResultOutcome::InvalidConfiguration; }
		return EDocResultOutcome::Failed;
	}
}

UDocMaterialReactionSubsystem::UDocMaterialReactionSubsystem()
{
}

void UDocMaterialReactionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Objects.Empty();
	Contacts.Empty();
	LastTransferTime.Empty();
}

void UDocMaterialReactionSubsystem::Deinitialize()
{
	Objects.Empty();
	Contacts.Empty();
	LastTransferTime.Empty();
	Super::Deinitialize();
}

void UDocMaterialReactionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bAutoStep || DeltaTime <= 0.0f)
	{
		return;
	}

	const double Step = FMath::Max(0.01f, FixedStepSeconds);
	Accumulator += DeltaTime;
	int32 Steps = 0;
	while (Accumulator + UE_KINDA_SMALL_NUMBER >= Step && Steps < FMath::Max(1, MaxStepsPerTick))
	{
		StepSimulation(static_cast<float>(Step));
		Accumulator -= Step;
		++Steps;
	}
	if (Accumulator >= Step)
	{
		const double Excess = Accumulator - FMath::Fmod(Accumulator, Step);
		DroppedSeconds += Excess;
		Accumulator -= Excess;
	}
}

TStatId UDocMaterialReactionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocMaterialReactionSubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocMaterialReactionSubsystem::RegisterReactiveObject(UDocReactiveMaterialComponent* Comp)
{
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null component."));
	}

	CleanupStaleReferences();

	for (const TPair<FName, TWeakObjectPtr<UDocReactiveMaterialComponent>>& Kvp : Objects)
	{
		if (Kvp.Value.Get() == Comp)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Already registered."));
		}
	}

	if (Comp->ReactiveObjectId.IsNone())
	{
		const AActor* Owner = Comp->GetOwner();
		const FString Base = Owner ? FString::Printf(TEXT("%s.%s"), *Owner->GetFName().ToString(), *Comp->GetFName().ToString())
			: Comp->GetFName().ToString();
		Comp->ReactiveObjectId = FName(*Base);
	}

	if (const TWeakObjectPtr<UDocReactiveMaterialComponent>* Existing = Objects.Find(Comp->ReactiveObjectId))
	{
		if (Existing->IsValid())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
				FString::Printf(TEXT("ReactiveObjectId %s is already registered."), *Comp->ReactiveObjectId.ToString()));
		}
	}

	Objects.Add(Comp->ReactiveObjectId, Comp);
	Comp->SetSimulationTime(SimTime);
	Comp->SetMaxActiveReactions(PropagationBudget.MaxActiveReactionsPerObject);
	return FDocSystemResult::MakeSuccess();
}

void UDocMaterialReactionSubsystem::RegisterReactiveComponent(UDocReactiveMaterialComponent* Comp)
{
	RegisterReactiveObject(Comp);
}

FString UDocMaterialReactionSubsystem::MakePropagationSourcePrefix(FName SourceObjectId)
{
	return FString::Printf(TEXT("Prop:%s:"), *SourceObjectId.ToString());
}

void UDocMaterialReactionSubsystem::UnregisterReactiveComponent(UDocReactiveMaterialComponent* Comp)
{
	if (!Comp)
	{
		return;
	}

	FName RemovedId = NAME_None;
	for (auto It = Objects.CreateIterator(); It; ++It)
	{
		if (It->Value.Get() == Comp)
		{
			RemovedId = It->Key;
			It.RemoveCurrent();
			break;
		}
	}
	CleanupStaleReferences();

	if (RemovedId.IsNone())
	{
		return;
	}

	for (auto It = Contacts.CreateIterator(); It; ++It)
	{
		if (It->Key == RemovedId || It->Value == RemovedId)
		{
			It.RemoveCurrent();
		}
	}

	const FString KeyPrefix = RemovedId.ToString() + TEXT("|");
	const FString KeyInfix = TEXT("|") + RemovedId.ToString() + TEXT("|");
	for (auto It = LastTransferTime.CreateIterator(); It; ++It)
	{
		if (It->Key.StartsWith(KeyPrefix) || It->Key.Contains(KeyInfix))
		{
			It.RemoveCurrent();
		}
	}

	// An unloaded source stops contributing; its exposure on others is withdrawn now, not left to expire.
	const FString Prefix = MakePropagationSourcePrefix(RemovedId);
	for (const TPair<FName, TWeakObjectPtr<UDocReactiveMaterialComponent>>& Kvp : Objects)
	{
		if (UDocReactiveMaterialComponent* Other = Kvp.Value.Get())
		{
			Other->RemoveExposureSourcesWithPrefix(Prefix);
		}
	}
}

UDocReactiveMaterialComponent* UDocMaterialReactionSubsystem::FindReactiveObject(FName ObjectId) const
{
	if (const TWeakObjectPtr<UDocReactiveMaterialComponent>* Found = Objects.Find(ObjectId))
	{
		UDocReactiveMaterialComponent* Comp = Found->Get();
		if (Comp && Comp->IsRegistered())
		{
			return Comp;
		}
	}
	return nullptr;
}

int32 UDocMaterialReactionSubsystem::GetRegisteredComponentCount() const
{
	int32 Count = 0;
	for (const TPair<FName, TWeakObjectPtr<UDocReactiveMaterialComponent>>& Kvp : Objects)
	{
		if (Kvp.Value.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

void UDocMaterialReactionSubsystem::CleanupStaleReferences()
{
	for (auto It = Objects.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

TArray<FName> UDocMaterialReactionSubsystem::GetSortedIds() const
{
	TArray<FName> Ids;
	for (const TPair<FName, TWeakObjectPtr<UDocReactiveMaterialComponent>>& Kvp : Objects)
	{
		if (Kvp.Value.IsValid())
		{
			Ids.Add(Kvp.Key);
		}
	}
	Ids.Sort(FNameLexicalLess());
	return Ids;
}

// ---------------------------------------------------------------------------------------------
// Gameplay API
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocMaterialReactionSubsystem::FromReject(const UDocReactiveMaterialComponent* Comp)
{
	const FName Reason = Comp ? Comp->GetLastRejectReason() : FName(TEXT("NotFound"));
	return FDocSystemResult::MakeFailure(DocMaterialSubsystemPrivate::OutcomeForReason(Reason), Reason.ToString());
}

FDocSystemResult UDocMaterialReactionSubsystem::AddExposureSource(FName ObjectId, const FDocExposureSample& Sample)
{
	UDocReactiveMaterialComponent* Comp = FindReactiveObject(ObjectId);
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No loaded reactive object %s."), *ObjectId.ToString()));
	}
	return Comp->AddExposureSource(Sample) ? FDocSystemResult::MakeSuccess() : FromReject(Comp);
}

FDocSystemResult UDocMaterialReactionSubsystem::UpdateExposureSource(FName ObjectId, FName SourceId, float NewIntensity)
{
	UDocReactiveMaterialComponent* Comp = FindReactiveObject(ObjectId);
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No loaded reactive object %s."), *ObjectId.ToString()));
	}
	return Comp->UpdateExposureSource(SourceId, NewIntensity) ? FDocSystemResult::MakeSuccess() : FromReject(Comp);
}

FDocSystemResult UDocMaterialReactionSubsystem::RemoveExposureSource(FName ObjectId, FName SourceId)
{
	UDocReactiveMaterialComponent* Comp = FindReactiveObject(ObjectId);
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No loaded reactive object %s."), *ObjectId.ToString()));
	}
	return Comp->RemoveExposureSource(SourceId) ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("Source not present."));
}

FDocSystemResult UDocMaterialReactionSubsystem::RequestReaction(FName ObjectId, FName ReactionId, FName Cause)
{
	UDocReactiveMaterialComponent* Comp = FindReactiveObject(ObjectId);
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No loaded reactive object %s."), *ObjectId.ToString()));
	}
	const bool bWasActive = Comp->IsReactionActive(ReactionId);
	if (!Comp->RequestManualReaction(ReactionId, Cause))
	{
		return FromReject(Comp);
	}
	return bWasActive ? FDocSystemResult::MakeNoChange(TEXT("Reaction already active.")) : FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocMaterialReactionSubsystem::SuppressReaction(FName ObjectId, FName ReactionId, FName SourceId, float DurationSeconds)
{
	UDocReactiveMaterialComponent* Comp = FindReactiveObject(ObjectId);
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No loaded reactive object %s."), *ObjectId.ToString()));
	}
	if (ReactionId.IsNone() || SourceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Suppression needs a reaction id and a source id."));
	}
	Comp->SuppressReaction(ReactionId, SourceId, DurationSeconds);
	return FDocSystemResult::MakeSuccess();
}

bool UDocMaterialReactionSubsystem::QueryMaterialState(FName ObjectId, FDocMaterialState& OutState) const
{
	if (const UDocReactiveMaterialComponent* Comp = FindReactiveObject(ObjectId))
	{
		OutState = Comp->CurrentState;
		return true;
	}
	return false;
}

TArray<FDocReactionInstance> UDocMaterialReactionSubsystem::QueryActiveReactions(FName ObjectId) const
{
	if (const UDocReactiveMaterialComponent* Comp = FindReactiveObject(ObjectId))
	{
		return Comp->GetActiveReactions();
	}
	return TArray<FDocReactionInstance>();
}

// ---------------------------------------------------------------------------------------------
// Contacts, budget, simulation
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocMaterialReactionSubsystem::AddContact(FName ObjectA, FName ObjectB)
{
	if (ObjectA.IsNone() || ObjectB.IsNone() || ObjectA == ObjectB)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("A contact needs two different object ids."));
	}
	if (!FindReactiveObject(ObjectA) || !FindReactiveObject(ObjectB))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Both contact endpoints must be loaded and registered."));
	}
	const TPair<FName, FName> Key = DocMaterialSubsystemPrivate::NormalizePair(ObjectA, ObjectB);
	if (Contacts.Contains(Key))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Contact already exists."));
	}
	Contacts.Add(Key);
	return FDocSystemResult::MakeSuccess();
}

bool UDocMaterialReactionSubsystem::RemoveContact(FName ObjectA, FName ObjectB)
{
	return Contacts.Remove(DocMaterialSubsystemPrivate::NormalizePair(ObjectA, ObjectB)) > 0;
}

void UDocMaterialReactionSubsystem::SetPropagationBudget(const FDocPropagationBudget& NewBudget)
{
	PropagationBudget = NewBudget;
	PropagationBudget.MaxNeighborsPerSource = FMath::Max(0, PropagationBudget.MaxNeighborsPerSource);
	PropagationBudget.MaxQueuedTransfersPerStep = FMath::Max(0, PropagationBudget.MaxQueuedTransfersPerStep);
	PropagationBudget.MaxWorkPerStep = FMath::Max(0, PropagationBudget.MaxWorkPerStep);
	PropagationBudget.MaxActiveReactionsPerObject = FMath::Max(1, PropagationBudget.MaxActiveReactionsPerObject);

	for (const TPair<FName, TWeakObjectPtr<UDocReactiveMaterialComponent>>& Kvp : Objects)
	{
		if (UDocReactiveMaterialComponent* Comp = Kvp.Value.Get())
		{
			Comp->SetMaxActiveReactions(PropagationBudget.MaxActiveReactionsPerObject);
		}
	}
}

void UDocMaterialReactionSubsystem::StepSimulation(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	CleanupStaleReferences();
	SimTime += DeltaTime;
	++StepCounter;

	// Each object steps against the same boundary time, in stable id order.
	const TArray<FName> Ids = GetSortedIds();
	for (const FName& Id : Ids)
	{
		if (UDocReactiveMaterialComponent* Comp = FindReactiveObject(Id))
		{
			Comp->StepSimulation(DeltaTime, SimTime);
		}
	}

	ProcessPropagation(DeltaTime);
}

void UDocMaterialReactionSubsystem::ProcessPropagation(float DeltaTime)
{
	FDocPropagationStats Stats;
	const TArray<FName> Ids = GetSortedIds();
	const int32 Count = Ids.Num();
	if (Count == 0)
	{
		LastStats = Stats;
		PropagationCursor = 0;
		return;
	}

	const int32 MaxWork = PropagationBudget.MaxWorkPerStep;
	const int32 MaxNeighbors = PropagationBudget.MaxNeighborsPerSource;
	const int32 MaxTransfers = PropagationBudget.MaxQueuedTransfersPerStep;
	const int32 Start = PropagationCursor % Count;

	int32 Work = 0;
	int32 Transfers = 0;
	bool bBudgetOut = false;
	int32 NextCursor = 0;
	TSet<FString> StepKeys;

	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		const int32 Index = (Start + Offset) % Count;
		const FName SourceId = Ids[Index];
		UDocReactiveMaterialComponent* Source = FindReactiveObject(SourceId);
		if (!Source || !Source->MaterialProfile || !Source->GetOwner())
		{
			continue;
		}

		// Active propagating reactions on this source, in evaluation order.
		TArray<const UDocMaterialReactionDefinition*> Propagating;
		for (const UDocMaterialReactionDefinition* Def : Source->MaterialProfile->GetEvaluationOrder())
		{
			if (Def->bCanPropagate && Source->IsReactionActive(Def->ReactionId))
			{
				Propagating.Add(Def);
			}
		}
		if (Propagating.Num() == 0)
		{
			continue;
		}

		if (bBudgetOut)
		{
			++Stats.SourcesDeferred;
			continue;
		}

		const FVector SourceLocation = Source->GetOwner()->GetActorLocation();

		// Candidates: explicit contacts first (id order), then registered neighbors by distance, then id.
		TArray<FName> ContactIds;
		for (const TPair<FName, FName>& Pair : Contacts)
		{
			if (Pair.Key == SourceId) { ContactIds.Add(Pair.Value); }
			else if (Pair.Value == SourceId) { ContactIds.Add(Pair.Key); }
		}
		ContactIds.Sort(FNameLexicalLess());

		struct FCandidate { FName Id; double DistSq; bool bContact; };
		TArray<FCandidate> Candidates;
		bool bScanComplete = true;

		for (const FName& Id : ContactIds)
		{
			if (Work >= MaxWork) { bScanComplete = false; break; }
			++Work;
			++Stats.CandidatesEvaluated;
			Candidates.Add({ Id, -1.0, true });
		}

		float MaxRadius = 0.0f;
		for (const UDocMaterialReactionDefinition* Def : Propagating)
		{
			MaxRadius = FMath::Max(MaxRadius, Def->PropagationRadius);
		}

		if (bScanComplete)
		{
			for (const FName& Id : Ids)
			{
				if (Id == SourceId || ContactIds.Contains(Id))
				{
					continue;
				}
				if (Work >= MaxWork) { bScanComplete = false; break; }
				++Work;
				++Stats.CandidatesEvaluated;
				const UDocReactiveMaterialComponent* Target = FindReactiveObject(Id);
				if (!Target || !Target->GetOwner())
				{
					continue;
				}
				const double DistSq = FVector::DistSquared(SourceLocation, Target->GetOwner()->GetActorLocation());
				if (DistSq <= FMath::Square(static_cast<double>(MaxRadius)))
				{
					Candidates.Add({ Id, DistSq, false });
				}
			}
		}

		Candidates.Sort([](const FCandidate& A, const FCandidate& B)
		{
			if (A.bContact != B.bContact) { return A.bContact; }
			if (A.DistSq != B.DistSq) { return A.DistSq < B.DistSq; }
			return A.Id.LexicalLess(B.Id);
		});

		const FString Prefix = MakePropagationSourcePrefix(SourceId);
		for (const UDocMaterialReactionDefinition* Def : Propagating)
		{
			FDocReactionInstance Inst;
			Source->GetReactionInstance(Def->ReactionId, Inst);
			if (Inst.Generation >= Def->MaxPropagationGeneration)
			{
				++Stats.TransfersRejectedGeneration;
				continue;
			}

			int32 Neighbors = 0;
			for (const FCandidate& Candidate : Candidates)
			{
				if (Neighbors >= MaxNeighbors)
				{
					break;
				}
				if (!Candidate.bContact && Candidate.DistSq > FMath::Square(static_cast<double>(Def->PropagationRadius)))
				{
					continue;
				}
				if (Transfers >= MaxTransfers)
				{
					bScanComplete = false;
					break;
				}

				const FString Key = FString::Printf(TEXT("%s|%s|%s"), *SourceId.ToString(), *Candidate.Id.ToString(), *Def->ReactionId.ToString());
				if (StepKeys.Contains(Key))
				{
					++Stats.TransfersDeduplicated;
					continue;
				}
				StepKeys.Add(Key);

				UDocReactiveMaterialComponent* Target = FindReactiveObject(Candidate.Id);
				if (!Target)
				{
					++Stats.TransfersRejectedByTarget;
					continue;
				}

				if (Def->PropagationCooldownSeconds > 0.0f)
				{
					if (const double* Last = LastTransferTime.Find(Key))
					{
						if (SimTime - *Last < Def->PropagationCooldownSeconds)
						{
							++Stats.TransfersSkippedCooldown;
							++Neighbors; // the previous transfer is still live and holds this slot
							continue;
						}
					}
				}

				FDocExposureSample Sample;
				Sample.SourceId = FName(*(Prefix + Def->ReactionId.ToString()));
				Sample.Channel = Def->PropagationExposureChannel;
				Sample.Intensity = Def->PropagationIntensity;
				Sample.Generation = Inst.Generation + 1;
				Sample.Sequence = StepCounter;
				Sample.ExpiryTimestamp = SimTime + FMath::Max(2.0 * DeltaTime, static_cast<double>(Def->PropagationCooldownSeconds) + DeltaTime);

				if (Target->AddExposureSource(Sample))
				{
					++Stats.TransfersIssued;
					++Transfers;
					++Neighbors;
					LastTransferTime.Add(Key, SimTime);
				}
				else
				{
					++Stats.TransfersRejectedByTarget;
				}
			}
		}

		if (!bScanComplete)
		{
			// This source did not get its full turn; it goes first next step.
			bBudgetOut = true;
			NextCursor = Index;
			++Stats.SourcesDeferred;
		}
		else
		{
			++Stats.SourcesProcessed;
		}
	}

	PropagationCursor = bBudgetOut ? NextCursor : 0;
	LastStats = Stats;
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------

bool UDocMaterialReactionSubsystem::CaptureMaterialState(const UDocReactiveMaterialComponent* Comp, FDocMaterialSnapshot& OutSnapshot) const
{
	if (!Comp)
	{
		return false;
	}
	OutSnapshot = Comp->CaptureSnapshot();
	return true;
}

FDocSystemResult UDocMaterialReactionSubsystem::StageRestore(UDocReactiveMaterialComponent* Comp, const FDocMaterialSnapshot& Snapshot)
{
	if (!Comp)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null component."));
	}
	FString Error;
	if (!Comp->RestoreSnapshot(Snapshot, Error))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, Error);
	}
	return FDocSystemResult::MakeSuccess();
}
