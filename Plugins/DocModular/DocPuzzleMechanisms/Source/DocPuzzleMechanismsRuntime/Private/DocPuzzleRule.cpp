#include "DocPuzzleRule.h"
#include "Templates/UnrealTemplate.h"

namespace DocPuzzleRulePrivate
{
	static bool DepthExceeded(const FDocPuzzleRuleEvaluationContext& Context)
	{
		return Context.EvaluationDepth >= FDocPuzzleRuleEvaluationContext::MaxEvaluationDepth;
	}
}

void UDocPuzzleRule::ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const
{
}

void UDocPuzzleRule::OnInputSubmitted(const FDocPuzzleInputEvent& Event, FDocPuzzleRuleEvaluationContext& Context) const
{
}

void UDocPuzzleRule::UpdateLevelState(FDocPuzzleRuleEvaluationContext& Context) const
{
}

bool UDocPuzzleRule::Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const
{
	OutProgress.RuleId = RuleId;
	OutProgress.bSatisfied = false;
	OutProgress.NormalizedProgress = 0.0f;
	OutProgress.UnmetReason = LocalizedUnmetReason;
	return false;
}

void UDocPuzzleRule::ValidateRule(TArray<FText>& OutErrors) const
{
	if (RuleId.IsNone())
	{
		OutErrors.Add(FText::FromString(TEXT("Rule has an invalid/None RuleId.")));
	}
}

// -----------------------------------------------------------------------------
// UDocPuzzleRule_OrderedInputs
// -----------------------------------------------------------------------------

void UDocPuzzleRule_OrderedInputs::ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const
{
	if (Context.RuleStateInts)
	{
		Context.RuleStateInts->Add(RuleId, 0);
	}
}

void UDocPuzzleRule_OrderedInputs::OnInputSubmitted(const FDocPuzzleInputEvent& Event, FDocPuzzleRuleEvaluationContext& Context) const
{
	if (!Context.RuleStateInts || ExpectedSequence.IsEmpty())
	{
		return;
	}

	// A level input re-sent with its current value is not a new edge: a held button cannot advance twice (PUZ-01).
	if (Event.Value.Kind != EDocPuzzleInputKind::Trigger && Context.CurrentInputStates)
	{
		const FDocPuzzleInputValue* Current = Context.CurrentInputStates->Find(Event.InputId);
		if (Current && Current->Matches(Event.Value))
		{
			return;
		}
	}

	int32 CurrentStep = Context.RuleStateInts->FindOrAdd(RuleId, 0);
	if (CurrentStep >= ExpectedSequence.Num())
	{
		return;
	}

	const FDocPuzzleOrderedStep& Step = ExpectedSequence[CurrentStep];
	if (Event.InputId == Step.InputId && Event.Value.Matches(Step.ExpectedValue))
	{
		CurrentStep++;
		Context.RuleStateInts->Add(RuleId, CurrentStep);
	}
	else
	{
		switch (MismatchPolicy)
		{
		case EDocPuzzleOrderedMismatchPolicy::ResetProgress:
			Context.RuleStateInts->Add(RuleId, 0);
			break;
		case EDocPuzzleOrderedMismatchPolicy::FailAttempt:
			Context.bAttemptFailed = true;
			break;
		case EDocPuzzleOrderedMismatchPolicy::IgnoreUnexpected:
			break;
		}
	}
}

bool UDocPuzzleRule_OrderedInputs::Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const
{
	OutProgress.RuleId = RuleId;
	if (ExpectedSequence.IsEmpty())
	{
		OutProgress.bSatisfied = true;
		OutProgress.NormalizedProgress = 1.0f;
		return true;
	}

	const int32 CurrentStep = Context.RuleStateInts ? Context.RuleStateInts->FindRef(RuleId) : 0;
	OutProgress.NormalizedProgress = FMath::Clamp((float)CurrentStep / (float)ExpectedSequence.Num(), 0.0f, 1.0f);
	OutProgress.bSatisfied = (CurrentStep >= ExpectedSequence.Num());

	if (!OutProgress.bSatisfied)
	{
		OutProgress.UnmetReason = LocalizedUnmetReason.IsEmpty() ?
			FText::FromString(FString::Printf(TEXT("Step %d of %d reached"), CurrentStep, ExpectedSequence.Num())) :
			LocalizedUnmetReason;
	}

	return OutProgress.bSatisfied;
}

