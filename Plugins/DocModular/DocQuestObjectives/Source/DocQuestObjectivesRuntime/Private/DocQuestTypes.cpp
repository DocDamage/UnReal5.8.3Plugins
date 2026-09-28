#include "DocQuestTypes.h"
#include "DocQuestObjectivesLog.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocQuestTypes)

namespace DocQuestTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Quest, "Doc.Error.Quest", "DocQuestObjectives errors");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_UnknownDefinition, "Doc.Error.Quest.UnknownDefinition");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_PrerequisitesUnmet, "Doc.Error.Quest.PrerequisitesUnmet");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_AlreadyActive, "Doc.Error.Quest.AlreadyActive");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_NotRepeatable, "Doc.Error.Quest.NotRepeatable");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_Terminal, "Doc.Error.Quest.Terminal");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_OwnerPolicy, "Doc.Error.Quest.OwnerPolicy");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_UntrustedSource, "Doc.Error.Quest.UntrustedSource");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_WrongWorld, "Doc.Error.Quest.WrongWorld");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_Quarantined, "Doc.Error.Quest.Quarantined");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_RewardPending, "Doc.Error.Quest.RewardPending");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_ProviderMissing, "Doc.Error.Quest.ProviderMissing");
	UE_DEFINE_GAMEPLAY_TAG(Error_Quest_NotTrackable, "Doc.Error.Quest.NotTrackable");
}

const FDocQuestStage* UDocQuestDefinition::FindStage(FName StageId) const
{
	return StageId.IsNone() ? nullptr : Stages.FindByPredicate([StageId](const FDocQuestStage& S) { return S.StageId == StageId; });
}

int32 UDocQuestDefinition::FindStageIndex(FName StageId) const
{
	return Stages.IndexOfByPredicate([StageId](const FDocQuestStage& S) { return S.StageId == StageId; });
}

bool UDocQuestDefinition::ApplyLegacyStageMode(FDocQuestStage& Stage, EDocQuestLegacyStageMode Mode)
{
	switch (Mode)
	{
	case EDocQuestLegacyStageMode::AllRequired:
		Stage.Activation = EDocStageActivation::Parallel;
		Stage.Completion = EDocStageCompletion::AllRequired;
		return true;
	case EDocQuestLegacyStageMode::AnyRequired:
		Stage.Activation = EDocStageActivation::Parallel;
		Stage.Completion = EDocStageCompletion::AnyRequired;
		return true;
	case EDocQuestLegacyStageMode::Ordered:
		Stage.Activation = EDocStageActivation::Ordered;
		Stage.Completion = EDocStageCompletion::AllRequired;
		return true;
	case EDocQuestLegacyStageMode::Parallel:
		Stage.Activation = EDocStageActivation::Parallel;
		return false; // completion rule must be chosen explicitly
	}
	return false;
}

