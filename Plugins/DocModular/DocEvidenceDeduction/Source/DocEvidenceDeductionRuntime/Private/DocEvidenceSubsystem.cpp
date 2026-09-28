#include "DocEvidenceSubsystem.h"
#include "DocEvidenceDeductionLog.h"

namespace DocEvidencePrivate
{
	static TArray<FName> SortedKeys(const TMap<FName, FDocEvidenceObservation>& Map)
	{
		TArray<FName> Keys;
		Map.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		return Keys;
	}
}

void UDocEvidenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocEvidenceSubsystem::Deinitialize()
{
	Owners.Empty();
	EvidenceDefinitions.Empty();
	HypothesisDefinitions.Empty();
	Super::Deinitialize();
}

UDocEvidenceSubsystem::FOwnerState& UDocEvidenceSubsystem::GetOrAddOwner(FName OwnerId)
{
	return Owners.FindOrAdd(OwnerId);
}

const UDocEvidenceSubsystem::FOwnerState* UDocEvidenceSubsystem::FindOwner(FName OwnerId) const
{
	return Owners.Find(OwnerId);
}

// ---------------------------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocEvidenceSubsystem::RegisterEvidenceDefinition(UDocEvidenceDefinition* Def)
{
	if (!Def || Def->EvidenceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid evidence definition or EvidenceId"));
	}
	if (const TStrongObjectPtr<UDocEvidenceDefinition>* Existing = EvidenceDefinitions.Find(Def->EvidenceId))
	{
		if (Existing->Get() == Def)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Already registered"));
		}
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("EvidenceId %s already registered"), *Def->EvidenceId.ToString()));
	}
	EvidenceDefinitions.Add(Def->EvidenceId, TStrongObjectPtr<UDocEvidenceDefinition>(Def));
	return FDocSystemResult::MakeSuccess();
}

bool UDocEvidenceSubsystem::WouldCreateCycle(const UDocHypothesisDefinition& Def, FString& OutPath) const
{
	// Iterative DFS with explicit path; the candidate definition replaces any registered one with the same id.
	struct FFrame { FName Id; int32 Next; };
	TArray<FFrame> Stack;
	TArray<FName> Path;
	Stack.Add({ Def.HypothesisId, 0 });
	Path.Add(Def.HypothesisId);
	int32 Visits = 0;

	auto DepsOf = [&](FName Id) -> const TArray<FName>*
	{
		if (Id == Def.HypothesisId)
		{
			return &Def.DependentHypothesisIds;
		}
		const TStrongObjectPtr<UDocHypothesisDefinition>* Found = HypothesisDefinitions.Find(Id);
		return (Found && Found->IsValid()) ? &(*Found)->DependentHypothesisIds : nullptr;
	};

	while (Stack.Num() > 0)
	{
		if (++Visits > 4096)
		{
			OutPath = TEXT("inference graph too large");
			return true;
		}
		FFrame& Top = Stack.Last();
		const TArray<FName>* Deps = DepsOf(Top.Id);
		if (!Deps || Top.Next >= Deps->Num())
		{
			Stack.Pop();
			Path.Pop();
			continue;
		}
		const FName Next = (*Deps)[Top.Next++];
		if (Path.Contains(Next))
		{
			Path.Add(Next);
			TArray<FString> Parts;
			for (const FName& P : Path) { Parts.Add(P.ToString()); }
			OutPath = FString::Join(Parts, TEXT(" -> "));
			return true;
		}
		if (Path.Num() >= MaxInferenceDepth)
		{
			OutPath = FString::Printf(TEXT("inference depth exceeds %d at %s"), MaxInferenceDepth, *Next.ToString());
			return true;
		}
		Stack.Add({ Next, 0 });
		Path.Add(Next);
	}
	return false;
}

FDocSystemResult UDocEvidenceSubsystem::RegisterHypothesisDefinition(UDocHypothesisDefinition* Def)
{
	if (!Def || Def->HypothesisId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid hypothesis definition or HypothesisId"));
	}
	if (Def->DependentHypothesisIds.Num() > MaxDependenciesPerHypothesis)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
			FString::Printf(TEXT("Hypothesis %s has more than %d dependencies"), *Def->HypothesisId.ToString(), MaxDependenciesPerHypothesis));
	}
	for (const FDocEvidenceSet& Set : Def->AlternativeRequiredEvidenceSets)
	{
		if (Set.RequiredEvidenceIds.Num() == 0)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("An alternative evidence set is empty"));
		}
	}
	FString CyclePath;
	if (WouldCreateCycle(*Def, CyclePath))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Inference graph rejected: %s"), *CyclePath));
	}
	if (const TStrongObjectPtr<UDocHypothesisDefinition>* Existing = HypothesisDefinitions.Find(Def->HypothesisId))
	{
		if (Existing->Get() == Def)
		{
			return FDocSystemResult::MakeNoChange(TEXT("Already registered"));
		}
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("HypothesisId %s already registered"), *Def->HypothesisId.ToString()));
	}
	HypothesisDefinitions.Add(Def->HypothesisId, TStrongObjectPtr<UDocHypothesisDefinition>(Def));
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Audience and evaluation
// ---------------------------------------------------------------------------------------------

