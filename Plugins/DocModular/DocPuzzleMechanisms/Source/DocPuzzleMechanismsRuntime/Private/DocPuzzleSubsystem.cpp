#include "DocPuzzleSubsystem.h"
#include "DocPuzzleMechanismsLog.h"
#include "Engine/World.h"

void UDocPuzzleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDocPuzzleMechanisms, Log, TEXT("DocPuzzleSubsystem initialized."));
}

void UDocPuzzleSubsystem::Deinitialize()
{
	RegisteredPuzzles.Empty();
	RetainedPuzzles.Empty();
	ClockOverride = nullptr;
	Super::Deinitialize();
}

void UDocPuzzleSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessTimers();
}

TStatId UDocPuzzleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocPuzzleSubsystem, STATGROUP_Tickables);
}

double UDocPuzzleSubsystem::GetCurrentTimeSeconds() const
{
	if (ClockOverride)
	{
		return ClockOverride();
	}
	UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

FDocSystemResult UDocPuzzleSubsystem::RegisterPuzzle(const FGuid& InstanceId, const UDocPuzzleDefinition* Definition, const FDocOwnerScope& Scope)
{
	if (!InstanceId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid InstanceId for puzzle registration."));
	}
	if (!Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Null Definition for puzzle registration."));
	}
	TArray<FText> Errors;
	if (!Definition->ValidateDefinition(Errors))
	{
		const FString First = Errors.Num() > 0 ? Errors[0].ToString() : FString(TEXT("Definition failed validation."));
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Invalid puzzle definition: %s"), *First));
	}
	if (RegisteredPuzzles.Contains(InstanceId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Puzzle instance already registered."));
	}

	// Re-bind retained state instead of silently starting over.
	if (FDocPuzzleRuntimeRecord* Retained = RetainedPuzzles.Find(InstanceId))
	{
		const bool bSameDefinition = Retained->Definition == Definition
			|| (Retained->Definition && Retained->Definition->DefinitionId == Definition->DefinitionId && Retained->Definition->SchemaVersion == Definition->SchemaVersion);
		if (!bSameDefinition)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Retained state belongs to a different definition; call DiscardRetainedState first."));
		}
		FDocPuzzleRuntimeRecord Rebound = MoveTemp(*Retained);
		RetainedPuzzles.Remove(InstanceId);
		Rebound.Definition = Definition;
		Rebound.Scope = Scope;
		Rebound.StateRevision++;
		RegisteredPuzzles.Add(InstanceId, MoveTemp(Rebound));
		return FDocSystemResult::MakeSuccess();
	}

	FDocPuzzleRuntimeRecord NewRecord;
	NewRecord.InstanceId = InstanceId;
	NewRecord.Scope = Scope;
	NewRecord.Definition = Definition;
	NewRecord.State = EDocPuzzleState::Ready;
	NewRecord.ResetEpoch = 0;
	NewRecord.StateRevision = 1;

	RegisteredPuzzles.Add(InstanceId, MoveTemp(NewRecord));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPuzzleSubsystem::UnregisterPuzzle(const FGuid& InstanceId)
{
	FDocPuzzleRuntimeRecord Record;
	if (!RegisteredPuzzles.RemoveAndCopyValue(InstanceId, Record))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Puzzle instance not found to unregister."));
	}

	// Live contributors belong to the live world and leave with it; everything else is retained.
	for (const FDocPuzzleInputContributor& Contributor : Record.ActiveContributors)
	{
		Record.CurrentInputStates.Remove(Contributor.InputId);
	}
	Record.ActiveContributors.Reset();
	RetainedPuzzles.Add(InstanceId, MoveTemp(Record));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPuzzleSubsystem::DiscardRetainedState(const FGuid& InstanceId)
{
	if (RetainedPuzzles.Remove(InstanceId) > 0)
	{
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No retained state for this instance."));
}

bool UDocPuzzleSubsystem::HasRetainedState(const FGuid& InstanceId) const
{
	return RetainedPuzzles.Contains(InstanceId);
}

FDocSystemResult UDocPuzzleSubsystem::StartAttempt(const FGuid& InstanceId, FGuid& OutAttemptId)
{
	FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Puzzle instance not found."));
	}
	if (Record->State == EDocPuzzleState::AttemptActive)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("An attempt is already active; reset it first."));
	}
	if (Record->State == EDocPuzzleState::Solved && !Record->Definition->bAllowReset)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Puzzle is already solved and cannot be repeated."));
	}

	Record->ActiveAttemptId = FGuid::NewGuid();
	Record->State = EDocPuzzleState::AttemptActive;
	Record->AttemptStartTime = GetCurrentTimeSeconds();
	Record->LastAcceptedSourceSequence.Empty();
	Record->LastInputTimes.Empty();
	Record->RuleStateInts.Empty();
	Record->RuleStateDoubles.Empty();
	Record->WinningRuleId = NAME_None;
	Record->StateRevision++;

	if (Record->Definition && Record->Definition->RootRule)
	{
		FDocPuzzleRuleEvaluationContext Context;
		Context.CurrentTime = Record->AttemptStartTime;
		Context.CurrentInputStates = &Record->CurrentInputStates;
		Context.ActiveContributors = &Record->ActiveContributors;
		Context.RuleStateInts = &Record->RuleStateInts;
		Context.RuleStateDoubles = &Record->RuleStateDoubles;
		Record->Definition->RootRule->ResetRuntimeState(Context);
	}

	OutAttemptId = Record->ActiveAttemptId;

	// Commit, not a silent view: if live contributors already satisfy the rules, this solves with events.
	CommitInstance(InstanceId, false);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPuzzleSubsystem::RejectInput(const FDocPuzzleInputEvent& Event, const FDocSystemResult& Result)
{
	OnInputRejected.Broadcast(Event.InstanceId, Event, Result);
	OnInputRejectedNative.Broadcast(Event.InstanceId, Event, Result);
	return Result;
}