void UDocQuestDefinition::FindProblems(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const
{
	if (QuestId.IsNone())
	{
		OutErrors.Add(TEXT("QuestId is required"));
	}
	if (Stages.IsEmpty())
	{
		OutErrors.Add(TEXT("Quest has no stages"));
	}
	TSet<FName> StageIds;
	for (const FDocQuestStage& S : Stages)
	{
		bool bDup = false;
		StageIds.Add(S.StageId, &bDup);
		if (S.StageId.IsNone() || bDup)
		{
			OutErrors.Add(FString::Printf(TEXT("StageId missing or duplicated (%s)"), *S.StageId.ToString()));
		}
	}

	auto CheckActions = [&OutErrors](const TArray<FDocQuestAction>& Actions, const FString& Where)
	{
		TSet<FName> Ids;
		for (const FDocQuestAction& A : Actions)
		{
			bool bDup = false;
			Ids.Add(A.ActionId, &bDup);
			if (A.ActionId.IsNone() || bDup)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: ActionId missing or duplicated (%s)"), *Where, *A.ActionId.ToString()));
			}
			if (A.Type == EDocQuestActionType::Custom && A.ProviderId.IsNone())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: Custom action %s needs ProviderId"), *Where, *A.ActionId.ToString()));
			}
		}
	};
	auto CheckConditions = [&OutErrors](const TArray<FDocQuestCondition>& Conditions, const FString& Where)
	{
		for (const FDocQuestCondition& C : Conditions)
		{
			if (C.Type == EDocQuestConditionType::Provider && C.ProviderId.IsNone())
			{
				OutErrors.Add(Where + TEXT(": provider condition without ProviderId"));
			}
			if (C.Type != EDocQuestConditionType::Provider && C.QuestId.IsNone())
			{
				OutErrors.Add(Where + TEXT(": quest condition without QuestId"));
			}
		}
	};
	auto CheckStageLink = [&](FName Target, const FString& Where)
	{
		if (!Target.IsNone() && !StageIds.Contains(Target))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: unknown stage %s"), *Where, *Target.ToString()));
		}
	};

	CheckConditions(Prerequisites, TEXT("Prerequisites"));
	CheckActions(Rewards, TEXT("Rewards"));

	for (const FDocQuestStage& S : Stages)
	{
		const FString Where = FString::Printf(TEXT("Stage %s"), *S.StageId.ToString());
		TSet<FName> ObjectiveIds;
		int32 Required = 0;
		for (const FDocObjectiveDefinition& O : S.Objectives)
		{
			const FString At = FString::Printf(TEXT("%s objective %s"), *Where, *O.ObjectiveId.ToString());
			bool bDup = false;
			ObjectiveIds.Add(O.ObjectiveId, &bDup);
			if (O.ObjectiveId.IsNone() || bDup) { OutErrors.Add(At + TEXT(": ObjectiveId missing or duplicated")); }
			if (O.TargetCount <= 0) { OutErrors.Add(At + TEXT(": TargetCount must be positive")); }
			const bool bObservation = O.ProgressMode == EDocObjectiveProgressMode::Cumulative
				&& O.Evaluator != EDocObjectiveEvaluator::Wait && O.Evaluator != EDocObjectiveEvaluator::CustomCondition;
			if (bObservation && !O.EventTag.IsValid()) { OutErrors.Add(At + TEXT(": event-driven objective needs EventTag")); }
			if ((O.ProgressMode == EDocObjectiveProgressMode::CurrentState || O.Evaluator == EDocObjectiveEvaluator::CustomCondition) && O.ProviderId.IsNone())
			{
				OutErrors.Add(At + TEXT(": provider-driven objective needs ProviderId"));
			}
			if (O.Evaluator == EDocObjectiveEvaluator::Wait && O.WaitSeconds <= 0.f) { OutWarnings.Add(At + TEXT(": Wait of 0 seconds completes on activation")); }
			CheckConditions(O.Conditions, At);
			CheckActions(O.CompletionActions, At + TEXT(" completion"));
			CheckActions(O.FailureActions, At + TEXT(" failure"));
			if (!O.bOptional) { ++Required; }
		}
		if (Required == 0 && !S.bPassThrough)
		{
			OutErrors.Add(Where + TEXT(": no required objectives (mark bPassThrough for an intentional empty stage)"));
		}
		if (S.OnRequiredObjectiveFailed == EDocQuestFailureAction::BranchToStage && S.FailureBranchStage.IsNone())
		{
			OutErrors.Add(Where + TEXT(": BranchToStage without FailureBranchStage"));
		}
		CheckStageLink(S.NextStage, Where + TEXT(" NextStage"));
		CheckStageLink(S.FailureBranchStage, Where + TEXT(" FailureBranchStage"));
		CheckActions(S.CompletionActions, Where + TEXT(" completion"));
	}

	for (const FDocQuestFailureRule& Rule : FailureRules)
	{
		const FString Where = FString::Printf(TEXT("Failure rule %s"), *Rule.RuleId.ToString());
		if (!Rule.EventTag.IsValid()) { OutErrors.Add(Where + TEXT(": EventTag required")); }
		if (Rule.Action == EDocQuestFailureAction::BranchToStage && Rule.BranchStageId.IsNone()) { OutErrors.Add(Where + TEXT(": BranchToStage without BranchStageId")); }
		CheckStageLink(Rule.BranchStageId, Where);
		CheckStageLink(Rule.StageId, Where);
	}
	for (const TPair<FName, FName>& Redirect : StageRedirects)
	{
		CheckStageLink(Redirect.Value, FString::Printf(TEXT("Stage redirect %s"), *Redirect.Key.ToString()));
	}
	if (MaxRepeats > 0 && !bRepeatable)
	{
		OutWarnings.Add(TEXT("MaxRepeats is ignored for a non-repeatable quest"));
	}
}

#if WITH_EDITOR
EDataValidationResult UDocQuestDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TArray<FString> Errors, Warnings;
	FindProblems(Errors, Warnings);
	for (const FString& E : Errors)
	{
		Context.AddError(FText::FromString(E));
		Result = EDataValidationResult::Invalid;
	}
	for (const FString& W : Warnings)
	{
		Context.AddWarning(FText::FromString(W));
	}
	return Result;
}
#endif