bool UDocEvidenceSubsystem::IsVisible(const FDocEvidenceObservation& Obs, EAudience Audience) const
{
	return Audience == EAudience::Debug || !Obs.bIsHiddenFromPlayer;
}

bool UDocEvidenceSubsystem::IsEstablished(const FDocEvidenceObservation& Obs, EAudience Audience) const
{
	return Obs.Status == EDocEvidenceStatus::EstablishedFact && !Obs.bIsRetracted && IsVisible(Obs, Audience);
}

TArray<FDocContradiction> UDocEvidenceSubsystem::FindContradictions(const FOwnerState& State, EAudience Audience) const
{
	TArray<const FDocEvidenceObservation*> Facts;
	for (const FName& Key : DocEvidencePrivate::SortedKeys(State.Observations))
	{
		const FDocEvidenceObservation& Obs = State.Observations[Key];
		if (IsEstablished(Obs, Audience) && !Obs.SubjectId.IsNone())
		{
			Facts.Add(&Obs);
		}
	}

	auto Make = [](EDocContradictionKind Kind, FName Subject, FName A, FName B)
	{
		FDocContradiction C;
		C.Kind = Kind;
		C.SubjectId = Subject;
		C.ObservationA = A;
		C.ObservationB = B;
		return C;
	};
	TArray<FDocContradiction> Result;
	for (int32 i = 0; i < Facts.Num(); ++i)
	{
		for (int32 j = i + 1; j < Facts.Num(); ++j)
		{
			const FDocEvidenceObservation& A = *Facts[i];
			const FDocEvidenceObservation& B = *Facts[j];
			if (A.SubjectId != B.SubjectId)
			{
				continue;
			}
			if (!A.ClaimValue.IsNone() && !B.ClaimValue.IsNone() && A.ClaimValue != B.ClaimValue)
			{
				Result.Add(Make(EDocContradictionKind::MutuallyExclusiveStatement, A.SubjectId, A.ObservationId, B.ObservationId));
			}
			if (!A.EventFrameId.IsNone() && A.EventFrameId == B.EventFrameId
				&& A.EventTimestampPrecisionSeconds >= 0.0 && B.EventTimestampPrecisionSeconds >= 0.0
				&& FMath::Abs(A.EventTimestampSeconds - B.EventTimestampSeconds) > A.EventTimestampPrecisionSeconds + B.EventTimestampPrecisionSeconds)
			{
				Result.Add(Make(EDocContradictionKind::IncompatibleTimestamp, A.SubjectId, A.ObservationId, B.ObservationId));
			}
		}
	}
	return Result;
}

