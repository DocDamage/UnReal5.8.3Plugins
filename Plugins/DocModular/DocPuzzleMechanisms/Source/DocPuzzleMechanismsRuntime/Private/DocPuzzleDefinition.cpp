#include "DocPuzzleDefinition.h"

namespace DocPuzzleDefinitionPrivate
{
	struct FGraphWalk
	{
		TArray<FText>& Errors;
		TArray<const UDocPuzzleRule*> Path;
		TSet<const UDocPuzzleRule*> Visited;
		TMap<FName, const UDocPuzzleRule*> RuleIds;
		TArray<FName> ReferencedInputs;
		int32 NodeCount = 0;
		bool bTooLarge = false;

		explicit FGraphWalk(TArray<FText>& InErrors) : Errors(InErrors) {}

		void Visit(const UDocPuzzleRule* Rule, int32 Depth)
		{
			if (!Rule || bTooLarge)
			{
				return;
			}
			if (Path.Contains(Rule))
			{
				Errors.Add(FText::FromString(FString::Printf(TEXT("Rule graph has a cycle through '%s'."), *Rule->RuleId.ToString())));
				return;
			}
			if (Depth > UDocPuzzleDefinition::MaxGraphDepth)
			{
				Errors.Add(FText::FromString(FString::Printf(TEXT("Rule graph is deeper than %d at '%s'."), UDocPuzzleDefinition::MaxGraphDepth, *Rule->RuleId.ToString())));
				return;
			}
			if (Visited.Contains(Rule))
			{
				return; // Shared subgraph (a DAG), already validated.
			}
			Visited.Add(Rule);
			if (++NodeCount > UDocPuzzleDefinition::MaxGraphNodes)
			{
				Errors.Add(FText::FromString(FString::Printf(TEXT("Rule graph has more than %d rules."), UDocPuzzleDefinition::MaxGraphNodes)));
				bTooLarge = true;
				return;
			}

			Rule->ValidateRule(Errors);
			if (!Rule->RuleId.IsNone())
			{
				if (const UDocPuzzleRule* const* Existing = RuleIds.Find(Rule->RuleId))
				{
					if (*Existing != Rule)
					{
						Errors.Add(FText::FromString(FString::Printf(TEXT("Duplicate RuleId '%s' (rule state is keyed by RuleId)."), *Rule->RuleId.ToString())));
					}
				}
				else
				{
					RuleIds.Add(Rule->RuleId, Rule);
				}
			}
			Rule->GetReferencedInputIds(ReferencedInputs);

			TArray<const UDocPuzzleRule*> Children;
			Rule->GetChildRules(Children);
			Path.Push(Rule);
			for (const UDocPuzzleRule* Child : Children)
			{
				Visit(Child, Depth + 1);
			}
			Path.Pop();
		}
	};
}

bool UDocPuzzleDefinition::ValidateDefinition(TArray<FText>& OutErrors) const
{
	const int32 ErrorsBefore = OutErrors.Num();

	if (DefinitionId.IsNone())
	{
		OutErrors.Add(FText::FromString(TEXT("DefinitionId is None.")));
	}

	TSet<FName> SeenInputIds;
	for (const FDocPuzzleInputDefinition& InputDef : Inputs)
	{
		if (InputDef.InputId.IsNone())
		{
			OutErrors.Add(FText::FromString(TEXT("Input definition has None InputId.")));
		}
		else if (SeenInputIds.Contains(InputDef.InputId))
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("Duplicate InputId '%s' in puzzle definition."), *InputDef.InputId.ToString())));
		}
		else
		{
			SeenInputIds.Add(InputDef.InputId);
		}

		if (InputDef.AcceptedKind == EDocPuzzleInputKind::Scalar && InputDef.MinScalar > InputDef.MaxScalar)
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("InputId '%s' has MinScalar > MaxScalar."), *InputDef.InputId.ToString())));
		}
		if (!FMath::IsFinite(InputDef.DebounceSeconds) || InputDef.DebounceSeconds < 0.0f)
		{
			OutErrors.Add(FText::FromString(FString::Printf(TEXT("InputId '%s' has an invalid debounce."), *InputDef.InputId.ToString())));
		}
	}

	if (!RootRule)
	{
		OutErrors.Add(FText::FromString(TEXT("RootRule is null.")));
	}
	else
	{
		DocPuzzleDefinitionPrivate::FGraphWalk Walk(OutErrors);
		Walk.Visit(RootRule.Get(), 1);

		// Missing inputs: only checked when the definition declares its inputs.
		if (!Inputs.IsEmpty())
		{
			for (const FName& Referenced : Walk.ReferencedInputs)
			{
				if (!Referenced.IsNone() && !SeenInputIds.Contains(Referenced))
				{
					OutErrors.Add(FText::FromString(FString::Printf(TEXT("A rule references undeclared input '%s'."), *Referenced.ToString())));
				}
			}
		}
	}

	if (!FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds < 0.0f)
	{
		OutErrors.Add(FText::FromString(TEXT("TimeoutSeconds must be finite and >= 0.")));
	}

	return OutErrors.Num() == ErrorsBefore;
}