void UDocPuzzleRule_OrderedInputs::ValidateRule(TArray<FText>& OutErrors) const
{
	Super::ValidateRule(OutErrors);
	if (ExpectedSequence.IsEmpty())
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("OrderedInputs rule '%s' has an empty expected sequence."), *RuleId.ToString())));
	}
	for (int32 Index = 0; Index < ExpectedSequence.Num(); ++Index)
	{
		if (ExpectedSequence[Index].InputId.IsNone())
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("OrderedInputs rule '%s' step %d has None InputId."), *RuleId.ToString(), Index)));
		}
	}
}

void UDocPuzzleRule_OrderedInputs::GetReferencedInputIds(TArray<FName>& OutInputIds) const
{
	for (const FDocPuzzleOrderedStep& Step : ExpectedSequence)
	{
		OutInputIds.AddUnique(Step.InputId);
	}
}

// -----------------------------------------------------------------------------
// UDocPuzzleRule_Simultaneous
// -----------------------------------------------------------------------------

void UDocPuzzleRule_Simultaneous::ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const
{
	if (Context.RuleStateDoubles)
	{
		Context.RuleStateDoubles->Remove(RuleId);
	}
}

bool UDocPuzzleRule_Simultaneous::AreConditionsMet(const FDocPuzzleRuleEvaluationContext& Context, int32& OutMetCount) const
{
	OutMetCount = 0;
	if (!Context.CurrentInputStates)
	{
		return false;
	}
	bool bAll = true;
	for (const FDocPuzzleSimultaneousRequirement& Req : RequiredInputs)
	{
		const FDocPuzzleInputValue* FoundVal = Context.CurrentInputStates->Find(Req.InputId);
		if (FoundVal && FoundVal->Matches(Req.RequiredValue))
		{
			++OutMetCount;
		}
		else
		{
			bAll = false;
		}
	}
	return bAll;
}

void UDocPuzzleRule_Simultaneous::UpdateLevelState(FDocPuzzleRuleEvaluationContext& Context) const
{
	if (!Context.RuleStateDoubles || RequiredInputs.IsEmpty())
	{
		return;
	}
	int32 MetCount = 0;
	if (AreConditionsMet(Context, MetCount))
	{
		// Start the continuous dwell at the first committed moment every condition holds.
		if (!Context.RuleStateDoubles->Contains(RuleId))
		{
			Context.RuleStateDoubles->Add(RuleId, Context.CurrentTime);
		}
	}
	else
	{
		// Continuous dwell broken.
		Context.RuleStateDoubles->Remove(RuleId);
	}
}

bool UDocPuzzleRule_Simultaneous::Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const
{
	OutProgress.RuleId = RuleId;
	if (RequiredInputs.IsEmpty())
	{
		OutProgress.bSatisfied = true;
		OutProgress.NormalizedProgress = 1.0f;
		return true;
	}

	int32 MetCount = 0;
	if (!AreConditionsMet(Context, MetCount))
	{
		OutProgress.NormalizedProgress = (float)MetCount / (float)RequiredInputs.Num() * 0.5f;
		OutProgress.bSatisfied = false;
		OutProgress.UnmetReason = LocalizedUnmetReason.IsEmpty() ?
			FText::FromString(TEXT("Required concurrent inputs not satisfied")) :
			LocalizedUnmetReason;
		return false;
	}

	if (MinDwellSeconds <= 0.0f)
	{
		OutProgress.NormalizedProgress = 1.0f;
		OutProgress.bSatisfied = true;
		return true;
	}

	// Read-only: the dwell start is written by UpdateLevelState at commit time.
	const double* DwellStart = Context.RuleStateDoubles ? Context.RuleStateDoubles->Find(RuleId) : nullptr;
	if (!DwellStart)
	{
		OutProgress.NormalizedProgress = 0.5f;
		OutProgress.bSatisfied = false;
		OutProgress.UnmetReason = LocalizedUnmetReason.IsEmpty() ? FText::FromString(TEXT("Dwell not started")) : LocalizedUnmetReason;
		return false;
	}

	const double Elapsed = Context.CurrentTime - *DwellStart;
	OutProgress.NormalizedProgress = 0.5f + 0.5f * FMath::Clamp((float)(Elapsed / MinDwellSeconds), 0.0f, 1.0f);
	OutProgress.bSatisfied = (Elapsed >= MinDwellSeconds);

	if (!OutProgress.bSatisfied)
	{
		OutProgress.UnmetReason = LocalizedUnmetReason.IsEmpty() ?
			FText::FromString(FString::Printf(TEXT("Dwelling: %.1f / %.1f s"), Elapsed, MinDwellSeconds)) :
			LocalizedUnmetReason;
	}

	return OutProgress.bSatisfied;
}