void UDocEvidenceSubsystem::EvaluateInternal(const FOwnerState& State, FName HypothesisId, EAudience Audience, int32 Depth, FDocDeductionResult& Out) const
{
	Out = FDocDeductionResult();
	Out.HypothesisId = HypothesisId;
	Out.SnapshotRevision = State.Revision;

	const TStrongObjectPtr<UDocHypothesisDefinition>* DefPtr = HypothesisDefinitions.Find(HypothesisId);
	if (!DefPtr || !DefPtr->IsValid() || Depth > MaxInferenceDepth)
	{
		Out.Evaluation = EDocHypothesisEvaluation::Unavailable;
		Out.Summary = TEXT("Hypothesis unavailable");
		return;
	}
	const UDocHypothesisDefinition& Def = *DefPtr->Get();

	auto EstablishedObsFor = [&](FName EvidenceId, TArray<const FDocEvidenceObservation*>& OutObs)
	{
		for (const FName& Key : DocEvidencePrivate::SortedKeys(State.Observations))
		{
			const FDocEvidenceObservation& Obs = State.Observations[Key];
			if (Obs.EvidenceId == EvidenceId && IsEstablished(Obs, Audience))
			{
				OutObs.Add(&Obs);
			}
		}
	};

	bool bContradicted = false;
	TArray<FString> Reasons;

	// 1. Authored disqualifiers (established, visible facts only; undiscovered never disqualifies).
	for (const FName& DisqId : Def.DisqualifyingEvidenceIds)
	{
		TArray<const FDocEvidenceObservation*> Found;
		EstablishedObsFor(DisqId, Found);
		if (Found.Num() > 0)
		{
			bContradicted = true;
			Out.ContradictingEvidenceIds.AddUnique(DisqId);
		}
	}

	// 2. Dependencies (acyclic by registration).
	bool bDepsSupported = true;
	for (const FName& DepId : Def.DependentHypothesisIds)
	{
		FDocDeductionResult Dep;
		EvaluateInternal(State, DepId, Audience, Depth + 1, Dep);
		if (Dep.Evaluation == EDocHypothesisEvaluation::Contradicted)
		{
			bContradicted = true;
			Reasons.Add(FString::Printf(TEXT("depends on contradicted %s"), *DepId.ToString()));
		}
		if (Dep.Evaluation != EDocHypothesisEvaluation::Supported)
		{
			bDepsSupported = false;
		}
		if (Dep.Evaluation == EDocHypothesisEvaluation::Unavailable)
		{
			Out.Evaluation = EDocHypothesisEvaluation::Unavailable;
			Out.Summary = FString::Printf(TEXT("Dependency %s unavailable"), *DepId.ToString());
			return;
		}
	}

	// 3. Alternative evidence sets: the first fully established set (authored order) supports.
	bool bSetSatisfied = Def.AlternativeRequiredEvidenceSets.Num() == 0 && Def.DependentHypothesisIds.Num() > 0;
	TArray<const FDocEvidenceObservation*> SupportingObs;
	int32 BestMissing = MAX_int32;
	TArray<FName> BestMissingIds;
	for (int32 SetIndex = 0; SetIndex < Def.AlternativeRequiredEvidenceSets.Num() && !bSetSatisfied; ++SetIndex)
	{
		const FDocEvidenceSet& Set = Def.AlternativeRequiredEvidenceSets[SetIndex];
		TArray<FName> Missing;
		TArray<const FDocEvidenceObservation*> SetObs;
		for (const FName& ReqId : Set.RequiredEvidenceIds)
		{
			TArray<const FDocEvidenceObservation*> Found;
			EstablishedObsFor(ReqId, Found);
			if (Found.Num() == 0)
			{
				Missing.Add(ReqId);
			}
			SetObs.Append(Found);
		}
		if (Missing.Num() == 0)
		{
			bSetSatisfied = true;
			Out.SatisfiedSetIndex = SetIndex;
			Out.SupportingEvidenceIds = Set.RequiredEvidenceIds;
			SupportingObs = SetObs;
		}
		else if (Missing.Num() < BestMissing)
		{
			BestMissing = Missing.Num();
			BestMissingIds = Missing;
		}
	}

	// 4. Typed contradictions touching the supporting observations.
	if (bSetSatisfied && Def.bContradictionsDisqualify)
	{
		for (const FDocContradiction& C : FindContradictions(State, Audience))
		{
			const bool bInvolved = SupportingObs.ContainsByPredicate([&C](const FDocEvidenceObservation* O)
			{
				return O->ObservationId == C.ObservationA || O->ObservationId == C.ObservationB;
			});
			if (bInvolved)
			{
				bContradicted = true;
				Out.Contradictions.Add(C);
			}
		}
	}

	if (bContradicted)
	{
		Out.Evaluation = EDocHypothesisEvaluation::Contradicted;
		Out.Summary = Reasons.Num() > 0 ? FString::Join(Reasons, TEXT("; ")) : TEXT("Contradicted by established evidence");
	}
	else if (bSetSatisfied && bDepsSupported)
	{
		Out.Evaluation = EDocHypothesisEvaluation::Supported;
		Out.ConfidenceScore = Def.SupportedScore;
		Out.Summary = TEXT("Supported by established observations");
	}
	else
	{
		Out.Evaluation = EDocHypothesisEvaluation::Inconclusive;
		Out.Summary = TEXT("Evidence is insufficient");
		if (!bSetSatisfied && BestMissing != MAX_int32)
		{
			Out.MissingEvidenceIds = BestMissingIds;
			Out.MissingEvidenceCount = BestMissingIds.Num();
		}
	}

	for (const TPair<FName, FDocConclusionRecord>& Kvp : State.Conclusions)
	{
		if (Kvp.Value.HypothesisId == HypothesisId && Kvp.Value.bIsChallenged)
		{
			Out.bIsChallenged = true;
		}
	}
}

