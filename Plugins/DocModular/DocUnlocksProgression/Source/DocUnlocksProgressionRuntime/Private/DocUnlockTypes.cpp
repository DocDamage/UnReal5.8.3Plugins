#include "DocUnlockTypes.h"
#include "DocUnlocksProgressionLog.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocUnlockTypes)

namespace DocUnlockTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Unlock, "Unlock", "DocUnlocksProgression categories");
	UE_DEFINE_GAMEPLAY_TAG(Unlock_Ability, "Unlock.Ability");
	UE_DEFINE_GAMEPLAY_TAG(Unlock_Area, "Unlock.Area");
	UE_DEFINE_GAMEPLAY_TAG(Unlock_Feature, "Unlock.Feature");
	UE_DEFINE_GAMEPLAY_TAG(Unlock_Item, "Unlock.Item");
	UE_DEFINE_GAMEPLAY_TAG(Unlock_Mode, "Unlock.Mode");
	UE_DEFINE_GAMEPLAY_TAG(Unlock_Travel, "Unlock.Travel");
	UE_DEFINE_GAMEPLAY_TAG(Unlock_Interaction, "Unlock.Interaction");
	UE_DEFINE_GAMEPLAY_TAG(Unlock_Custom, "Unlock.Custom");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Unlock, "Doc.Error.Unlock", "DocUnlocksProgression errors");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_UnknownUnlock, "Doc.Error.Unlock.UnknownUnlock");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_MissingPrerequisite, "Doc.Error.Unlock.MissingPrerequisite");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_PrerequisiteLocked, "Doc.Error.Unlock.PrerequisiteLocked");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_ConditionUnsatisfied, "Doc.Error.Unlock.ConditionUnsatisfied");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_ProviderMissing, "Doc.Error.Unlock.ProviderMissing");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_Disabled, "Doc.Error.Unlock.Disabled");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_NoGrant, "Doc.Error.Unlock.NoGrant");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_Gated, "Doc.Error.Unlock.Gated");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_Cycle, "Doc.Error.Unlock.Cycle");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_StaleRevision, "Doc.Error.Unlock.StaleRevision");
	UE_DEFINE_GAMEPLAY_TAG(Error_Unlock_Private, "Doc.Error.Unlock.Private");
}

// ---------------------------------------------------------------------------
// FDocUnlockExpression
// ---------------------------------------------------------------------------

bool FDocUnlockExpression::Validate(FString& OutError, int32 MaxDepth) const
{
	if (Nodes.IsEmpty())
	{
		return true;
	}
	if (!Nodes.IsValidIndex(Root))
	{
		OutError = FString::Printf(TEXT("root %d out of range"), Root);
		return false;
	}
	TArray<bool> OnPath;
	OnPath.Init(false, Nodes.Num());
	TFunction<bool(int32, int32)> Visit = [&](int32 Index, int32 Depth) -> bool
	{
		if (Depth > MaxDepth)
		{
			OutError = FString::Printf(TEXT("deeper than %d"), MaxDepth);
			return false;
		}
		if (!Nodes.IsValidIndex(Index))
		{
			OutError = FString::Printf(TEXT("child index %d out of range"), Index);
			return false;
		}
		if (OnPath[Index])
		{
			OutError = FString::Printf(TEXT("node %d references itself through its children"), Index);
			return false;
		}
		const FDocUnlockExprNode& N = Nodes[Index];
		switch (N.Op)
		{
		case EDocUnlockExprOp::Leaf:
			if (!N.Children.IsEmpty()) { OutError = FString::Printf(TEXT("leaf %d has children"), Index); return false; }
			return true;
		case EDocUnlockExprOp::Not:
			if (N.Children.Num() != 1) { OutError = FString::Printf(TEXT("Not node %d needs exactly one child"), Index); return false; }
			break;
		default:
			if (N.Children.IsEmpty()) { OutError = FString::Printf(TEXT("And/Or node %d has no children"), Index); return false; }
			break;
		}
		OnPath[Index] = true;
		for (int32 Child : N.Children)
		{
			if (!Visit(Child, Depth + 1)) { return false; }
		}
		OnPath[Index] = false;
		return true;
	};
	return Visit(Root, 1);
}

void FDocUnlockExpression::CollectLeaves(TArray<const FDocUnlockCondition*>& Out) const
{
	for (const FDocUnlockExprNode& N : Nodes)
	{
		if (N.Op == EDocUnlockExprOp::Leaf)
		{
			Out.Add(&N.Condition);
		}
	}
}

FDocUnlockExpression FDocUnlockExpression::Leaf(const FDocUnlockCondition& Condition)
{
	FDocUnlockExpression E;
	FDocUnlockExprNode& N = E.Nodes.AddDefaulted_GetRef();
	N.Op = EDocUnlockExprOp::Leaf;
	N.Condition = Condition;
	return E;
}