void UDocPuzzleRule_Simultaneous::ValidateRule(TArray<FText>& OutErrors) const
{
	Super::ValidateRule(OutErrors);
	if (RequiredInputs.IsEmpty())
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("Simultaneous rule '%s' has no required inputs."), *RuleId.ToString())));
	}
	if (!FMath::IsFinite(MinDwellSeconds) || MinDwellSeconds < 0.0f)
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("Simultaneous rule '%s' dwell must be finite and >= 0."), *RuleId.ToString())));
	}
}

void UDocPuzzleRule_Simultaneous::GetReferencedInputIds(TArray<FName>& OutInputIds) const
{
	for (const FDocPuzzleSimultaneousRequirement& Req : RequiredInputs)
	{
		OutInputIds.AddUnique(Req.InputId);
	}
}

// -----------------------------------------------------------------------------
// UDocPuzzleRule_WeightedThreshold
// -----------------------------------------------------------------------------

bool UDocPuzzleRule_WeightedThreshold::Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const
{
	OutProgress.RuleId = RuleId;
	float TotalWeight = 0.0f;

	auto ValueOf = [](const FDocPuzzleInputValue& V)
	{
		return (V.Kind == EDocPuzzleInputKind::Scalar) ? V.ScalarValue : (V.bBoolValue ? 1.0f : 0.0f);
	};

	for (const FDocPuzzleWeightedInput& Item : ContributingInputs)
	{
		// Level state from all active contributors; one contributor leaving removes only its own weight.
		bool bHasContributor = false;
		if (Context.ActiveContributors)
		{
			for (const FDocPuzzleInputContributor& Contrib : *Context.ActiveContributors)
			{
				if (Contrib.InputId == Item.InputId)
				{
					bHasContributor = true;
					TotalWeight += ValueOf(Contrib.Value) * Item.WeightMultiplier;
				}
			}
		}
		if (!bHasContributor && Context.CurrentInputStates)
		{
			if (const FDocPuzzleInputValue* FoundVal = Context.CurrentInputStates->Find(Item.InputId))
			{
				TotalWeight += ValueOf(*FoundVal) * Item.WeightMultiplier;
			}
		}
	}

	const float Target = FMath::Max(RequiredThreshold, 0.001f);
	OutProgress.NormalizedProgress = FMath::Clamp(TotalWeight / Target, 0.0f, 1.0f);
	OutProgress.bSatisfied = (TotalWeight >= RequiredThreshold);

	if (!OutProgress.bSatisfied)
	{
		OutProgress.UnmetReason = LocalizedUnmetReason.IsEmpty() ?
			FText::FromString(FString::Printf(TEXT("Total weight %.1f below required threshold %.1f"), TotalWeight, RequiredThreshold)) :
			LocalizedUnmetReason;
	}

	return OutProgress.bSatisfied;
}

void UDocPuzzleRule_WeightedThreshold::ValidateRule(TArray<FText>& OutErrors) const
{
	Super::ValidateRule(OutErrors);
	if (ContributingInputs.IsEmpty())
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("WeightedThreshold rule '%s' has no contributing inputs."), *RuleId.ToString())));
	}
	if (!(RequiredThreshold > 0.0f) || !FMath::IsFinite(RequiredThreshold))
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("WeightedThreshold rule '%s' required threshold must be > 0."), *RuleId.ToString())));
	}
}