void UDocEvidenceSubsystem::RedactForPlayer(FDocDeductionResult& Result) const
{
	auto IsSecret = [this](FName EvidenceId)
	{
		const TStrongObjectPtr<UDocEvidenceDefinition>* Def = EvidenceDefinitions.Find(EvidenceId);
		return Def && Def->IsValid() && (*Def)->bIsSecret;
	};
	const int32 Before = Result.SupportingEvidenceIds.Num() + Result.ContradictingEvidenceIds.Num();
	Result.SupportingEvidenceIds.RemoveAll(IsSecret);
	Result.ContradictingEvidenceIds.RemoveAll(IsSecret);
	const bool bHadMissing = Result.MissingEvidenceIds.Num() > 0;
	Result.MissingEvidenceIds.Reset(); // undiscovered evidence identities are never sent to the player
	Result.bRedacted = bHadMissing || Before != Result.SupportingEvidenceIds.Num() + Result.ContradictingEvidenceIds.Num();
}

// ---------------------------------------------------------------------------------------------
// Observations
// ---------------------------------------------------------------------------------------------

void UDocEvidenceSubsystem::PushHistory(FDocEvidenceObservation& Obs, FName Reason)
{
	FDocObservationRevision R;
	R.Revision = Obs.Revision;
	R.Status = Obs.Status;
	R.ClaimContent = Obs.ClaimContent;
	R.ClaimValue = Obs.ClaimValue;
	R.EventTimestampSeconds = Obs.EventTimestampSeconds;
	R.bRetracted = Obs.bIsRetracted;
	R.Reason = Reason;
	Obs.History.Add(R);
	if (Obs.History.Num() > MaxObservationHistory)
	{
		Obs.History.RemoveAt(0, Obs.History.Num() - MaxObservationHistory);
	}
	++Obs.Revision;
}

bool UDocEvidenceSubsystem::DependsOnEvidence(FName HypothesisId, FName EvidenceId, int32 Depth) const
{
	const TStrongObjectPtr<UDocHypothesisDefinition>* DefPtr = HypothesisDefinitions.Find(HypothesisId);
	if (!DefPtr || !DefPtr->IsValid() || Depth > MaxInferenceDepth)
	{
		return false;
	}
	const UDocHypothesisDefinition& Def = *DefPtr->Get();
	if (Def.bContradictionsDisqualify || Def.DisqualifyingEvidenceIds.Contains(EvidenceId))
	{
		return true; // contradictions can arise from any subject observation
	}
	for (const FDocEvidenceSet& Set : Def.AlternativeRequiredEvidenceSets)
	{
		if (Set.RequiredEvidenceIds.Contains(EvidenceId))
		{
			return true;
		}
	}
	for (const FName& Dep : Def.DependentHypothesisIds)
	{
		if (DependsOnEvidence(Dep, EvidenceId, Depth + 1))
		{
			return true;
		}
	}
	return false;
}

void UDocEvidenceSubsystem::ReevaluateConclusions(FName OwnerId, FOwnerState& State, FName ChangedEvidenceId, const FString& Reason)
{
	TArray<FName> Keys;
	State.Conclusions.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());

	TArray<TPair<FName, FString>> Challenged;
	for (const FName& Key : Keys)
	{
		FDocConclusionRecord& Record = State.Conclusions[Key];
		if (Record.bIsChallenged || !DependsOnEvidence(Record.HypothesisId, ChangedEvidenceId))
		{
			continue;
		}
		FDocDeductionResult Eval;
		EvaluateInternal(State, Record.HypothesisId, EAudience::Player, 0, Eval);
		if (Eval.Evaluation != EDocHypothesisEvaluation::Supported)
		{
			Record.bIsChallenged = true;
			Record.ChallengeReason = FString::Printf(TEXT("%s (evidence %s); now %s"), *Reason, *ChangedEvidenceId.ToString(),
				*UEnum::GetValueAsString(Eval.Evaluation));
			Challenged.Add(TPair<FName, FString>(Record.ConclusionId, Record.ChallengeReason));
		}
	}
	// History is never rewritten: the record stays, flagged Challenged. Events fire after all updates.
	for (const TPair<FName, FString>& C : Challenged)
	{
		OnConclusionChallenged.Broadcast(C.Key, C.Value);
		OnConclusionChallengedNative.Broadcast(C.Key, C.Value);
	}
}