FDocUnlockExpression FDocUnlockExpression::Combine(EDocUnlockExprOp Op, const TArray<FDocUnlockExpression>& Parts)
{
	FDocUnlockExpression E;
	E.Nodes.AddDefaulted();
	E.Nodes[0].Op = Op;
	for (const FDocUnlockExpression& Part : Parts)
	{
		if (Part.IsEmpty())
		{
			continue;
		}
		const int32 Offset = E.Nodes.Num();
		for (FDocUnlockExprNode Node : Part.Nodes)
		{
			for (int32& Child : Node.Children) { Child += Offset; }
			E.Nodes.Add(Node);
		}
		E.Nodes[0].Children.Add(Part.Root + Offset);
	}
	return E;
}

// ---------------------------------------------------------------------------
// UDocUnlockDefinition
// ---------------------------------------------------------------------------

TArray<FName> UDocUnlockDefinition::GetPrerequisiteIds() const
{
	TArray<FName> Out;
	for (const FDocUnlockExpression* Expr : { &Prerequisites, &Conditions, &AvailabilityGate })
	{
		TArray<const FDocUnlockCondition*> Leaves;
		Expr->CollectLeaves(Leaves);
		for (const FDocUnlockCondition* C : Leaves)
		{
			if (C->Type == EDocUnlockConditionType::Prerequisite && !C->UnlockId.IsNone())
			{
				Out.AddUnique(C->UnlockId);
			}
		}
	}
	return Out;
}

void UDocUnlockDefinition::FindProblems(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const
{
	const FString Where = FString::Printf(TEXT("Unlock %s"), *UnlockId.ToString());
	if (UnlockId.IsNone())
	{
		OutErrors.Add(TEXT("UnlockId is required"));
	}
	const TPair<const TCHAR*, const FDocUnlockExpression*> Exprs[] = {
		{ TEXT("Prerequisites"), &Prerequisites }, { TEXT("Conditions"), &Conditions }, { TEXT("AvailabilityGate"), &AvailabilityGate } };
	for (const TPair<const TCHAR*, const FDocUnlockExpression*>& Pair : Exprs)
	{
		FString Error;
		if (!Pair.Value->Validate(Error, GetDefault<UDocUnlockSettings>()->MaxExpressionDepth))
		{
			OutErrors.Add(FString::Printf(TEXT("%s %s: invalid expression (%s)"), *Where, Pair.Key, *Error));
		}
		TArray<const FDocUnlockCondition*> Leaves;
		Pair.Value->CollectLeaves(Leaves);
		for (const FDocUnlockCondition* C : Leaves)
		{
			switch (C->Type)
			{
			case EDocUnlockConditionType::Prerequisite:
				if (C->UnlockId.IsNone()) { OutErrors.Add(Where + TEXT(": prerequisite without UnlockId")); }
				if (C->UnlockId == UnlockId) { OutErrors.Add(FString::Printf(TEXT("%s: self-edge %s -> %s"), *Where, *UnlockId.ToString(), *UnlockId.ToString())); }
				break;
			case EDocUnlockConditionType::GameplayTag:
				if (!C->Tag.IsValid()) { OutErrors.Add(Where + TEXT(": tag condition without Tag")); }
				break;
			case EDocUnlockConditionType::NumericThreshold:
				if (C->ProgressKey.IsNone()) { OutErrors.Add(Where + TEXT(": threshold without ProgressKey")); }
				if (!FMath::IsFinite(C->Threshold)) { OutErrors.Add(Where + TEXT(": non-finite threshold")); }
				break;
			case EDocUnlockConditionType::Custom:
				if (C->ProviderId.IsNone()) { OutErrors.Add(Where + TEXT(": custom condition without ProviderId")); }
				break;
			default:
				break;
			}
		}
	}
	TSet<FName> ActionIds;
	for (const TArray<FDocUnlockAction>* Actions : { &UnlockActions, &LockActions })
	{
		for (const FDocUnlockAction& A : *Actions)
		{
			bool bDup = false;
			ActionIds.Add(A.ActionId, &bDup);
			if (A.ActionId.IsNone() || bDup) { OutErrors.Add(FString::Printf(TEXT("%s: ActionId missing or duplicated (%s)"), *Where, *A.ActionId.ToString())); }
			if ((A.Type == EDocUnlockActionType::GrantGameplayTag || A.Type == EDocUnlockActionType::RemoveGameplayTag) && !A.Tag.IsValid())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: tag action %s without Tag"), *Where, *A.ActionId.ToString()));
			}
			if (A.Type == EDocUnlockActionType::Custom && A.ProviderId.IsNone())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: custom action %s without ProviderId"), *Where, *A.ActionId.ToString()));
			}
		}
	}
	if (!Category.IsValid())
	{
		OutWarnings.Add(Where + TEXT(": no category"));
	}
}

#if WITH_EDITOR
EDataValidationResult UDocUnlockDefinition::IsDataValid(FDataValidationContext& Context) const
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