FDocSystemResult UDocPuzzleSubsystem::ValidateDeclaredInput(const FDocPuzzleRuntimeRecord& Record, FName InputId, const FDocPuzzleInputValue& Value) const
{
	if (!Record.Definition || Record.Definition->Inputs.IsEmpty())
	{
		return FDocSystemResult::MakeSuccess(); // Undeclared-input definitions accept any input id.
	}
	const FDocPuzzleInputDefinition* Declared = Record.Definition->FindInput(InputId);
	if (!Declared)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Input '%s' is not declared by this puzzle."), *InputId.ToString()));
	}
	if (Declared->AcceptedKind != Value.Kind)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Input '%s' has the wrong value kind."), *InputId.ToString()));
	}
	if (Value.Kind == EDocPuzzleInputKind::Scalar
		&& (!FMath::IsFinite(Value.ScalarValue) || Value.ScalarValue < Declared->MinScalar || Value.ScalarValue > Declared->MaxScalar))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Input '%s' is out of range."), *InputId.ToString()));
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPuzzleSubsystem::SubmitInput(const FDocPuzzleInputEvent& Event)
{
	FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(Event.InstanceId);
	if (!Record)
	{
		return RejectInput(Event, FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Puzzle instance not found.")));
	}
	if (Record->State != EDocPuzzleState::AttemptActive)
	{
		return RejectInput(Event, FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Puzzle is not in an active attempt.")));
	}

	// Every input names the attempt it was produced for; a missing id cannot slip past the stale check.
	if (!Event.AttemptId.IsValid() || Event.AttemptId != Record->ActiveAttemptId)
	{
		return RejectInput(Event, FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Missing or stale attempt ID on submitted input.")));
	}

	const FDocSystemResult Declared = ValidateDeclaredInput(*Record, Event.InputId, Event.Value);
	if (!Declared.IsSuccess())
	{
		return RejectInput(Event, Declared);
	}

	// Authority clock: the caller's timestamp is not trusted for deadlines.
	const double Now = GetCurrentTimeSeconds();

	// Evaluate against the attempt deadline before applying the input: exactly at the deadline is late.
	if (Record->Definition->TimeoutSeconds > 0.0f && Now >= Record->AttemptStartTime + (double)Record->Definition->TimeoutSeconds)
	{
		const FGuid InstanceId = Record->InstanceId;
		FailAttempt(InstanceId);
		return RejectInput(Event, FDocSystemResult::MakeFailure(EDocResultOutcome::TimedOut, TEXT("Puzzle attempt deadline expired.")));
	}

	// Debounce declared inputs.
	if (const FDocPuzzleInputDefinition* InputDef = Record->Definition->FindInput(Event.InputId))
	{
		const double* LastTime = Record->LastInputTimes.Find(Event.InputId);
		if (InputDef->DebounceSeconds > 0.0f && LastTime && (Now - *LastTime) < (double)InputDef->DebounceSeconds)
		{
			return RejectInput(Event, FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Input debounced.")));
		}
	}

	// Deduplicate replayed source sequence
	if (Event.SourceId.IsValid() && Event.SourceSequenceNumber > 0)
	{
		const int64* LastSeq = Record->LastAcceptedSourceSequence.Find(Event.SourceId);
		if (LastSeq && Event.SourceSequenceNumber <= *LastSeq)
		{
			return RejectInput(Event, FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Duplicate or out-of-order source sequence input.")));
		}
		Record->LastAcceptedSourceSequence.Add(Event.SourceId, Event.SourceSequenceNumber);
	}

	FDocPuzzleInputEvent Accepted = Event;
	Accepted.AcceptedTimestamp = Now;
	Record->LastInputTimes.Add(Event.InputId, Now);

	const FGuid InstanceId = Record->InstanceId;
	bool bFailed = false;
	if (Record->Definition && Record->Definition->RootRule)
	{
		FDocPuzzleRuleEvaluationContext Context;
		Context.CurrentTime = Now;
		Context.CurrentInputStates = &Record->CurrentInputStates; // Pre-update state, for edge detection.
		Context.ActiveContributors = &Record->ActiveContributors;
		Context.RuleStateInts = &Record->RuleStateInts;
		Context.RuleStateDoubles = &Record->RuleStateDoubles;
		Record->Definition->RootRule->OnInputSubmitted(Accepted, Context);
		bFailed = Context.bAttemptFailed;
	}

	if (!bFailed && Accepted.Value.Kind != EDocPuzzleInputKind::Trigger)
	{
		Record->CurrentInputStates.Add(Accepted.InputId, Accepted.Value);
	}
	Record->StateRevision++;

	// Record may move once listeners run; only ids are used from here on.
	OnInputAccepted.Broadcast(InstanceId, Accepted);
	OnInputAcceptedNative.Broadcast(InstanceId, Accepted);

	if (bFailed)
	{
		FailAttempt(InstanceId);
		return FDocSystemResult::MakeSuccess();
	}

	CommitInstance(InstanceId, true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPuzzleSubsystem::SetInputContributor(const FGuid& InstanceId, const FGuid& SourceId, FName InputId, const FDocPuzzleInputValue& Value)
{
	FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Puzzle instance not found."));
	}
	if (!SourceId.IsValid() || InputId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Contributor needs a valid SourceId and InputId."));
	}
	const FDocSystemResult Declared = ValidateDeclaredInput(*Record, InputId, Value);
	if (!Declared.IsSuccess())
	{
		return Declared;
	}

	const int32 ExistingIndex = Record->ActiveContributors.IndexOfByPredicate([&](const FDocPuzzleInputContributor& Item) {
		return Item.SourceId == SourceId && Item.InputId == InputId;
	});

	const double Now = GetCurrentTimeSeconds();
	if (ExistingIndex != INDEX_NONE)
	{
		Record->ActiveContributors[ExistingIndex].Value = Value;
		Record->ActiveContributors[ExistingIndex].Timestamp = Now;
	}
	else
	{
		FDocPuzzleInputContributor Contrib;
		Contrib.SourceId = SourceId;
		Contrib.InputId = InputId;
		Contrib.Value = Value;
		Contrib.Timestamp = Now;
		Record->ActiveContributors.Add(MoveTemp(Contrib));
	}

	Record->CurrentInputStates.Add(InputId, Value);
	Record->StateRevision++;
	CommitInstance(InstanceId, true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPuzzleSubsystem::RemoveInputContributor(const FGuid& InstanceId, const FGuid& SourceId, FName InputId)
{
	FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Puzzle instance not found."));
	}

	const int32 RemovedCount = Record->ActiveContributors.RemoveAll([&](const FDocPuzzleInputContributor& Item) {
		return Item.SourceId == SourceId && Item.InputId == InputId;
	});

	if (RemovedCount > 0)
	{
		const FDocPuzzleInputContributor* Remaining = Record->ActiveContributors.FindByPredicate([&](const FDocPuzzleInputContributor& Item) {
			return Item.InputId == InputId;
		});
		if (Remaining)
		{
			Record->CurrentInputStates.Add(InputId, Remaining->Value);
		}
		else
		{
			Record->CurrentInputStates.Remove(InputId);
		}
		Record->StateRevision++;
		CommitInstance(InstanceId, true);
	}

	return FDocSystemResult::MakeSuccess(); // Repeat-safe.
}

void UDocPuzzleSubsystem::FailAttempt(const FGuid& InstanceId)
{
	FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	if (!Record || Record->State != EDocPuzzleState::AttemptActive)
	{
		return;
	}
	Record->State = EDocPuzzleState::Failed;
	Record->StateRevision++;
	const FGuid AttemptId = Record->ActiveAttemptId;
	const bool bResetOnFailure = Record->Definition && Record->Definition->bResetOnFailure;

	OnAttemptFailed.Broadcast(InstanceId, AttemptId);
	OnAttemptFailedNative.Broadcast(InstanceId, AttemptId);

	if (bResetOnFailure)
	{
		RequestReset(InstanceId);
	}
}

FDocSystemResult UDocPuzzleSubsystem::RequestReset(const FGuid& InstanceId)
{
	FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Puzzle instance not found."));
	}

	if (!Record->Definition->bAllowReset)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Puzzle definition does not allow reset."));
	}

	Record->ResetEpoch++;
	Record->ActiveAttemptId.Invalidate();
	Record->State = EDocPuzzleState::Ready;
	Record->RuleStateInts.Empty();
	Record->RuleStateDoubles.Empty();
	Record->LastAcceptedSourceSequence.Empty();
	Record->LastInputTimes.Empty();
	Record->WinningRuleId = NAME_None;
	Record->CachedProgress.Reset();
	Record->UnsatisfiedRuleIds.Reset();
	Record->StateRevision++;
	// EffectReceipts are kept: a reset never erases a committed external reward receipt.

	if (Record->Definition && Record->Definition->RootRule)
	{
		FDocPuzzleRuleEvaluationContext Context;
		Context.CurrentTime = GetCurrentTimeSeconds();
		Context.RuleStateInts = &Record->RuleStateInts;
		Context.RuleStateDoubles = &Record->RuleStateDoubles;
		Record->Definition->RootRule->ResetRuntimeState(Context);
	}

	const int32 Epoch = Record->ResetEpoch;
	OnResetCommitted.Broadcast(InstanceId, Epoch);
	OnResetCommittedNative.Broadcast(InstanceId, Epoch);
	return FDocSystemResult::MakeSuccess();
}

void UDocPuzzleSubsystem::BuildEvaluation(const FDocPuzzleRuntimeRecord& Record, double Now, FDocPuzzleEvaluation& OutEvaluation, bool& bOutSatisfied, FName& OutWinningRuleId) const
{
	OutEvaluation = FDocPuzzleEvaluation();
	OutEvaluation.State = Record.State;
	OutEvaluation.ActiveAttemptId = Record.ActiveAttemptId;
	OutEvaluation.StateRevision = Record.StateRevision;
	bOutSatisfied = false;
	OutWinningRuleId = NAME_None;

	if (Record.Definition && Record.Definition->RootRule)
	{
		// Rules only read through these pointers during Evaluate.
		FDocPuzzleRuntimeRecord& Mutable = const_cast<FDocPuzzleRuntimeRecord&>(Record);
		FDocPuzzleRuleEvaluationContext Context;
		Context.CurrentTime = Now;
		Context.CurrentInputStates = &Mutable.CurrentInputStates;
		Context.ActiveContributors = &Mutable.ActiveContributors;
		Context.RuleStateInts = &Mutable.RuleStateInts;
		Context.RuleStateDoubles = &Mutable.RuleStateDoubles;

		FDocPuzzleRuleProgress RootProgress;
		bOutSatisfied = Record.Definition->RootRule->Evaluate(Context, RootProgress);
		OutEvaluation.RuleProgress.Add(RootProgress);
		if (!bOutSatisfied)
		{
			OutEvaluation.UnsatisfiedRuleIds.Add(RootProgress.RuleId);
			for (const FName& Child : RootProgress.UnsatisfiedChildRuleIds)
			{
				OutEvaluation.UnsatisfiedRuleIds.AddUnique(Child);
			}
		}
		OutWinningRuleId = RootProgress.WinningChildRuleId.IsNone() ? RootProgress.RuleId : RootProgress.WinningChildRuleId;
	}

	OutEvaluation.WinningRuleId = Record.State == EDocPuzzleState::Solved ? Record.WinningRuleId : NAME_None;
}

FDocSystemResult UDocPuzzleSubsystem::EvaluatePuzzle(const FGuid& InstanceId, FDocPuzzleEvaluation& OutEvaluation)
{
	const FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Puzzle instance not found."));
	}

	bool bSatisfied = false;
	FName Winning;
	BuildEvaluation(*Record, GetCurrentTimeSeconds(), OutEvaluation, bSatisfied, Winning);
	return FDocSystemResult::MakeSuccess();
}

void UDocPuzzleSubsystem::CommitInstance(const FGuid& InstanceId, bool bPublishProgress)
{
	FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	if (!Record || Record->State != EDocPuzzleState::AttemptActive || !Record->Definition || !Record->Definition->RootRule)
	{
		return;
	}

	const double Now = GetCurrentTimeSeconds();
	if (Record->Definition->TimeoutSeconds > 0.0f && Now >= Record->AttemptStartTime + (double)Record->Definition->TimeoutSeconds)
	{
		FailAttempt(InstanceId);
		return;
	}

	FDocPuzzleRuleEvaluationContext Context;
	Context.CurrentTime = Now;
	Context.CurrentInputStates = &Record->CurrentInputStates;
	Context.ActiveContributors = &Record->ActiveContributors;
	Context.RuleStateInts = &Record->RuleStateInts;
	Context.RuleStateDoubles = &Record->RuleStateDoubles;
	Record->Definition->RootRule->UpdateLevelState(Context);
	if (Context.bAttemptFailed)
	{
		FailAttempt(InstanceId);
		return;
	}

	FDocPuzzleEvaluation Eval;
	bool bSatisfied = false;
	FName Winning;
	BuildEvaluation(*Record, Now, Eval, bSatisfied, Winning);
	Record->CachedProgress = Eval.RuleProgress;
	Record->UnsatisfiedRuleIds = Eval.UnsatisfiedRuleIds;

	if (bSatisfied)
	{
		// One transition per attempt: alternatives completing together still produce a single solve.
		Record->State = EDocPuzzleState::Solved;
		Record->WinningRuleId = Winning;
		Record->StateRevision++;
		Eval.State = Record->State;
		Eval.StateRevision = Record->StateRevision;
		Eval.WinningRuleId = Winning;
		const FGuid AttemptId = Record->ActiveAttemptId;

		OnProgressChanged.Broadcast(InstanceId, Eval);
		OnProgressChangedNative.Broadcast(InstanceId, Eval);
		OnPuzzleSolved.Broadcast(InstanceId, AttemptId);
		OnPuzzleSolvedNative.Broadcast(InstanceId, AttemptId);
	}
	else if (bPublishProgress)
	{
		OnProgressChanged.Broadcast(InstanceId, Eval);
		OnProgressChangedNative.Broadcast(InstanceId, Eval);
	}
}

void UDocPuzzleSubsystem::ProcessTimers()
{
	TArray<FGuid> Active;
	for (const TPair<FGuid, FDocPuzzleRuntimeRecord>& Pair : RegisteredPuzzles)
	{
		if (Pair.Value.State == EDocPuzzleState::AttemptActive)
		{
			Active.Add(Pair.Key);
		}
	}
	for (const FGuid& Id : Active)
	{
		CommitInstance(Id, false);
	}
}

FDocSystemResult UDocPuzzleSubsystem::CapturePuzzle(const FGuid& InstanceId, FDocPuzzleSnapshot& OutSnapshot) const
{
	const FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Puzzle instance not found."));
	}

	const double Now = GetCurrentTimeSeconds();
	OutSnapshot = FDocPuzzleSnapshot();
	OutSnapshot.SchemaVersion = 2;
	OutSnapshot.InstanceId = Record->InstanceId;
	OutSnapshot.DefinitionId = Record->Definition ? Record->Definition->DefinitionId : NAME_None;
	OutSnapshot.AttemptId = Record->ActiveAttemptId;
	OutSnapshot.ResetEpoch = Record->ResetEpoch;
	OutSnapshot.State = Record->State;
	OutSnapshot.EffectReceipts = Record->EffectReceipts;
	OutSnapshot.RuleStateInts = Record->RuleStateInts;
	OutSnapshot.WinningRuleId = Record->WinningRuleId;
	OutSnapshot.AttemptElapsedSeconds = Record->State == EDocPuzzleState::AttemptActive ? FMath::Max(0.0, Now - Record->AttemptStartTime) : 0.0;

	for (const TPair<FName, double>& Timer : Record->RuleStateDoubles)
	{
		OutSnapshot.RuleTimerElapsedSeconds.Add(Timer.Key, FMath::Max(0.0, Now - Timer.Value));
	}

	// Submitted level inputs only: contributor-owned levels come back from their live sources.
	TSet<FName> ContributorInputs;
	for (const FDocPuzzleInputContributor& Contributor : Record->ActiveContributors)
	{
		ContributorInputs.Add(Contributor.InputId);
	}
	for (const TPair<FName, FDocPuzzleInputValue>& Input : Record->CurrentInputStates)
	{
		if (!ContributorInputs.Contains(Input.Key))
		{
			OutSnapshot.InputStates.Add(Input.Key, Input.Value);
		}
	}

	// Legacy readers.
	for (const TPair<FName, int32>& Kvp : Record->RuleStateInts)
	{
		OutSnapshot.RuleProgressValues.Add(Kvp.Key, (float)Kvp.Value);
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPuzzleSubsystem::StageRestore(const FDocPuzzleSnapshot& Snapshot)
{
	if (!Snapshot.InstanceId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid InstanceId in restore snapshot."));
	}
	if (Snapshot.SchemaVersion < 1 || Snapshot.SchemaVersion > 2)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unsupported puzzle snapshot schema."));
	}

	FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(Snapshot.InstanceId);
	if (!Record)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Cannot restore snapshot: puzzle instance is not registered."));
	}
	if (!Snapshot.DefinitionId.IsNone() && Record->Definition && Record->Definition->DefinitionId != Snapshot.DefinitionId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Snapshot belongs to a different puzzle definition."));
	}

	const double Now = GetCurrentTimeSeconds();
	Record->ActiveAttemptId = Snapshot.AttemptId;
	Record->ResetEpoch = Snapshot.ResetEpoch;
	Record->State = Snapshot.State;
	Record->EffectReceipts = Snapshot.EffectReceipts;
	Record->WinningRuleId = Snapshot.WinningRuleId;
	Record->AttemptStartTime = Now - FMath::Max(0.0, Snapshot.AttemptElapsedSeconds);
	Record->LastAcceptedSourceSequence.Empty();
	Record->LastInputTimes.Empty();
	Record->CachedProgress.Reset();
	Record->UnsatisfiedRuleIds.Reset();
	Record->StateRevision++;

	Record->RuleStateInts.Empty();
	if (Snapshot.SchemaVersion >= 2)
	{
		Record->RuleStateInts = Snapshot.RuleStateInts;
	}
	else
	{
		for (const TPair<FName, float>& Kvp : Snapshot.RuleProgressValues)
		{
			Record->RuleStateInts.Add(Kvp.Key, FMath::RoundToInt(Kvp.Value));
		}
	}

	// Timers keep their remaining duration on the restoring clock.
	Record->RuleStateDoubles.Empty();
	for (const TPair<FName, double>& Timer : Snapshot.RuleTimerElapsedSeconds)
	{
		Record->RuleStateDoubles.Add(Timer.Key, Now - FMath::Max(0.0, Timer.Value));
	}

	// Submitted inputs come from the snapshot; contributor levels only from contributors that are live now.
	Record->CurrentInputStates = Snapshot.InputStates;
	for (const FDocPuzzleInputContributor& Contributor : Record->ActiveContributors)
	{
		Record->CurrentInputStates.Add(Contributor.InputId, Contributor.Value);
	}

	// No evaluation commit and no events: restore never replays historical completion.
	return FDocSystemResult::MakeSuccess();
}

bool UDocPuzzleSubsystem::IsPuzzleRegistered(const FGuid& InstanceId) const
{
	return RegisteredPuzzles.Contains(InstanceId);
}

EDocPuzzleState UDocPuzzleSubsystem::GetPuzzleState(const FGuid& InstanceId) const
{
	const FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	return Record ? Record->State : EDocPuzzleState::Inactive;
}

bool UDocPuzzleSubsystem::IsPuzzleSolved(const FGuid& InstanceId) const
{
	return GetPuzzleState(InstanceId) == EDocPuzzleState::Solved;
}

int32 UDocPuzzleSubsystem::GetResetEpoch(const FGuid& InstanceId) const
{
	const FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	return Record ? Record->ResetEpoch : 0;
}

FGuid UDocPuzzleSubsystem::GetActiveAttemptId(const FGuid& InstanceId) const
{
	const FDocPuzzleRuntimeRecord* Record = RegisteredPuzzles.Find(InstanceId);
	return Record ? Record->ActiveAttemptId : FGuid();
}