FDocSystemResult UDocEvidenceSubsystem::AddObservation(const FDocEvidenceObservation& Observation, FName OwnerId)
{
	if (Observation.ObservationId.IsNone() || Observation.EvidenceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("ObservationId or EvidenceId is None"));
	}
	if (!EvidenceDefinitions.Contains(Observation.EvidenceId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Evidence %s is not registered"), *Observation.EvidenceId.ToString()));
	}
	if (!FMath::IsFinite(Observation.EventTimestampSeconds) || !FMath::IsFinite(Observation.ObservationTimestampSeconds)
		|| !FMath::IsFinite(Observation.EventTimestampPrecisionSeconds))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Timestamps must be finite"));
	}

	FOwnerState& State = GetOrAddOwner(OwnerId);
	if (const FDocEvidenceObservation* Existing = State.Observations.Find(Observation.ObservationId))
	{
		const bool bSame = Existing->EvidenceId == Observation.EvidenceId && Existing->SourceEntityId == Observation.SourceEntityId
			&& Existing->ProvenanceKey == Observation.ProvenanceKey;
		return bSame ? FDocSystemResult::MakeNoChange(TEXT("Observation already recorded"))
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("ObservationId already used by different provenance"));
	}
	if (!Observation.ProvenanceKey.IsNone())
	{
		for (const TPair<FName, FDocEvidenceObservation>& Kvp : State.Observations)
		{
			if (Kvp.Value.ProvenanceKey == Observation.ProvenanceKey && Kvp.Value.EvidenceId == Observation.EvidenceId)
			{
				return FDocSystemResult::MakeNoChange(TEXT("Same provenance already recorded (deduplicated)"));
			}
		}
	}

	FDocEvidenceObservation Stored = Observation;
	Stored.Revision = 1;
	Stored.bIsRetracted = false;
	Stored.History.Reset();
	State.Observations.Add(Stored.ObservationId, Stored);
	++State.Revision;

	OnObservationAdded.Broadcast(Stored);
	OnObservationAddedNative.Broadcast(Stored);
	ReevaluateConclusions(OwnerId, State, Stored.EvidenceId, TEXT("New observation"));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocEvidenceSubsystem::ReviseObservation(FName ObservationId, const FString& NewClaim, EDocEvidenceStatus NewStatus, double NewTimestamp, FName OwnerId)
{
	FOwnerState* State = Owners.Find(OwnerId);
	FDocEvidenceObservation* Obs = State ? State->Observations.Find(ObservationId) : nullptr;
	if (!Obs)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Observation not found"));
	}
	if (Obs->bIsRetracted)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Cannot revise retracted observation"));
	}
	if (NewStatus == EDocEvidenceStatus::EstablishedFact && Obs->Status == EDocEvidenceStatus::Claim)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("A revision cannot turn a claim into a fact; use EstablishClaim"));
	}
	if (!FMath::IsFinite(NewTimestamp))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Timestamp must be finite"));
	}

	PushHistory(*Obs, TEXT("Revised"));
	Obs->ClaimContent = NewClaim;
	Obs->Status = NewStatus;
	if (NewTimestamp >= 0.0)
	{
		Obs->EventTimestampSeconds = NewTimestamp;
	}
	++State->Revision;

	const FDocEvidenceObservation Committed = *Obs;
	OnObservationRevised.Broadcast(Committed);
	OnObservationRevisedNative.Broadcast(Committed);
	ReevaluateConclusions(OwnerId, *State, Committed.EvidenceId, TEXT("Observation revised"));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocEvidenceSubsystem::EstablishClaim(FName ObservationId, FName CorroboratingSourceId, FName OwnerId)
{
	FOwnerState* State = Owners.Find(OwnerId);
	FDocEvidenceObservation* Obs = State ? State->Observations.Find(ObservationId) : nullptr;
	if (!Obs)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Observation not found"));
	}
	if (CorroboratingSourceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Establishing a claim requires a corroborating source"));
	}
	if (Obs->bIsRetracted)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Cannot establish a retracted observation"));
	}
	if (Obs->Status == EDocEvidenceStatus::EstablishedFact)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already established"));
	}
	PushHistory(*Obs, FName(*FString::Printf(TEXT("EstablishedBy:%s"), *CorroboratingSourceId.ToString())));
	Obs->Status = EDocEvidenceStatus::EstablishedFact;
	++State->Revision;

	const FDocEvidenceObservation Committed = *Obs;
	OnObservationRevised.Broadcast(Committed);
	OnObservationRevisedNative.Broadcast(Committed);
	ReevaluateConclusions(OwnerId, *State, Committed.EvidenceId, TEXT("Claim established"));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocEvidenceSubsystem::RetractObservation(FName ObservationId, FName OwnerId)
{
	FOwnerState* State = Owners.Find(OwnerId);
	FDocEvidenceObservation* Obs = State ? State->Observations.Find(ObservationId) : nullptr;
	if (!Obs)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Observation not found"));
	}
	if (Obs->bIsRetracted)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already retracted"));
	}
	PushHistory(*Obs, TEXT("Retracted"));
	Obs->bIsRetracted = true;
	++State->Revision;

	const FName EvidenceId = Obs->EvidenceId;
	OnObservationRetracted.Broadcast(ObservationId);
	OnObservationRetractedNative.Broadcast(ObservationId);
	ReevaluateConclusions(OwnerId, *State, EvidenceId, TEXT("Observation retracted"));
	return FDocSystemResult::MakeSuccess();
}