void UDocPuzzleRule_WeightedThreshold::GetReferencedInputIds(TArray<FName>& OutInputIds) const
{
	for (const FDocPuzzleWeightedInput& Item : ContributingInputs)
	{
		OutInputIds.AddUnique(Item.InputId);
	}
}

// -----------------------------------------------------------------------------
// UDocPuzzleRule_SymbolCombination
// -----------------------------------------------------------------------------

bool UDocPuzzleRule_SymbolCombination::Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const
{
	OutProgress.RuleId = RuleId;
	if (RequiredSymbols.IsEmpty())
	{
		OutProgress.bSatisfied = true;
		OutProgress.NormalizedProgress = 1.0f;
		return true;
	}

	if (!Context.CurrentInputStates)
	{
		OutProgress.bSatisfied = false;
		OutProgress.NormalizedProgress = 0.0f;
		OutProgress.UnmetReason = LocalizedUnmetReason;
		return false;
	}

	int32 Matched = 0;
	for (const auto& Kvp : RequiredSymbols)
	{
		const FDocPuzzleInputValue* FoundVal = Context.CurrentInputStates->Find(Kvp.Key);
		if (FoundVal && FoundVal->Kind == EDocPuzzleInputKind::DiscreteSymbol && FoundVal->SymbolValue == Kvp.Value)
		{
			Matched++;
		}
	}

	OutProgress.NormalizedProgress = (float)Matched / (float)RequiredSymbols.Num();
	OutProgress.bSatisfied = (Matched == RequiredSymbols.Num());

	if (!OutProgress.bSatisfied)
	{
		// Count only: the unmet reason never names the required symbols.
		OutProgress.UnmetReason = LocalizedUnmetReason.IsEmpty() ?
			FText::FromString(FString::Printf(TEXT("Symbol combination matches %d of %d"), Matched, RequiredSymbols.Num())) :
			LocalizedUnmetReason;
	}

	return OutProgress.bSatisfied;
}

void UDocPuzzleRule_SymbolCombination::ValidateRule(TArray<FText>& OutErrors) const
{
	Super::ValidateRule(OutErrors);
	if (RequiredSymbols.IsEmpty())
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("SymbolCombination rule '%s' has no required symbols."), *RuleId.ToString())));
	}
}

void UDocPuzzleRule_SymbolCombination::GetReferencedInputIds(TArray<FName>& OutInputIds) const
{
	for (const auto& Kvp : RequiredSymbols)
	{
		OutInputIds.AddUnique(Kvp.Key);
	}
}

// -----------------------------------------------------------------------------
// UDocPuzzleRule_TimedSequence
// -----------------------------------------------------------------------------

void UDocPuzzleRule_TimedSequence::ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const
{
	if (DocPuzzleRulePrivate::DepthExceeded(Context))
	{
		return;
	}
	TGuardValue<int32> DepthGuard(Context.EvaluationDepth, Context.EvaluationDepth + 1);
	if (Context.RuleStateDoubles)
	{
		Context.RuleStateDoubles->Remove(RuleId);
	}
	if (ChildRule)
	{
		ChildRule->ResetRuntimeState(Context);
	}
}

void UDocPuzzleRule_TimedSequence::OnInputSubmitted(const FDocPuzzleInputEvent& Event, FDocPuzzleRuleEvaluationContext& Context) const
{
	if (!ChildRule || DocPuzzleRulePrivate::DepthExceeded(Context))
	{
		return;
	}
	TGuardValue<int32> DepthGuard(Context.EvaluationDepth, Context.EvaluationDepth + 1);

	// Half-open interval: [start, deadline). The window opens at the first accepted input.
	if (Context.RuleStateDoubles)
	{
		const double* StartTime = Context.RuleStateDoubles->Find(RuleId);
		const double WindowStart = StartTime ? *StartTime : Event.AcceptedTimestamp;
		if (!StartTime)
		{
			Context.RuleStateDoubles->Add(RuleId, WindowStart);
		}

		// Evaluate the event against the deadline before applying it: exactly at the deadline is late.
		if (Event.AcceptedTimestamp >= WindowStart + (double)WindowDurationSeconds)
		{
			Context.bAttemptFailed = true;
			return;
		}
	}

	ChildRule->OnInputSubmitted(Event, Context);
}

