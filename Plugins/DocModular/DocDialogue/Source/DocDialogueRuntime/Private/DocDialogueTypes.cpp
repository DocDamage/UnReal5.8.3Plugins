#include "DocDialogueTypes.h"
#include "DocDialogueLog.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocDialogueTypes)

namespace DocDialogueTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_Dialogue, "Doc.Error.Dialogue", "DocDialogue errors");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_StaleRevision, "Doc.Error.Dialogue.StaleRevision");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_ChoiceNotFound, "Doc.Error.Dialogue.ChoiceNotFound");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_ChoiceHidden, "Doc.Error.Dialogue.ChoiceHidden");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_ChoiceDisabled, "Doc.Error.Dialogue.ChoiceDisabled");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_Unauthorized, "Doc.Error.Dialogue.Unauthorized");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_WrongState, "Doc.Error.Dialogue.WrongState");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_NotSkippable, "Doc.Error.Dialogue.NotSkippable");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_RunawayLoop, "Doc.Error.Dialogue.RunawayLoop");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_MissingNode, "Doc.Error.Dialogue.MissingNode");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_NoChoices, "Doc.Error.Dialogue.NoChoices");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_ReservationConflict, "Doc.Error.Dialogue.ReservationConflict");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_ProviderMissing, "Doc.Error.Dialogue.ProviderMissing");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_VariableMissing, "Doc.Error.Dialogue.VariableMissing");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_VariableType, "Doc.Error.Dialogue.VariableType");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_ParticipantMissing, "Doc.Error.Dialogue.ParticipantMissing");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_ParticipantLost, "Doc.Error.Dialogue.ParticipantLost");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_ActionFailed, "Doc.Error.Dialogue.ActionFailed");
	UE_DEFINE_GAMEPLAY_TAG(Error_Dialogue_UnresolvedAction, "Doc.Error.Dialogue.UnresolvedAction");
}

// ---------------------------------------------------------------------------
// FDocDialogueValue
// ---------------------------------------------------------------------------

bool FDocDialogueValue::ToNumber(double& Out) const
{
	switch (Type)
	{
	case EDocDialogueValueType::Bool: Out = bBool ? 1.0 : 0.0; return true;
	case EDocDialogueValueType::Int: Out = static_cast<double>(Int); return true;
	case EDocDialogueValueType::Float: Out = Float; return true;
	default: return false;
	}
}

bool FDocDialogueValue::CoerceTo(EDocDialogueValueType Target, FDocDialogueValue& Out) const
{
	if (Type == EDocDialogueValueType::None || Target == EDocDialogueValueType::None)
	{
		return false;
	}
	if (Type == Target)
	{
		Out = *this;
		return true;
	}
	if (Target == EDocDialogueValueType::Name || Type == EDocDialogueValueType::Name)
	{
		return false;
	}
	double Number = 0.0;
	if (!ToNumber(Number))
	{
		return false;
	}
	switch (Target)
	{
	case EDocDialogueValueType::Bool: Out = MakeBool(Number != 0.0); return true;
	case EDocDialogueValueType::Int: Out = MakeInt(static_cast<int64>(FMath::RoundHalfFromZero(Number))); return true;
	case EDocDialogueValueType::Float: Out = MakeFloat(Number); return true;
	default: return false;
	}
}

FString FDocDialogueValue::ToString() const
{
	switch (Type)
	{
	case EDocDialogueValueType::Bool: return bBool ? TEXT("true") : TEXT("false");
	case EDocDialogueValueType::Int: return FString::Printf(TEXT("%lld"), Int);
	case EDocDialogueValueType::Float: return FString::SanitizeFloat(Float);
	case EDocDialogueValueType::Name: return Name.ToString();
	default: return TEXT("<none>");
	}
}

// ---------------------------------------------------------------------------
// UDocDialogueGraph
// ---------------------------------------------------------------------------

const FDocDialogueNode* UDocDialogueGraph::FindNode(FName NodeId) const
{
	return NodeId.IsNone() ? nullptr : Nodes.FindByPredicate([NodeId](const FDocDialogueNode& N) { return N.NodeId == NodeId; });
}

const FDocDialogueRole* UDocDialogueGraph::FindRole(FName RoleId) const
{
	return RoleId.IsNone() ? nullptr : Roles.FindByPredicate([RoleId](const FDocDialogueRole& R) { return R.RoleId == RoleId; });
}