bool UDocEvidenceSubsystem::HasObservation(FName ObservationId, FName OwnerId) const
{
	const FOwnerState* State = FindOwner(OwnerId);
	return State && State->Observations.Contains(ObservationId);
}

bool UDocEvidenceSubsystem::GetObservation(FName ObservationId, FDocEvidenceObservation& OutObs, FName OwnerId) const
{
	const FOwnerState* State = FindOwner(OwnerId);
	if (const FDocEvidenceObservation* Obs = State ? State->Observations.Find(ObservationId) : nullptr)
	{
		OutObs = *Obs;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Links (interpretation only; never changes evaluation or the evidence revision)
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocEvidenceSubsystem::AddInterpretationLink(const FDocEvidenceLink& Link, FName OwnerId)
{
	if (Link.LinkId.IsNone() || Link.SourceEvidenceId.IsNone() || Link.TargetEvidenceId.IsNone() || Link.SourceEvidenceId == Link.TargetEvidenceId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid link identifiers"));
	}
	FOwnerState& State = GetOrAddOwner(OwnerId);
	if (const FDocEvidenceLink* Existing = State.Links.Find(Link.LinkId))
	{
		const bool bSame = Existing->SourceEvidenceId == Link.SourceEvidenceId && Existing->TargetEvidenceId == Link.TargetEvidenceId && Existing->Relation == Link.Relation;
		return bSame ? FDocSystemResult::MakeNoChange(TEXT("Link exists")) : FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("LinkId already used"));
	}
	State.Links.Add(Link.LinkId, Link);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocEvidenceSubsystem::RemoveInterpretationLink(FName LinkId, FName OwnerId)
{
	FOwnerState* State = Owners.Find(OwnerId);
	if (!State || State->Links.Remove(LinkId) == 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Link not found"));
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocEvidenceSubsystem::EvaluateHypothesis(FName HypothesisId, FDocDeductionResult& OutResult, FName OwnerId) const
{
	OutResult = FDocDeductionResult();
	OutResult.HypothesisId = HypothesisId;
	const TStrongObjectPtr<UDocHypothesisDefinition>* DefPtr = HypothesisDefinitions.Find(HypothesisId);
	if (!DefPtr || !DefPtr->IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Hypothesis definition not found"));
	}
	if ((*DefPtr)->bIsHiddenSolution)
	{
		OutResult.Evaluation = EDocHypothesisEvaluation::Unavailable;
		OutResult.bRedacted = true;
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Hidden solution requires authorization"));
	}
	static const FOwnerState Empty;
	const FOwnerState* State = FindOwner(OwnerId);
	EvaluateInternal(State ? *State : Empty, HypothesisId, EAudience::Player, 0, OutResult);
	RedactForPlayer(OutResult);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocEvidenceSubsystem::QueryExplanation(FName HypothesisId, bool bIncludeSecretSolutions, FDocDeductionResult& OutResult, FName OwnerId) const
{
	if (bIncludeSecretSolutions)
	{
		return QueryDebugExplanation(HypothesisId, OutResult, OwnerId);
	}
	return EvaluateHypothesis(HypothesisId, OutResult, OwnerId);
}

FDocSystemResult UDocEvidenceSubsystem::QueryDebugExplanation(FName HypothesisId, FDocDeductionResult& OutResult, FName OwnerId) const
{
	OutResult = FDocDeductionResult();
	OutResult.HypothesisId = HypothesisId;
#if UE_BUILD_SHIPPING
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Debug explanations are not available in Shipping"));
#else
	if (!bAuthorizeDebugExplanations)
	{
		OutResult.bRedacted = true;
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Debug explanations are not authorized"));
	}
	if (!HypothesisDefinitions.Contains(HypothesisId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Hypothesis definition not found"));
	}
	static const FOwnerState Empty;
	const FOwnerState* State = FindOwner(OwnerId);
	EvaluateInternal(State ? *State : Empty, HypothesisId, EAudience::Debug, 0, OutResult);
	return FDocSystemResult::MakeSuccess();
#endif
}

TArray<FDocDeductionResult> UDocEvidenceSubsystem::QueryAvailableHypotheses(FName OwnerId) const
{
	TArray<FName> Ids;
	for (const TPair<FName, TStrongObjectPtr<UDocHypothesisDefinition>>& Kvp : HypothesisDefinitions)
	{
		if (Kvp.Value.IsValid() && !Kvp.Value->bIsHiddenSolution)
		{
			Ids.Add(Kvp.Key);
		}
	}
	Ids.Sort(FNameLexicalLess());
	TArray<FDocDeductionResult> Results;
	for (const FName& Id : Ids)
	{
		FDocDeductionResult R;
		EvaluateHypothesis(Id, R, OwnerId);
		Results.Add(R);
	}
	return Results;
}

TArray<FDocContradiction> UDocEvidenceSubsystem::QueryContradictions(FName OwnerId) const
{
	const FOwnerState* State = FindOwner(OwnerId);
	return State ? FindContradictions(*State, EAudience::Player) : TArray<FDocContradiction>();
}

// ---------------------------------------------------------------------------------------------
// Conclusions
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocEvidenceSubsystem::CommitConclusion(FName ConclusionId, FName HypothesisId, int64 ExpectedRevision, FName ReceiptKey, FName OwnerId)
{
	if (ConclusionId.IsNone() || HypothesisId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("ConclusionId or HypothesisId is None"));
	}
	FOwnerState& State = GetOrAddOwner(OwnerId);

	if (const FDocConclusionRecord* Existing = State.Conclusions.Find(ConclusionId))
	{
		return Existing->HypothesisId == HypothesisId
			? FDocSystemResult::MakeNoChange(TEXT("Conclusion already committed (no duplicate effects)"))
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("ConclusionId already used for another hypothesis"));
	}
	if (!ReceiptKey.IsNone() && State.Receipts.Contains(ReceiptKey))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Receipt key already used"));
	}
	if (ExpectedRevision != State.Revision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Stale evidence snapshot (expected %lld vs current %lld)"), ExpectedRevision, State.Revision));
	}

	const TStrongObjectPtr<UDocHypothesisDefinition>* DefPtr = HypothesisDefinitions.Find(HypothesisId);
	if (!DefPtr || !DefPtr->IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Hypothesis not found"));
	}
	const UDocHypothesisDefinition& Def = *DefPtr->Get();

	if (!Def.MutuallyExclusiveGroupId.IsNone())
	{
		for (const TPair<FName, FDocConclusionRecord>& Kvp : State.Conclusions)
		{
			const TStrongObjectPtr<UDocHypothesisDefinition>* Other = HypothesisDefinitions.Find(Kvp.Value.HypothesisId);
			if (!Kvp.Value.bIsChallenged && Other && Other->IsValid() && (*Other)->MutuallyExclusiveGroupId == Def.MutuallyExclusiveGroupId)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("Conclusion group %s already has committed conclusion %s"),
					*Def.MutuallyExclusiveGroupId.ToString(), *Kvp.Key.ToString()));
			}
		}
	}

	FDocDeductionResult Eval;
	EvaluateInternal(State, HypothesisId, EAudience::Player, 0, Eval);
	if (Eval.Evaluation != EDocHypothesisEvaluation::Supported)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed,
			FString::Printf(TEXT("Hypothesis is %s for this player"), *UEnum::GetValueAsString(Eval.Evaluation)));
	}

	FDocConclusionRecord Record;
	Record.ConclusionId = ConclusionId;
	Record.OwnerId = OwnerId;
	Record.HypothesisId = HypothesisId;
	Record.CommittedSnapshotRevision = State.Revision;
	Record.ReceiptKey = ReceiptKey;
	for (const FName& Key : DocEvidencePrivate::SortedKeys(State.Observations))
	{
		const FDocEvidenceObservation& Obs = State.Observations[Key];
		if (Eval.SupportingEvidenceIds.Contains(Obs.EvidenceId) && IsEstablished(Obs, EAudience::Player))
		{
			Record.SupportingObservationRevisions.Add(FString::Printf(TEXT("%s@%d"), *Obs.ObservationId.ToString(), Obs.Revision));
		}
	}

	// Commit first; effects/listeners run after the record exists.
	State.Conclusions.Add(ConclusionId, Record);
	if (!ReceiptKey.IsNone())
	{
		State.Receipts.Add(ReceiptKey);
	}
	++State.Revision;

	OnConclusionCommitted.Broadcast(Record);
	OnConclusionCommittedNative.Broadcast(Record);
	return FDocSystemResult::MakeSuccess();
}