void UDocPuzzleRule_TimedSequence::UpdateLevelState(FDocPuzzleRuleEvaluationContext& Context) const
{
	if (!ChildRule || DocPuzzleRulePrivate::DepthExceeded(Context))
	{
		return;
	}
	TGuardValue<int32> DepthGuard(Context.EvaluationDepth, Context.EvaluationDepth + 1);
	ChildRule->UpdateLevelState(Context);

	// The window closing fails the attempt unless the child already completed inside it.
	if (Context.RuleStateDoubles)
	{
		if (const double* StartTime = Context.RuleStateDoubles->Find(RuleId))
		{
			if (Context.CurrentTime >= *StartTime + (double)WindowDurationSeconds)
			{
				FDocPuzzleRuleProgress ChildProgress;
				if (!ChildRule->Evaluate(Context, ChildProgress))
				{
					Context.bAttemptFailed = true;
				}
			}
		}
	}
}

bool UDocPuzzleRule_TimedSequence::Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const
{
	OutProgress.RuleId = RuleId;
	if (!ChildRule || DocPuzzleRulePrivate::DepthExceeded(Context))
	{
		OutProgress.bSatisfied = false;
		OutProgress.NormalizedProgress = 0.0f;
		return false;
	}
	TGuardValue<int32> DepthGuard(Context.EvaluationDepth, Context.EvaluationDepth + 1);

	FDocPuzzleRuleProgress ChildProgress;
	const bool bChildSatisfied = ChildRule->Evaluate(Context, ChildProgress);
	OutProgress = ChildProgress;
	OutProgress.RuleId = RuleId;
	if (!bChildSatisfied)
	{
		OutProgress.UnsatisfiedChildRuleIds.AddUnique(ChildProgress.RuleId);
	}
	// A child satisfied within the window stays satisfied; input past the deadline already failed the attempt.
	return bChildSatisfied;
}

void UDocPuzzleRule_TimedSequence::ValidateRule(TArray<FText>& OutErrors) const
{
	Super::ValidateRule(OutErrors);
	if (!ChildRule)
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("TimedSequence rule '%s' has no ChildRule."), *RuleId.ToString())));
	}
	if (!(WindowDurationSeconds > 0.0f) || !FMath::IsFinite(WindowDurationSeconds))
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("TimedSequence rule '%s' window duration must be > 0."), *RuleId.ToString())));
	}
}

void UDocPuzzleRule_TimedSequence::GetChildRules(TArray<const UDocPuzzleRule*>& OutChildren) const
{
	if (ChildRule)
	{
		OutChildren.Add(ChildRule.Get());
	}
}

// -----------------------------------------------------------------------------
// UDocPuzzleRule_Composite
// -----------------------------------------------------------------------------

void UDocPuzzleRule_Composite::ResetRuntimeState(FDocPuzzleRuleEvaluationContext& Context) const
{
	if (DocPuzzleRulePrivate::DepthExceeded(Context))
	{
		return;
	}
	TGuardValue<int32> DepthGuard(Context.EvaluationDepth, Context.EvaluationDepth + 1);
	for (const auto& Child : ChildRules)
	{
		if (Child)
		{
			Child->ResetRuntimeState(Context);
		}
	}
}

void UDocPuzzleRule_Composite::OnInputSubmitted(const FDocPuzzleInputEvent& Event, FDocPuzzleRuleEvaluationContext& Context) const
{
	if (DocPuzzleRulePrivate::DepthExceeded(Context))
	{
		return;
	}
	TGuardValue<int32> DepthGuard(Context.EvaluationDepth, Context.EvaluationDepth + 1);
	for (const auto& Child : ChildRules)
	{
		if (Child)
		{
			Child->OnInputSubmitted(Event, Context);
		}
	}
}