const FDocDialogueVariableDecl* UDocDialogueGraph::FindVariable(FName Name) const
{
	return Name.IsNone() ? nullptr : Variables.FindByPredicate([Name](const FDocDialogueVariableDecl& V) { return V.Name == Name; });
}

namespace DocDialogueGraphPrivate
{
	void CollectEdges(const FDocDialogueNode& N, TArray<FName>& Out)
	{
		auto Add = [&Out](FName Id) { if (!Id.IsNone()) { Out.AddUnique(Id); } };
		Add(N.Next);
		for (const FDocDialogueChoice& C : N.Choices) { Add(C.Destination); }
		Add(N.NoChoiceDestination);
		for (const FDocDialogueBranchCase& Case : N.Cases) { Add(Case.Destination); }
		Add(N.FailDestination);
		Add(N.UnavailableDestination);
	}

	/** Nodes that transition immediately and unconditionally (no wait, no condition). */
	bool IsUnconditionalImmediate(const FDocDialogueNode& N)
	{
		return N.Type == EDocDialogueNodeType::Jump || N.Type == EDocDialogueNodeType::Event;
	}
}

void UDocDialogueGraph::FindProblems(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const
{
	using namespace DocDialogueGraphPrivate;

	if (GraphId.IsNone())
	{
		OutErrors.Add(TEXT("GraphId is required"));
	}

	TMap<FName, const FDocDialogueNode*> ById;
	for (const FDocDialogueNode& N : Nodes)
	{
		if (N.NodeId.IsNone())
		{
			OutErrors.Add(TEXT("Node with empty NodeId"));
			continue;
		}
		if (ById.Contains(N.NodeId))
		{
			OutErrors.Add(FString::Printf(TEXT("Duplicate NodeId %s"), *N.NodeId.ToString()));
			continue;
		}
		ById.Add(N.NodeId, &N);
	}

	if (StartNode.IsNone() || !ById.Contains(StartNode))
	{
		OutErrors.Add(FString::Printf(TEXT("Missing start node (%s)"), *StartNode.ToString()));
	}

	TSet<FName> RoleIds;
	for (const FDocDialogueRole& R : Roles)
	{
		bool bDup = false;
		RoleIds.Add(R.RoleId, &bDup);
		if (R.RoleId.IsNone() || bDup)
		{
			OutErrors.Add(FString::Printf(TEXT("Role id missing or duplicated (%s)"), *R.RoleId.ToString()));
		}
	}

	TSet<FName> VarIds;
	for (const FDocDialogueVariableDecl& V : Variables)
	{
		bool bDup = false;
		VarIds.Add(V.Name, &bDup);
		if (V.Name.IsNone() || bDup)
		{
			OutErrors.Add(FString::Printf(TEXT("Variable name missing or duplicated (%s)"), *V.Name.ToString()));
		}
		if (V.Type == EDocDialogueValueType::None)
		{
			OutErrors.Add(FString::Printf(TEXT("Variable %s has no type"), *V.Name.ToString()));
		}
		FDocDialogueValue Coerced;
		if (V.Default.IsSet() && !V.Default.CoerceTo(V.Type, Coerced))
		{
			OutErrors.Add(FString::Printf(TEXT("Variable %s default (%s) is not convertible to its type"), *V.Name.ToString(), *V.Default.ToString()));
		}
	}

	auto CheckRole = [&](FName Role, const FString& Where)
	{
		if (!Role.IsNone() && !FindRole(Role))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: missing participant role %s"), *Where, *Role.ToString()));
		}
	};
	auto CheckLink = [&](FName Dest, const FString& Where)
	{
		if (!Dest.IsNone() && !ById.Contains(Dest))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: broken link to %s"), *Where, *Dest.ToString()));
		}
	};
	auto CheckCondition = [&](const FDocDialogueCondition& C, const FString& Where)
	{
		switch (C.Type)
		{
		case EDocDialogueConditionType::TagPresent:
		case EDocDialogueConditionType::TagMissing:
			if (!C.Tag.IsValid()) { OutErrors.Add(Where + TEXT(": tag condition without Tag")); }
			CheckRole(C.Role, Where);
			break;
		case EDocDialogueConditionType::ParticipantTag:
			if (!C.Tag.IsValid() || C.Role.IsNone()) { OutErrors.Add(Where + TEXT(": ParticipantTag needs Role and Tag")); }
			CheckRole(C.Role, Where);
			break;
		case EDocDialogueConditionType::NumericComparison:
		{
			const FDocDialogueVariableDecl* Decl = FindVariable(C.VariableName);
			if (!Decl) { OutErrors.Add(FString::Printf(TEXT("%s: undeclared variable %s"), *Where, *C.VariableName.ToString())); }
			else if (Decl->Type == EDocDialogueValueType::Name) { OutErrors.Add(FString::Printf(TEXT("%s: variable %s is not numeric"), *Where, *C.VariableName.ToString())); }
			break;
		}
		case EDocDialogueConditionType::EventState:
			if (C.QueryId.IsNone() && !C.Tag.IsValid()) { OutErrors.Add(Where + TEXT(": EventState needs QueryId or Tag")); }
			break;
		case EDocDialogueConditionType::CustomProvider:
		case EDocDialogueConditionType::InterfaceQuery:
			if (C.ProviderId.IsNone()) { OutErrors.Add(Where + TEXT(": provider condition without ProviderId")); }
			CheckRole(C.Role, Where);
			break;
		case EDocDialogueConditionType::NodeVisited:
		case EDocDialogueConditionType::ChoiceTaken:
			if (C.QueryId.IsNone()) { OutErrors.Add(Where + TEXT(": history condition without QueryId")); }
			break;
		}
	};
	auto CheckActions = [&](const TArray<FDocDialogueAction>& Actions, const FString& Where)
	{
		TSet<FName> Ids;
		for (const FDocDialogueAction& A : Actions)
		{
			const FString At = FString::Printf(TEXT("%s action %s"), *Where, *A.ActionId.ToString());
			bool bDup = false;
			Ids.Add(A.ActionId, &bDup);
			if (A.ActionId.IsNone() || bDup) { OutErrors.Add(At + TEXT(": ActionId missing or duplicated")); }
			switch (A.Type)
			{
			case EDocDialogueActionType::SetVariable:
			case EDocDialogueActionType::AddVariable:
			{
				const FDocDialogueVariableDecl* Decl = FindVariable(A.VariableName);
				FDocDialogueValue Tmp;
				if (!Decl) { OutErrors.Add(At + TEXT(": undeclared variable")); }
				else if (A.Type == EDocDialogueActionType::SetVariable && !A.Value.CoerceTo(Decl->Type, Tmp)) { OutErrors.Add(At + TEXT(": value type is not convertible")); }
				else if (A.Type == EDocDialogueActionType::AddVariable && (Decl->Type == EDocDialogueValueType::Name || !A.Value.IsNumeric())) { OutErrors.Add(At + TEXT(": AddVariable needs numeric variable and value")); }
				break;
			}
			case EDocDialogueActionType::BroadcastEvent:
				if (!A.Tag.IsValid()) { OutErrors.Add(At + TEXT(": BroadcastEvent needs Tag")); }
				break;
			case EDocDialogueActionType::GrantTag:
			case EDocDialogueActionType::RemoveTag:
				if (!A.Tag.IsValid() || A.Role.IsNone()) { OutErrors.Add(At + TEXT(": tag action needs Role and Tag")); }
				CheckRole(A.Role, At);
				break;
			case EDocDialogueActionType::Custom:
				if (A.ProviderId.IsNone()) { OutErrors.Add(At + TEXT(": Custom action needs ProviderId")); }
				break;
			}
		}
	};

	TSet<FName> ChoiceIds;
	for (const FDocDialogueNode& N : Nodes)
	{
		const FString Where = FString::Printf(TEXT("Node %s"), *N.NodeId.ToString());
		switch (N.Type)
		{
		case EDocDialogueNodeType::Line:
			CheckRole(N.SpeakerRole, Where);
			if (N.Duration < 0.f) { OutErrors.Add(Where + TEXT(": negative Duration")); }
			break;
		case EDocDialogueNodeType::Choice:
			if (N.Choices.IsEmpty()) { OutErrors.Add(Where + TEXT(": Choice node without choices")); }
			for (const FDocDialogueChoice& C : N.Choices)
			{
				const FString At = FString::Printf(TEXT("%s choice %s"), *Where, *C.ChoiceId.ToString());
				bool bDup = false;
				ChoiceIds.Add(C.ChoiceId, &bDup);
				if (C.ChoiceId.IsNone() || bDup) { OutErrors.Add(At + TEXT(": ChoiceId missing or duplicated (must be unique in the graph)")); }
				for (const FDocDialogueCondition& Cond : C.Conditions) { CheckCondition(Cond, At); }
				for (const FDocDialogueCondition& Cond : C.HiddenConditions) { CheckCondition(Cond, At); }
				for (const FDocDialogueCondition& Cond : C.DisabledConditions) { CheckCondition(Cond, At); }
				CheckActions(C.Actions, At);
				CheckLink(C.Destination, At);
			}
			CheckLink(N.NoChoiceDestination, Where);
			break;
		case EDocDialogueNodeType::Branch:
			if (N.Cases.IsEmpty()) { OutErrors.Add(Where + TEXT(": Branch without cases")); }
			for (const FDocDialogueBranchCase& Case : N.Cases)
			{
				for (const FDocDialogueCondition& Cond : Case.Conditions) { CheckCondition(Cond, Where); }
				CheckLink(Case.Destination, Where);
			}
			break;
		case EDocDialogueNodeType::Condition:
			if (N.Conditions.IsEmpty()) { OutWarnings.Add(Where + TEXT(": Condition node without conditions always passes")); }
			for (const FDocDialogueCondition& Cond : N.Conditions) { CheckCondition(Cond, Where); }
			break;
		case EDocDialogueNodeType::Event:
			CheckActions(N.Actions, Where);
			break;
		case EDocDialogueNodeType::Jump:
			if (N.Next.IsNone()) { OutErrors.Add(Where + TEXT(": Jump without target")); }
			break;
		case EDocDialogueNodeType::Delay:
			if (N.DelaySeconds < 0.f) { OutErrors.Add(Where + TEXT(": negative DelaySeconds")); }
			break;
		case EDocDialogueNodeType::End:
			break;
		case EDocDialogueNodeType::Custom:
			if (N.CustomType.IsNone()) { OutErrors.Add(Where + TEXT(": Custom node without CustomType")); }
			break;
		}
		CheckLink(N.Next, Where);
		CheckLink(N.FailDestination, Where);
		CheckLink(N.UnavailableDestination, Where);
	}

	for (const TPair<FName, FName>& Redirect : NodeRedirects)
	{
		CheckLink(Redirect.Value, FString::Printf(TEXT("Redirect %s"), *Redirect.Key.ToString()));
	}

	// Reachability (warnings).
	if (const FDocDialogueNode* const* Start = ById.Find(StartNode))
	{
		TSet<FName> Seen;
		TArray<FName> Stack = { StartNode };
		while (!Stack.IsEmpty())
		{
			const FName Id = Stack.Pop(EAllowShrinking::No);
			bool bAlready = false;
			Seen.Add(Id, &bAlready);
			if (bAlready) { continue; }
			if (const FDocDialogueNode* const* N = ById.Find(Id))
			{
				TArray<FName> Edges;
				CollectEdges(**N, Edges);
				Stack.Append(Edges);
			}
		}
		for (const FDocDialogueNode& N : Nodes)
		{
			if (!N.NodeId.IsNone() && !Seen.Contains(N.NodeId) && !N.bIntentionallyUnreachable)
			{
				OutWarnings.Add(FString::Printf(TEXT("Node %s is unreachable"), *N.NodeId.ToString()));
			}
		}
	}

	// Unconditional zero-wait cycles (Jump/Event chains that loop back). Errors: they can never yield.
	TMap<FName, int32> Color; // 0 white, 1 grey, 2 black
	TFunction<bool(FName, TArray<FName>&)> Visit = [&](FName Id, TArray<FName>& Path) -> bool
	{
		const FDocDialogueNode* const* N = ById.Find(Id);
		if (!N || !IsUnconditionalImmediate(**N)) { return false; }
		int32& C = Color.FindOrAdd(Id);
		if (C == 1) { Path.Add(Id); return true; }
		if (C == 2) { return false; }
		C = 1;
		Path.Add(Id);
		if (!(*N)->Next.IsNone() && Visit((*N)->Next, Path)) { return true; }
		Path.Pop();
		Color.FindOrAdd(Id) = 2;
		return false;
	};
	for (const FDocDialogueNode& N : Nodes)
	{
		TArray<FName> Path;
		if (IsUnconditionalImmediate(N) && Color.FindRef(N.NodeId) == 0 && Visit(N.NodeId, Path))
		{
			TArray<FString> Names;
			for (FName P : Path) { Names.Add(P.ToString()); }
			OutErrors.Add(FString::Printf(TEXT("Unconditional zero-wait cycle: %s"), *FString::Join(Names, TEXT(" -> "))));
		}
	}
}

#if WITH_EDITOR
EDataValidationResult UDocDialogueGraph::IsDataValid(FDataValidationContext& Context) const
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