bool UDocEvidenceSubsystem::IsConclusionCommitted(FName ConclusionId, FName OwnerId) const
{
	const FOwnerState* State = FindOwner(OwnerId);
	return State && State->Conclusions.Contains(ConclusionId);
}

bool UDocEvidenceSubsystem::IsConclusionChallenged(FName ConclusionId, FName OwnerId) const
{
	const FOwnerState* State = FindOwner(OwnerId);
	const FDocConclusionRecord* Record = State ? State->Conclusions.Find(ConclusionId) : nullptr;
	return Record && Record->bIsChallenged;
}

bool UDocEvidenceSubsystem::GetConclusion(FName ConclusionId, FDocConclusionRecord& OutRecord, FName OwnerId) const
{
	const FOwnerState* State = FindOwner(OwnerId);
	if (const FDocConclusionRecord* Record = State ? State->Conclusions.Find(ConclusionId) : nullptr)
	{
		OutRecord = *Record;
		return true;
	}
	return false;
}

bool UDocEvidenceSubsystem::HasReceipt(FName ReceiptKey, FName OwnerId) const
{
	const FOwnerState* State = FindOwner(OwnerId);
	return State && State->Receipts.Contains(ReceiptKey);
}

// ---------------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------------

int64 UDocEvidenceSubsystem::GetEvidenceRevision(FName OwnerId) const
{
	const FOwnerState* State = FindOwner(OwnerId);
	return State ? State->Revision : 1;
}