void UDocPuzzleRule_Composite::UpdateLevelState(FDocPuzzleRuleEvaluationContext& Context) const
{
	if (DocPuzzleRulePrivate::DepthExceeded(Context))
	{
		return;
	}
	TGuardValue<int32> DepthGuard(Context.EvaluationDepth, Context.EvaluationDepth + 1);
	for (const auto& Child : ChildRules)
	{
		if (Child)
		{
			Child->UpdateLevelState(Context);
		}
	}
}

bool UDocPuzzleRule_Composite::Evaluate(const FDocPuzzleRuleEvaluationContext& Context, FDocPuzzleRuleProgress& OutProgress) const
{
	OutProgress.RuleId = RuleId;
	if (ChildRules.IsEmpty())
	{
		OutProgress.bSatisfied = true;
		OutProgress.NormalizedProgress = 1.0f;
		return true;
	}
	if (DocPuzzleRulePrivate::DepthExceeded(Context))
	{
		OutProgress.bSatisfied = false;
		OutProgress.NormalizedProgress = 0.0f;
		return false;
	}
	TGuardValue<int32> DepthGuard(Context.EvaluationDepth, Context.EvaluationDepth + 1);

	const bool bAll = (Mode == EDocPuzzleCompositionMode::All);
	bool bAllSatisfied = true;
	bool bAnySatisfied = false;
	float SumProgress = 0.0f;
	float MaxProgress = 0.0f;
	int32 ValidChildren = 0;

	for (const auto& Child : ChildRules)
	{
		if (!Child)
		{
			continue;
		}
		++ValidChildren;
		FDocPuzzleRuleProgress ChildProg;
		const bool bChild = Child->Evaluate(Context, ChildProg);
		SumProgress += ChildProg.NormalizedProgress;
		MaxProgress = FMath::Max(MaxProgress, ChildProg.NormalizedProgress);
		if (bChild)
		{
			// Alternatives resolve deterministically: the first satisfied child in authored order wins.
			if (!bAnySatisfied && !bAll)
			{
				OutProgress.WinningChildRuleId = ChildProg.WinningChildRuleId.IsNone() ? ChildProg.RuleId : ChildProg.WinningChildRuleId;
			}
			bAnySatisfied = true;
		}
		else
		{
			bAllSatisfied = false;
			OutProgress.UnsatisfiedChildRuleIds.AddUnique(ChildProg.RuleId);
		}
		for (const FName& Nested : ChildProg.UnsatisfiedChildRuleIds)
		{
			if (!bChild)
			{
				OutProgress.UnsatisfiedChildRuleIds.AddUnique(Nested);
			}
		}
	}

	const bool bSatisfied = bAll ? bAllSatisfied : bAnySatisfied;
	OutProgress.bSatisfied = bSatisfied;
	OutProgress.NormalizedProgress = bAll ? (ValidChildren > 0 ? SumProgress / (float)ValidChildren : 1.0f) : MaxProgress;
	if (bSatisfied)
	{
		OutProgress.UnsatisfiedChildRuleIds.Reset();
	}
	else if (OutProgress.UnmetReason.IsEmpty())
	{
		OutProgress.UnmetReason = LocalizedUnmetReason;
	}
	return bSatisfied;
}

void UDocPuzzleRule_Composite::ValidateRule(TArray<FText>& OutErrors) const
{
	Super::ValidateRule(OutErrors);
	if (ChildRules.IsEmpty())
	{
		OutErrors.Add(FText::FromString(FString::Printf(TEXT("Composite rule '%s' has no child rules."), *RuleId.ToString())));
	}
	for (const auto& Child : ChildRules)
	{
		if (!Child)
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("Composite rule '%s' has a null child."), *RuleId.ToString())));
		}
	}
}

void UDocPuzzleRule_Composite::GetChildRules(TArray<const UDocPuzzleRule*>& OutChildren) const
{
	for (const auto& Child : ChildRules)
	{
		if (Child)
		{
			OutChildren.Add(Child.Get());
		}
	}
}