FDocEvidenceSnapshot UDocEvidenceSubsystem::CaptureState(FName OwnerId) const
{
	FDocEvidenceSnapshot Snapshot;
	Snapshot.OwnerId = OwnerId;
	if (const FOwnerState* State = FindOwner(OwnerId))
	{
		Snapshot.Revision = State->Revision;
		Snapshot.Observations = State->Observations;
		State->Links.GenerateValueArray(Snapshot.Links);
		Snapshot.Links.Sort([](const FDocEvidenceLink& A, const FDocEvidenceLink& B) { return A.LinkId.LexicalLess(B.LinkId); });
		Snapshot.Conclusions = State->Conclusions;
		Snapshot.Receipts = State->Receipts.Array();
		Snapshot.Receipts.Sort(FNameLexicalLess());
		Snapshot.QuarantinedConclusions = State->Quarantined;
	}
	else
	{
		Snapshot.Revision = 1;
	}
	return Snapshot;
}

FDocSystemResult UDocEvidenceSubsystem::RestoreState(const FDocEvidenceSnapshot& Snapshot)
{
	if (Snapshot.SchemaVersion != FDocEvidenceSnapshot::CurrentSchemaVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Unsupported evidence snapshot schema"));
	}
	if (Snapshot.Revision < 1)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid snapshot revision"));
	}
	for (const TPair<FName, FDocEvidenceObservation>& Kvp : Snapshot.Observations)
	{
		if (Kvp.Key.IsNone() || Kvp.Key != Kvp.Value.ObservationId || Kvp.Value.EvidenceId.IsNone() || Kvp.Value.Revision < 1)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Snapshot contains an invalid observation"));
		}
	}
	TSet<FName> SeenReceipts;
	for (const TPair<FName, FDocConclusionRecord>& Kvp : Snapshot.Conclusions)
	{
		if (Kvp.Key.IsNone() || Kvp.Key != Kvp.Value.ConclusionId)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Snapshot contains an invalid conclusion"));
		}
		if (!Kvp.Value.ReceiptKey.IsNone())
		{
			if (SeenReceipts.Contains(Kvp.Value.ReceiptKey))
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Two conclusions share a receipt"));
			}
			SeenReceipts.Add(Kvp.Value.ReceiptKey);
		}
	}
	TSet<FName> SeenLinks;
	for (const FDocEvidenceLink& Link : Snapshot.Links)
	{
		if (Link.LinkId.IsNone() || SeenLinks.Contains(Link.LinkId))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Snapshot contains duplicate or invalid links"));
		}
		SeenLinks.Add(Link.LinkId);
	}

	FOwnerState State;
	State.Revision = Snapshot.Revision;
	State.Observations = Snapshot.Observations;
	for (const FDocEvidenceLink& Link : Snapshot.Links)
	{
		State.Links.Add(Link.LinkId, Link);
	}
	State.Quarantined = Snapshot.QuarantinedConclusions;
	for (const TPair<FName, FDocConclusionRecord>& Kvp : Snapshot.Conclusions)
	{
		if (HypothesisDefinitions.Contains(Kvp.Value.HypothesisId))
		{
			State.Conclusions.Add(Kvp.Key, Kvp.Value);
		}
		else
		{
			State.Quarantined.Add(Kvp.Value); // removed definition: kept, inactive, not awarded again
		}
	}
	for (const FName& Receipt : Snapshot.Receipts) { State.Receipts.Add(Receipt); }
	for (const FName& Receipt : SeenReceipts) { State.Receipts.Add(Receipt); }

	Owners.Add(Snapshot.OwnerId, MoveTemp(State)); // no commit or challenge events on restore
	return FDocSystemResult::MakeSuccess();
}
