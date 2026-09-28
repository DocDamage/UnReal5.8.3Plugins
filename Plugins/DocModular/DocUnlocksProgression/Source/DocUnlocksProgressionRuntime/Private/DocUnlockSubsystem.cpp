#include "DocUnlockSubsystem.h"
#include "DocUnlocksProgressionLog.h"
#include "DocCoreTags.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/SecureHash.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocUnlockSubsystem)

static TWeakObjectPtr<UDocUnlockSubsystem> GDocUnlockTestOverride;

namespace DocUnlockPrivate
{
	bool IsRetryable(EDocResultOutcome Outcome)
	{
		return !(Outcome == EDocResultOutcome::InvalidInput || Outcome == EDocResultOutcome::InvalidConfiguration
			|| Outcome == EDocResultOutcome::Unsupported || Outcome == EDocResultOutcome::PermissionDenied);
	}

	bool Compare(double Value, EDocUnlockCompare Op, double Threshold)
	{
		switch (Op)
		{
		case EDocUnlockCompare::GreaterOrEqual: return Value >= Threshold;
		case EDocUnlockCompare::Greater: return Value > Threshold;
		case EDocUnlockCompare::LessOrEqual: return Value <= Threshold;
		case EDocUnlockCompare::Less: return Value < Threshold;
		case EDocUnlockCompare::Equal: return FMath::IsNearlyEqual(Value, Threshold);
		}
		return false;
	}

	int64 UtcNow() { return FDateTime::UtcNow().GetTicks(); }
}

using namespace DocUnlockPrivate;

// ---------------------------------------------------------------------------
// Lifetime / plumbing
// ---------------------------------------------------------------------------

UDocUnlockSubsystem* UDocUnlockSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocUnlockSubsystem* Override = GDocUnlockTestOverride.Get())
	{
		return Override;
	}
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UDocUnlockSubsystem>() : nullptr;
}

void UDocUnlockSubsystem::SetSubsystemOverrideForTesting(UDocUnlockSubsystem* Override)
{
	GDocUnlockTestOverride = Override;
}

void UDocUnlockSubsystem::Deinitialize()
{
	PendingEvents.Reset();
	PendingDeliveries.Reset();
	Super::Deinitialize();
}

bool UDocUnlockSubsystem::HasAuthority() const
{
	if (AuthorityOverride.IsSet())
	{
		return AuthorityOverride.GetValue();
	}
	const UWorld* World = AttachedWorld.Get();
	return !World || World->GetNetMode() != NM_Client;
}

const UDocUnlockSettings* UDocUnlockSubsystem::Settings() const
{
	return GetDefault<UDocUnlockSettings>();
}

UDocUnlockSubsystem::FOwnerData& UDocUnlockSubsystem::GetOwnerData(const FDocOwnerScope& Owner)
{
	FOwnerData& Data = Owners.FindOrAdd(Owner);
	if (!Data.Ledger.IsValid())
	{
		Data.Ledger = MakeShared<FDocReceiptLedger>();
	}
	return Data;
}

FGuid UDocUnlockSubsystem::ProducerIdFor(FName UnlockId)
{
	// Deterministic producer id per unlock, so keys survive restarts; ordinals separate transitions.
	const FString Text = FString::Printf(TEXT("DocUnlock:%s"), *UnlockId.ToString().ToLower());
	FMD5 Md5;
	FTCHARToUTF8 Utf8(*Text);
	Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	uint8 Digest[16];
	Md5.Final(Digest);
	uint32 Parts[4];
	FMemory::Memcpy(Parts, Digest, sizeof(Parts));
	return FGuid(Parts[0], Parts[1], Parts[2], Parts[3]);
}

void UDocUnlockSubsystem::AddAudit(const FDocOwnerScope& Owner, FName UnlockId, const FString& Operation, const FString& Reason)
{
	FOwnerData& Data = GetOwnerData(Owner);
	FDocUnlockAuditEntry& Entry = Data.Audit.AddDefaulted_GetRef();
	Entry.UnlockId = UnlockId;
	Entry.Operation = Operation;
	Entry.Reason = Reason;
	Entry.AtUtcTicks = UtcNow();
	const int32 Max = Settings()->MaxAuditEntriesPerOwner;
	if (Max > 0 && Data.Audit.Num() > Max)
	{
		Data.Audit.RemoveAt(0, Data.Audit.Num() - Max);
	}
}

void UDocUnlockSubsystem::QueueChange(const FDocOwnerScope& Owner, FName UnlockId, const FString& Cause)
{
	const FDocUnlockRecord* Record = FindOwnerData(Owner) ? FindOwnerData(Owner)->Records.Find(UnlockId) : nullptr;
	FDocUnlockChange Change;
	Change.Owner = Owner;
	Change.UnlockId = UnlockId;
	Change.bEffectiveAvailable = Record && Record->bLastEffective;
	Change.bPermanentEntitlement = Record && Record->bPermanentEntitlement;
	Change.Cause = Cause;
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocUnlockSubsystem>(this), Change]()
	{
		if (UDocUnlockSubsystem* This = WeakThis.Get())
		{
			This->OnUnlockChangedNative.Broadcast(Change);
			This->OnUnlockChanged.Broadcast(Change);
		}
	});
}

void UDocUnlockSubsystem::LeaveApi()
{
	if (--ApiDepth > 0)
	{
		return;
	}
	++ApiDepth; // callbacks queue further work instead of recursing into the same batch
	int32 Passes = 0;
	while ((PendingEvents.Num() > 0 || PendingDeliveries.Num() > 0) && Passes++ < 64)
	{
		TArray<TFunction<void()>> Events = MoveTemp(PendingEvents);
		PendingEvents.Reset();
		for (TFunction<void()>& Event : Events)
		{
			Event();
		}
		TArray<TPair<FDocOwnerScope, FDocEffectKey>> Deliveries = MoveTemp(PendingDeliveries);
		PendingDeliveries.Reset();
		for (const TPair<FDocOwnerScope, FDocEffectKey>& D : Deliveries)
		{
			DeliverIntent(D.Key, D.Value);
		}
	}
	--ApiDepth;
}

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------

FDocSystemResult UDocUnlockSubsystem::RegisterDefinition(UDocUnlockDefinition* Definition)
{
	return RegisterDefinitions({ Definition });
}

FDocSystemResult UDocUnlockSubsystem::ValidateCatalogChange(const TArray<UDocUnlockDefinition*>& Added) const
{
	TMap<FName, const UDocUnlockDefinition*> Combined;
	for (const TPair<FName, TObjectPtr<UDocUnlockDefinition>>& Pair : Definitions)
	{
		Combined.Add(Pair.Key, Pair.Value);
	}
	TArray<FString> Errors;
	for (const UDocUnlockDefinition* Def : Added)
	{
		if (!Def)
		{
			Errors.Add(TEXT("Null definition"));
			continue;
		}
		TArray<FString> DefErrors, Warnings;
		Def->FindProblems(DefErrors, Warnings);
		Errors.Append(DefErrors);
		if (const UDocUnlockDefinition* const* Existing = Combined.Find(Def->UnlockId))
		{
			if (*Existing != Def)
			{
				Errors.Add(FString::Printf(TEXT("Duplicate UnlockId %s"), *Def->UnlockId.ToString()));
			}
			continue;
		}
		Combined.Add(Def->UnlockId, Def);
	}
	if (!Errors.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Join(Errors, TEXT("; ")));
	}

	// Cycles across the whole loaded catalog, reported with a specific path.
	TMap<FName, int32> Color;
	TArray<FName> Path;
	FString CyclePath;
	TFunction<bool(FName)> Visit = [&](FName Id) -> bool
	{
		const UDocUnlockDefinition* const* Def = Combined.Find(Id);
		if (!Def)
		{
			return false; // missing prerequisite: evaluated as Unavailable, not a cycle
		}
		int32& C = Color.FindOrAdd(Id);
		if (C == 1)
		{
			const int32 Start = Path.IndexOfByKey(Id);
			TArray<FString> Names;
			for (int32 i = Start; i < Path.Num(); ++i) { Names.Add(Path[i].ToString()); }
			Names.Add(Id.ToString());
			CyclePath = FString::Join(Names, TEXT(" -> "));
			return true;
		}
		if (C == 2)
		{
			return false;
		}
		C = 1;
		Path.Add(Id);
		for (FName Next : (*Def)->GetPrerequisiteIds())
		{
			if (Visit(Next)) { return true; }
		}
		Path.Pop();
		Color.FindOrAdd(Id) = 2;
		return false;
	};
	TArray<FName> Ids;
	Combined.GetKeys(Ids);
	Ids.Sort([](FName A, FName B) { return A.LexicalLess(B); });
	for (FName Id : Ids)
	{
		if (Color.FindRef(Id) == 0 && Visit(Id))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration,
				FString::Printf(TEXT("Prerequisite cycle: %s"), *CyclePath), DocUnlockTags::Error_Unlock_Cycle);
		}
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUnlockSubsystem::RegisterDefinitions(const TArray<UDocUnlockDefinition*>& InDefinitions)
{
	FApiScope Scope(*this);
	const FDocSystemResult Valid = ValidateCatalogChange(InDefinitions);
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	TArray<FName> Added;
	for (UDocUnlockDefinition* Def : InDefinitions)
	{
		if (!Definitions.Contains(Def->UnlockId))
		{
			Definitions.Add(Def->UnlockId, Def);
			DefinitionRefs.Add(Def);
			Added.Add(Def->UnlockId);
		}
	}
	if (Added.IsEmpty())
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already registered"));
	}
	RebuildIndexes();
	TArray<FDocOwnerScope> Known;
	Owners.GetKeys(Known);
	for (const FDocOwnerScope& Owner : Known)
	{
		MarkDirty(Owner, Added);
		for (FName Id : Added) { MarkDependentsDirty(Owner, Id); }
		RunImmediate(Owner);
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocUnlockSubsystem::RebuildIndexes()
{
	Dependents.Reset();
	ProgressDependents.Reset();
	ProviderDependents.Reset();
	TagDependents.Reset();
	TopoRank.Reset();
	const UDocUnlockSettings* Cfg = Settings();
	for (const TPair<FName, TObjectPtr<UDocUnlockDefinition>>& Pair : Definitions)
	{
		const UDocUnlockDefinition& Def = *Pair.Value;
		for (FName Pre : Def.GetPrerequisiteIds())
		{
			Dependents.FindOrAdd(Pre).AddUnique(Pair.Key);
		}
		for (const FDocUnlockExpression* Expr : { &Def.Prerequisites, &Def.Conditions, &Def.AvailabilityGate })
		{
			TArray<const FDocUnlockCondition*> Leaves;
			Expr->CollectLeaves(Leaves);
			for (const FDocUnlockCondition* C : Leaves)
			{
				switch (C->Type)
				{
				case EDocUnlockConditionType::NumericThreshold: ProgressDependents.FindOrAdd(C->ProgressKey).AddUnique(Pair.Key); break;
				case EDocUnlockConditionType::GameplayTag:
					TagDependents.FindOrAdd(C->Tag).AddUnique(Pair.Key);
					ProviderDependents.FindOrAdd(Cfg->TagProviderId).AddUnique(Pair.Key);
					break;
				case EDocUnlockConditionType::WorldEvent: ProviderDependents.FindOrAdd(C->ProviderId.IsNone() ? Cfg->EventProviderId : C->ProviderId).AddUnique(Pair.Key); break;
				case EDocUnlockConditionType::Time: ProviderDependents.FindOrAdd(C->ProviderId.IsNone() ? Cfg->TimeProviderId : C->ProviderId).AddUnique(Pair.Key); break;
				case EDocUnlockConditionType::Custom: ProviderDependents.FindOrAdd(C->ProviderId).AddUnique(Pair.Key); break;
				default: break;
				}
			}
		}
	}
	// Deterministic topological order (Kahn, ties by id).
	TMap<FName, int32> InDegree;
	for (const TPair<FName, TObjectPtr<UDocUnlockDefinition>>& Pair : Definitions)
	{
		int32& Degree = InDegree.FindOrAdd(Pair.Key);
		for (FName Pre : Pair.Value->GetPrerequisiteIds())
		{
			Degree += Definitions.Contains(Pre) ? 1 : 0;
		}
	}
	TArray<FName> Ready;
	for (const TPair<FName, int32>& Pair : InDegree)
	{
		if (Pair.Value == 0) { Ready.Add(Pair.Key); }
	}
	int32 Rank = 0;
	while (!Ready.IsEmpty())
	{
		Ready.Sort([](FName A, FName B) { return A.LexicalLess(B); });
		const FName Id = Ready[0];
		Ready.RemoveAt(0);
		TopoRank.Add(Id, Rank++);
		if (const TArray<FName>* Deps = Dependents.Find(Id))
		{
			for (FName D : *Deps)
			{
				if (int32* Degree = InDegree.Find(D))
				{
					if (--(*Degree) == 0) { Ready.Add(D); }
				}
			}
		}
	}
}

void UDocUnlockSubsystem::ValidateCatalog(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const
{
	for (const TPair<FName, TObjectPtr<UDocUnlockDefinition>>& Pair : Definitions)
	{
		Pair.Value->FindProblems(OutErrors, OutWarnings);
		for (FName Pre : Pair.Value->GetPrerequisiteIds())
		{
			if (!Definitions.Contains(Pre))
			{
				OutWarnings.Add(FString::Printf(TEXT("Unlock %s: missing prerequisite %s (evaluates Unavailable)"), *Pair.Key.ToString(), *Pre.ToString()));
			}
		}
	}
	const FDocSystemResult Cycles = ValidateCatalogChange({});
	if (!Cycles.IsSuccess())
	{
		OutErrors.Add(Cycles.Diagnostic);
	}
}

const UDocUnlockDefinition* UDocUnlockSubsystem::FindDefinition(FName UnlockId) const
{
	const TObjectPtr<UDocUnlockDefinition>* Found = Definitions.Find(UnlockId);
	return Found ? Found->Get() : nullptr;
}

void UDocUnlockSubsystem::RegisterConditionProvider(FName ProviderId, TSharedPtr<IDocUnlockConditionProvider> Provider)
{
	if (Provider.IsValid()) { ConditionProviders.Add(ProviderId, Provider); } else { ConditionProviders.Remove(ProviderId); }
	TArray<FDocOwnerScope> Known;
	Owners.GetKeys(Known);
	for (const FDocOwnerScope& Owner : Known)
	{
		NotifyProviderChanged(Owner, ProviderId);
	}
}

void UDocUnlockSubsystem::RegisterEffectProvider(FName ProviderId, TSharedPtr<IDocUnlockEffectProvider> Provider)
{
	if (Provider.IsValid()) { EffectProviders.Add(ProviderId, Provider); } else { EffectProviders.Remove(ProviderId); }
}

void UDocUnlockSubsystem::RegisterOwner(const FDocOwnerScope& Owner)
{
	if (!Owner.IsValid())
	{
		return;
	}
	FApiScope Scope(*this);
	GetOwnerData(Owner);
	TArray<FName> Auto;
	for (const TPair<FName, TObjectPtr<UDocUnlockDefinition>>& Pair : Definitions)
	{
		if (Pair.Value->GetEffectivePolicy() == EDocUnlockEvaluationPolicy::Auto) { Auto.Add(Pair.Key); }
	}
	MarkDirty(Owner, Auto);
	RunImmediate(Owner);
}

// ---------------------------------------------------------------------------
// Pure evaluation
// ---------------------------------------------------------------------------

FDocConditionResult UDocUnlockSubsystem::EvaluateLeaf(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockCondition& C) const
{
	const FOwnerData* Data = FindOwnerData(Owner);
	const UDocUnlockSettings* Cfg = Settings();
	auto ViaProvider = [&](FName ProviderId) -> FDocConditionResult
	{
		const TSharedPtr<IDocUnlockConditionProvider>* Provider = ConditionProviders.Find(ProviderId);
		if (!Provider || !Provider->IsValid())
		{
			return FDocConditionResult::Unavailable(DocUnlockTags::Error_Unlock_ProviderMissing, FString::Printf(TEXT("No condition provider '%s'"), *ProviderId.ToString()));
		}
		FDocUnlockConditionQuery Query;
		Query.Condition = &C;
		Query.Owner = Owner;
		Query.UnlockId = UnlockId;
		return (*Provider)->EvaluateUnlockCondition(Query);
	};
	switch (C.Type)
	{
	case EDocUnlockConditionType::Prerequisite:
	{
		if (!FindDefinition(C.UnlockId))
		{
			return FDocConditionResult::Unavailable(DocUnlockTags::Error_Unlock_MissingPrerequisite, FString::Printf(TEXT("Prerequisite %s is not loaded"), *C.UnlockId.ToString()));
		}
		const FDocUnlockRecord* R = Data ? Data->Records.Find(C.UnlockId) : nullptr;
		const bool bMet = R && (C.bRequireEntitlementOnly ? R->bPermanentEntitlement : R->bLastEffective);
		return bMet ? FDocConditionResult::Satisfied()
			: FDocConditionResult::Unsatisfied(DocUnlockTags::Error_Unlock_PrerequisiteLocked, FString::Printf(TEXT("Prerequisite %s is locked"), *C.UnlockId.ToString()));
	}
	case EDocUnlockConditionType::GameplayTag:
	{
		const TArray<FName>* Claims = Data ? Data->TagClaims.Find(C.Tag) : nullptr;
		if (Claims && Claims->Num() > 0)
		{
			return FDocConditionResult::Satisfied();
		}
		if (ConditionProviders.Contains(Cfg->TagProviderId))
		{
			return ViaProvider(Cfg->TagProviderId);
		}
		return FDocConditionResult::Unsatisfied(DocUnlockTags::Error_Unlock_ConditionUnsatisfied, FString::Printf(TEXT("Tag %s not granted"), *C.Tag.ToString()));
	}
	case EDocUnlockConditionType::NumericThreshold:
	{
		const FDocUnlockProgressValue* V = Data ? Data->Progress.Find(C.ProgressKey) : nullptr;
		const double Value = V ? V->Value : 0.0; // an unset progress value is a legitimate zero
		return Compare(Value, C.Compare, C.Threshold) ? FDocConditionResult::Satisfied()
			: FDocConditionResult::Unsatisfied(DocUnlockTags::Error_Unlock_ConditionUnsatisfied,
				FString::Printf(TEXT("%s = %s does not meet %s"), *C.ProgressKey.ToString(), *FString::SanitizeFloat(Value), *FString::SanitizeFloat(C.Threshold)));
	}
	case EDocUnlockConditionType::WorldEvent: return ViaProvider(C.ProviderId.IsNone() ? Cfg->EventProviderId : C.ProviderId);
	case EDocUnlockConditionType::Time: return ViaProvider(C.ProviderId.IsNone() ? Cfg->TimeProviderId : C.ProviderId);
	case EDocUnlockConditionType::Custom: return ViaProvider(C.ProviderId);
	}
	return FDocConditionResult::Unavailable(DocCoreTags::Error_Unsupported, TEXT("Unknown condition"));
}

FDocConditionResult UDocUnlockSubsystem::EvaluateNode(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockExpression& Expr, int32 Node, int32 Depth, TArray<FDocUnlockBlockingReason>* OutReasons) const
{
	if (!Expr.Nodes.IsValidIndex(Node) || Depth > Settings()->MaxExpressionDepth)
	{
		return FDocConditionResult::Unavailable(DocCoreTags::Error_InvalidConfiguration, TEXT("Malformed expression"));
	}
	const FDocUnlockExprNode& N = Expr.Nodes[Node];
	if (N.Op == EDocUnlockExprOp::Leaf)
	{
		const FDocConditionResult R = EvaluateLeaf(Owner, UnlockId, N.Condition);
		if (OutReasons && !R.IsSatisfied())
		{
			FDocUnlockBlockingReason& Reason = OutReasons->AddDefaulted_GetRef();
			Reason.ReasonTag = R.ReasonTag;
			Reason.State = R.State;
			Reason.Diagnostic = R.Diagnostic;
		}
		return R;
	}
	TArray<FDocConditionResult> Children;
	for (int32 Child : N.Children)
	{
		Children.Add(EvaluateNode(Owner, UnlockId, Expr, Child, Depth + 1, OutReasons));
	}
	switch (N.Op)
	{
	case EDocUnlockExprOp::And: return FDocConditionResult::CombineAll(Children);
	case EDocUnlockExprOp::Or: return FDocConditionResult::CombineAny(Children);
	case EDocUnlockExprOp::Not:
	{
		FDocConditionResult R = Children.Num() == 1 ? Children[0] : FDocConditionResult::Unavailable(DocCoreTags::Error_InvalidConfiguration, TEXT("Not arity"));
		if (!R.IsUnavailable())
		{
			R.State = R.IsSatisfied() ? EDocConditionState::Unsatisfied : EDocConditionState::Satisfied;
		}
		return R;
	}
	default: break;
	}
	return FDocConditionResult::Unavailable(DocCoreTags::Error_InvalidConfiguration, TEXT("Unknown operator"));
}

FDocConditionResult UDocUnlockSubsystem::EvaluateExpression(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockExpression& Expr, TArray<FDocUnlockBlockingReason>* OutReasons) const
{
	return Expr.IsEmpty() ? FDocConditionResult::Satisfied() : EvaluateNode(Owner, UnlockId, Expr, Expr.Root, 1, OutReasons);
}

bool UDocUnlockSubsystem::IsGrantValid(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockTemporaryGrant& Grant) const
{
	switch (Grant.Source.Kind)
	{
	case EDocUnlockTemporaryKind::DurationBased:
		return Grant.RemainingSeconds > 0.0;
	case EDocUnlockTemporaryKind::RegionBased:
	{
		const FOwnerData* Data = FindOwnerData(Owner);
		const bool* Inside = Data ? Data->RegionInside.Find(Grant.Source.RegionId) : nullptr;
		return Inside && *Inside; // recomputed from membership; never a stored pointer
	}
	case EDocUnlockTemporaryKind::ConditionBased:
	{
		const FDocConditionResult R = EvaluateExpression(Owner, UnlockId, Grant.Source.Condition);
		return R.IsSatisfied(); // Unavailable: FailClosed and Suspend are both not-available now
	}
	case EDocUnlockTemporaryKind::SessionBased:
	case EDocUnlockTemporaryKind::Custom:
		return true; // until EndSession / explicit release
	}
	return false;
}

UDocUnlockSubsystem::FEvaluation UDocUnlockSubsystem::Evaluate(const FDocOwnerScope& Owner, const UDocUnlockDefinition& Def, const FDocUnlockRecord* Record, TArray<FDocUnlockBlockingReason>* OutReasons) const
{
	FEvaluation E;
	E.Eligibility = FDocConditionResult::CombineAll({
		EvaluateExpression(Owner, Def.UnlockId, Def.Prerequisites, OutReasons),
		EvaluateExpression(Owner, Def.UnlockId, Def.Conditions, OutReasons) });
	E.Gate = EvaluateExpression(Owner, Def.UnlockId, Def.AvailabilityGate, OutReasons);
	if (Record)
	{
		for (const FDocUnlockTemporaryGrant& G : Record->TemporaryGrants)
		{
			if (IsGrantValid(Owner, Def.UnlockId, G)) { E.bTemporaryValid = true; break; }
		}
	}
	const bool bPermanent = Record && Record->bPermanentEntitlement;
	E.bEntitled = bPermanent || E.bTemporaryValid || (!Def.bPermanent && E.Eligibility.IsSatisfied());
	const bool bDisabled = Record && Record->bAdministrativelyDisabled;
	E.bEffective = !bDisabled && E.bEntitled && E.Gate.IsSatisfied(); // an unavailable gate fails closed
	return E;
}

// ---------------------------------------------------------------------------
// Commit and batches
// ---------------------------------------------------------------------------

bool UDocUnlockSubsystem::Commit(const FDocOwnerScope& Owner, const UDocUnlockDefinition& Def, bool bLatchEligibility, const FString& Cause, bool bSilent)
{
	++EvaluationCount;
	FOwnerData& Data = GetOwnerData(Owner);
	FDocUnlockRecord& R = Data.Records.FindOrAdd(Def.UnlockId);
	R.UnlockId = Def.UnlockId;
	Data.Dirty.Remove(Def.UnlockId);
	FEvaluation E = Evaluate(Owner, Def, &R);
	bool bChanged = false;
	if (bLatchEligibility && Def.bPermanent && !R.bPermanentEntitlement && E.Eligibility.IsSatisfied())
	{
		// Latching: eligibility becomes an earned entitlement that later prerequisite loss does not delete.
		R.bPermanentEntitlement = true;
		R.EntitledAtUtcTicks = UtcNow();
		R.EntitlementSource = TEXT("Eligibility");
		E = Evaluate(Owner, Def, &R);
		bChanged = true;
	}
	R.LastEvaluatedRevision = Data.Revision;
	if (E.bEffective != R.bLastEffective)
	{
		R.bLastEffective = E.bEffective;
		++R.TransitionOrdinal;
		if (!bSilent)
		{
			EnqueueActions(Owner, Def, R, E.bEffective);
		}
		bChanged = true;
		MarkDependentsDirty(Owner, Def.UnlockId);
	}
	if (bChanged)
	{
		++Data.Revision;
		if (!bSilent)
		{
			QueueChange(Owner, Def.UnlockId, Cause);
		}
	}
	return bChanged;
}

void UDocUnlockSubsystem::MarkDirty(const FDocOwnerScope& Owner, const TArray<FName>& UnlockIds)
{
	FOwnerData& Data = GetOwnerData(Owner);
	for (FName Id : UnlockIds)
	{
		const UDocUnlockDefinition* Def = FindDefinition(Id);
		if (Def && Def->GetEffectivePolicy() != EDocUnlockEvaluationPolicy::Manual)
		{
			Data.Dirty.Add(Id);
		}
	}
}

void UDocUnlockSubsystem::MarkDependentsDirty(const FDocOwnerScope& Owner, FName UnlockId)
{
	if (const TArray<FName>* Deps = Dependents.Find(UnlockId))
	{
		MarkDirty(Owner, *Deps);
	}
}

int32 UDocUnlockSubsystem::RunBatch(const FDocOwnerScope& Owner, int32 Budget, bool bAllPolicies)
{
	FOwnerData& Data = GetOwnerData(Owner);
	int32 Processed = 0;
	while (Processed < Budget)
	{
		// Lowest topological rank first (deterministic); only affected nodes are visited.
		FName Next;
		int32 BestRank = MAX_int32;
		for (FName Id : Data.Dirty)
		{
			const UDocUnlockDefinition* Def = FindDefinition(Id);
			if (!Def)
			{
				continue;
			}
			const EDocUnlockEvaluationPolicy Policy = Def->GetEffectivePolicy();
			if (!bAllPolicies && Policy != EDocUnlockEvaluationPolicy::EventDriven && Policy != EDocUnlockEvaluationPolicy::Auto)
			{
				continue; // Batch policy waits for EvaluateDirty (visible as Pending)
			}
			const int32 Rank = TopoRank.FindRef(Id);
			if (Rank < BestRank || (Rank == BestRank && Id.LexicalLess(Next)))
			{
				BestRank = Rank;
				Next = Id;
			}
		}
		if (Next.IsNone())
		{
			break;
		}
		Commit(Owner, *FindDefinition(Next), true, TEXT("Evaluation"));
		++Processed;
	}
	int32 Remaining = 0;
	for (FName Id : Data.Dirty)
	{
		if (FindDefinition(Id)) { ++Remaining; }
	}
	return Remaining;
}

// ---------------------------------------------------------------------------
// Public commands
// ---------------------------------------------------------------------------

FDocSystemResult UDocUnlockSubsystem::EvaluateUnlock(const FDocOwnerScope& Owner, FName UnlockId)
{
	FApiScope Scope(*this);
	const UDocUnlockDefinition* Def = FindDefinition(UnlockId);
	if (!Def || !Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown unlock or owner"), DocUnlockTags::Error_Unlock_UnknownUnlock);
	}
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Unlock state changes on the authority"));
	}
	const bool bChanged = Commit(Owner, *Def, true, TEXT("EvaluateUnlock"));
	RunImmediate(Owner);
	return bChanged ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("Unchanged"));
}

int32 UDocUnlockSubsystem::EvaluateDirty(const FDocOwnerScope& Owner, int32 Budget)
{
	FApiScope Scope(*this);
	if (!HasAuthority() || !Owner.IsValid())
	{
		return 0;
	}
	return RunBatch(Owner, Budget > 0 ? Budget : Settings()->DefaultEvaluationBudget, true);
}

FDocSystemResult UDocUnlockSubsystem::SetProgress(const FDocOwnerScope& Owner, FName ProgressKey, EDocUnlockProgressMode Mode, double Value)
{
	FApiScope Scope(*this);
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Progress changes on the authority"));
	}
	if (!Owner.IsValid() || ProgressKey.IsNone() || !FMath::IsFinite(Value))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Owner, key and a finite value are required"));
	}
	FOwnerData& Data = GetOwnerData(Owner);
	FDocUnlockProgressValue& P = Data.Progress.FindOrAdd(ProgressKey);
	P.Key = ProgressKey;
	const double Next = Mode == EDocUnlockProgressMode::AddDelta ? P.Value + Value : Value;
	const double Max = Settings()->MaxProgressMagnitude;
	if (!FMath::IsFinite(Next) || FMath::Abs(Next) > Max)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Progress %s would exceed +/-%s"), *ProgressKey.ToString(), *FString::SanitizeFloat(Max)));
	}
	const bool bObserved = Mode == EDocUnlockProgressMode::Observed;
	if (Next == P.Value && P.bObserved == bObserved)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Same value")); // no dirty work, no actions
	}
	P.Value = Next;
	P.bObserved = bObserved;
	++Data.Revision;
	if (const TArray<FName>* Deps = ProgressDependents.Find(ProgressKey))
	{
		MarkDirty(Owner, *Deps);
	}
	RunImmediate(Owner);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUnlockSubsystem::GrantPermanent(const FDocOwnerScope& Owner, FName UnlockId, const FString& Reason, const FDocEffectKey& ReceiptKey)
{
	FApiScope Scope(*this);
	const UDocUnlockDefinition* Def = FindDefinition(UnlockId);
	if (!Def || !Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown unlock or owner"), DocUnlockTags::Error_Unlock_UnknownUnlock);
	}
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Entitlements are granted on the authority"));
	}
	FOwnerData& Data = GetOwnerData(Owner);
	const int64 Hash = FDocReceiptLedger::HashString(FString::Printf(TEXT("Grant|%s"), *UnlockId.ToString()));
	if (ReceiptKey.IsValid())
	{
		FDocEffectReceipt Existing;
		switch (Data.Ledger->Check(ReceiptKey, Hash, &Existing))
		{
		case EDocReceiptCheck::Duplicate: return Existing.Result;
		case EDocReceiptCheck::Conflict: return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Receipt key reused with a different payload"));
		default: break;
		}
	}
	FDocUnlockRecord& R = Data.Records.FindOrAdd(UnlockId);
	R.UnlockId = UnlockId;
	FDocSystemResult Result;
	if (R.bPermanentEntitlement)
	{
		Result = FDocSystemResult::MakeNoChange(TEXT("Already entitled"));
	}
	else
	{
		R.bPermanentEntitlement = true;
		R.EntitledAtUtcTicks = UtcNow();
		R.EntitlementSource = Reason;
		++Data.Revision;
		AddAudit(Owner, UnlockId, TEXT("GrantPermanent"), Reason);
		if (!Commit(Owner, *Def, false, TEXT("GrantPermanent")))
		{
			QueueChange(Owner, UnlockId, TEXT("GrantPermanent")); // entitlement changed even if availability did not
		}
		RunImmediate(Owner);
		Result = FDocSystemResult::MakeSuccess();
	}
	if (ReceiptKey.IsValid())
	{
		FDocEffectReceipt Receipt;
		Receipt.Key = ReceiptKey;
		Receipt.PayloadHash = Hash;
		Receipt.Result = Result;
		Data.Ledger->Record(Receipt);
	}
	return Result;
}

FDocSystemResult UDocUnlockSubsystem::RevokePermanent(const FDocOwnerScope& Owner, FName UnlockId, const FString& AuditReason)
{
	FApiScope Scope(*this);
	const UDocUnlockDefinition* Def = FindDefinition(UnlockId);
	if (!Def || !Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown unlock or owner"), DocUnlockTags::Error_Unlock_UnknownUnlock);
	}
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Revocation is an authority operation"));
	}
	if (AuditReason.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Revocation needs an audit reason"));
	}
	FOwnerData& Data = GetOwnerData(Owner);
	FDocUnlockRecord* R = Data.Records.Find(UnlockId);
	if (!R || !R->bPermanentEntitlement)
	{
		return FDocSystemResult::MakeNoChange(TEXT("No permanent entitlement"));
	}
	R->bPermanentEntitlement = false;
	R->EntitlementSource.Reset();
	++Data.Revision;
	AddAudit(Owner, UnlockId, TEXT("RevokePermanent"), AuditReason);
	// Declared LockActions run on the resulting transition; nothing else is assumed reversible.
	if (!Commit(Owner, *Def, false, TEXT("RevokePermanent")))
	{
		QueueChange(Owner, UnlockId, TEXT("RevokePermanent"));
	}
	RunImmediate(Owner);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUnlockSubsystem::AcquireTemporaryGrant(const FDocOwnerScope& Owner, FName UnlockId, const FDocUnlockTemporarySource& Source, FGuid& OutGrantId)
{
	FApiScope Scope(*this);
	const UDocUnlockDefinition* Def = FindDefinition(UnlockId);
	if (!Def || !Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown unlock or owner"), DocUnlockTags::Error_Unlock_UnknownUnlock);
	}
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Grants are issued on the authority"));
	}
	if (Source.SourceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Temporary grants need a SourceId"));
	}
	if (!Def->AllowedTemporaryKinds.IsEmpty() && !Def->AllowedTemporaryKinds.Contains(Source.Kind))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("This unlock does not accept that temporary source"));
	}
	FString Error;
	if ((Source.Kind == EDocUnlockTemporaryKind::DurationBased && Source.DurationSeconds <= 0.f)
		|| (Source.Kind == EDocUnlockTemporaryKind::SessionBased && !Source.SessionId.IsValid())
		|| (Source.Kind == EDocUnlockTemporaryKind::RegionBased && Source.RegionId.IsNone())
		|| (Source.Kind == EDocUnlockTemporaryKind::ConditionBased && (Source.Condition.IsEmpty() || !Source.Condition.Validate(Error))))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Temporary source is incomplete for its kind"));
	}
	FOwnerData& Data = GetOwnerData(Owner);
	FDocUnlockRecord& R = Data.Records.FindOrAdd(UnlockId);
	R.UnlockId = UnlockId;
	FDocUnlockTemporaryGrant& G = R.TemporaryGrants.AddDefaulted_GetRef();
	G.GrantId = FGuid::NewGuid();
	G.Source = Source;
	G.RemainingSeconds = Source.Kind == EDocUnlockTemporaryKind::DurationBased ? static_cast<double>(Source.DurationSeconds) : -1.0;
	G.AcquiredAtUtcTicks = UtcNow();
	OutGrantId = G.GrantId;
	++Data.Revision;
	Commit(Owner, *Def, false, TEXT("TemporaryGrant"));
	RunImmediate(Owner);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUnlockSubsystem::ReleaseTemporaryGrant(const FDocOwnerScope& Owner, FName UnlockId, const FGuid& GrantId, FName SourceId)
{
	FApiScope Scope(*this);
	const UDocUnlockDefinition* Def = FindDefinition(UnlockId);
	FOwnerData* Data = Owners.Find(Owner);
	FDocUnlockRecord* R = Data ? Data->Records.Find(UnlockId) : nullptr;
	if (!Def || !R)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("No such grant"));
	}
	const int32 Index = R->TemporaryGrants.IndexOfByPredicate([&GrantId](const FDocUnlockTemporaryGrant& G) { return G.GrantId == GrantId; });
	if (Index == INDEX_NONE)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already released"));
	}
	if (R->TemporaryGrants[Index].Source.SourceId != SourceId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Only the granting source may release its grant"));
	}
	R->TemporaryGrants.RemoveAt(Index); // other grants and the permanent entitlement are untouched
	++Data->Revision;
	Commit(Owner, *Def, false, TEXT("TemporaryRelease"));
	RunImmediate(Owner);
	return FDocSystemResult::MakeSuccess();
}

void UDocUnlockSubsystem::EndSession(const FGuid& SessionId)
{
	FApiScope Scope(*this);
	TArray<FDocOwnerScope> Known;
	Owners.GetKeys(Known);
	for (const FDocOwnerScope& Owner : Known)
	{
		TArray<FName> Affected;
		for (TPair<FName, FDocUnlockRecord>& Pair : Owners[Owner].Records)
		{
			const int32 Removed = Pair.Value.TemporaryGrants.RemoveAll([&SessionId](const FDocUnlockTemporaryGrant& G)
			{
				return G.Source.Kind == EDocUnlockTemporaryKind::SessionBased && G.Source.SessionId == SessionId;
			});
			if (Removed > 0) { Affected.Add(Pair.Key); }
		}
		for (FName Id : Affected)
		{
			if (const UDocUnlockDefinition* Def = FindDefinition(Id)) { Commit(Owner, *Def, false, TEXT("Session ended")); }
		}
		if (!Affected.IsEmpty()) { RunImmediate(Owner); }
	}
}

void UDocUnlockSubsystem::NotifyRegionMembership(const FDocOwnerScope& Owner, FName RegionId, bool bInside)
{
	FApiScope Scope(*this);
	FOwnerData& Data = GetOwnerData(Owner);
	bool& Slot = Data.RegionInside.FindOrAdd(RegionId);
	if (Slot == bInside)
	{
		return;
	}
	Slot = bInside;
	TArray<FName> Affected;
	for (const TPair<FName, FDocUnlockRecord>& Pair : Data.Records)
	{
		if (Pair.Value.TemporaryGrants.ContainsByPredicate([RegionId](const FDocUnlockTemporaryGrant& G) { return G.Source.Kind == EDocUnlockTemporaryKind::RegionBased && G.Source.RegionId == RegionId; }))
		{
			Affected.Add(Pair.Key);
		}
	}
	for (FName Id : Affected)
	{
		if (const UDocUnlockDefinition* Def = FindDefinition(Id)) { Commit(Owner, *Def, false, TEXT("Region membership")); }
	}
	RunImmediate(Owner);
}

void UDocUnlockSubsystem::NotifyProviderChanged(const FDocOwnerScope& Owner, FName ProviderId)
{
	FApiScope Scope(*this);
	if (const TArray<FName>* Deps = ProviderDependents.Find(ProviderId))
	{
		MarkDirty(Owner, *Deps);
	}
	// Condition-based grants re-check their provider (fail closed while it is missing).
	TArray<FName> WithConditionGrants;
	if (const FOwnerData* Data = FindOwnerData(Owner))
	{
		for (const TPair<FName, FDocUnlockRecord>& Pair : Data->Records)
		{
			if (Pair.Value.TemporaryGrants.ContainsByPredicate([](const FDocUnlockTemporaryGrant& G) { return G.Source.Kind == EDocUnlockTemporaryKind::ConditionBased; }))
			{
				WithConditionGrants.Add(Pair.Key);
			}
		}
	}
	for (FName Id : WithConditionGrants)
	{
		if (const UDocUnlockDefinition* Def = FindDefinition(Id)) { Commit(Owner, *Def, false, TEXT("Provider changed")); }
	}
	RunImmediate(Owner);
}

FDocSystemResult UDocUnlockSubsystem::SetDisabled(const FDocOwnerScope& Owner, FName UnlockId, bool bDisabled, const FString& AuditReason)
{
	FApiScope Scope(*this);
	const UDocUnlockDefinition* Def = FindDefinition(UnlockId);
	if (!Def || !Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown unlock or owner"), DocUnlockTags::Error_Unlock_UnknownUnlock);
	}
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Administrative operation"));
	}
	FOwnerData& Data = GetOwnerData(Owner);
	FDocUnlockRecord& R = Data.Records.FindOrAdd(UnlockId);
	R.UnlockId = UnlockId;
	if (R.bAdministrativelyDisabled == bDisabled)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Unchanged"));
	}
	R.bAdministrativelyDisabled = bDisabled; // entitlement and grants are untouched
	++Data.Revision;
	AddAudit(Owner, UnlockId, bDisabled ? TEXT("Disable") : TEXT("Enable"), AuditReason);
	Commit(Owner, *Def, false, bDisabled ? TEXT("Disabled") : TEXT("Enabled"));
	RunImmediate(Owner);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUnlockSubsystem::SetVisibility(const FDocOwnerScope& Owner, FName UnlockId, EDocUnlockVisibility Visibility)
{
	FApiScope Scope(*this);
	if (!FindDefinition(UnlockId) || !Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown unlock or owner"), DocUnlockTags::Error_Unlock_UnknownUnlock);
	}
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Administrative operation"));
	}
	FOwnerData& Data = GetOwnerData(Owner);
	FDocUnlockRecord& R = Data.Records.FindOrAdd(UnlockId);
	R.UnlockId = UnlockId;
	if (R.Visibility == Visibility)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Unchanged"));
	}
	R.Visibility = Visibility; // presentation only: never an entitlement change
	++Data.Revision;
	return FDocSystemResult::MakeSuccess();
}

void UDocUnlockSubsystem::AdvanceClock(EDocClockDomain Domain, double Seconds)
{
	if (Seconds <= 0.0 || !HasAuthority())
	{
		return; // backward/zero steps never resurrect expired grants
	}
	FApiScope Scope(*this);
	TArray<FDocOwnerScope> Known;
	Owners.GetKeys(Known);
	for (const FDocOwnerScope& Owner : Known)
	{
		TArray<FName> Expired;
		for (TPair<FName, FDocUnlockRecord>& Pair : Owners[Owner].Records)
		{
			bool bAny = false;
			for (int32 i = Pair.Value.TemporaryGrants.Num() - 1; i >= 0; --i)
			{
				FDocUnlockTemporaryGrant& G = Pair.Value.TemporaryGrants[i];
				if (G.Source.Kind != EDocUnlockTemporaryKind::DurationBased || G.Source.Clock != Domain)
				{
					continue;
				}
				G.RemainingSeconds -= Seconds;
				if (G.RemainingSeconds <= 0.0)
				{
					Pair.Value.TemporaryGrants.RemoveAt(i); // expires independently of other grants
					bAny = true;
				}
			}
			if (bAny) { Expired.Add(Pair.Key); }
		}
		for (FName Id : Expired)
		{
			if (const UDocUnlockDefinition* Def = FindDefinition(Id)) { Commit(Owner, *Def, false, TEXT("Grant expired")); }
		}
		if (!Expired.IsEmpty()) { RunImmediate(Owner); }
	}
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

IDocUnlockEffectProvider* UDocUnlockSubsystem::FindEffectProvider(const FDocUnlockAction& Action) const
{
	FName ProviderId = Action.ProviderId;
	if (ProviderId.IsNone())
	{
		ProviderId = Action.Type == EDocUnlockActionType::BroadcastEvent ? Settings()->EventProviderId
			: (Action.Type == EDocUnlockActionType::Custom ? NAME_None : Settings()->TagProviderId);
	}
	const TSharedPtr<IDocUnlockEffectProvider>* Provider = EffectProviders.Find(ProviderId);
	return Provider ? Provider->Get() : nullptr;
}

void UDocUnlockSubsystem::EnqueueActions(const FDocOwnerScope& Owner, const UDocUnlockDefinition& Def, FDocUnlockRecord& Record, bool bUnlocking)
{
	FOwnerData& Data = GetOwnerData(Owner);
	for (const FDocUnlockAction& Action : bUnlocking ? Def.UnlockActions : Def.LockActions)
	{
		bool bDeliver = true;
		if (Action.Type == EDocUnlockActionType::GrantGameplayTag || Action.Type == EDocUnlockActionType::RemoveGameplayTag)
		{
			// Source-owned claims: another entitlement granting the same tag keeps it.
			TArray<FName>& Sources = Data.TagClaims.FindOrAdd(Action.Tag);
			const int32 Before = Sources.Num();
			if (Action.Type == EDocUnlockActionType::GrantGameplayTag) { Sources.AddUnique(Def.UnlockId); } else { Sources.Remove(Def.UnlockId); }
			const int32 After = Sources.Num();
			if (After == 0) { Data.TagClaims.Remove(Action.Tag); }
			// External tag stores only hear about the first claim and the last release.
			bDeliver = (Before == 0 && After > 0) || (Before > 0 && After == 0);
			if (Before != After)
			{
				if (const TArray<FName>* Deps = TagDependents.Find(Action.Tag)) { MarkDirty(Owner, *Deps); }
			}
			if (!bDeliver || !FindEffectProvider(Action))
			{
				continue;
			}
		}
		FDocUnlockActionIntent& Intent = Data.Intents.AddDefaulted_GetRef();
		Intent.Key.Owner = Owner;
		Intent.Key.CampaignEpoch = Owner.CampaignNamespace;
		Intent.Key.ProducerInstanceId = ProducerIdFor(Def.UnlockId);
		Intent.Key.TransitionOrdinal = Record.TransitionOrdinal;
		Intent.Key.ActionId = Action.ActionId;
		Intent.UnlockId = Def.UnlockId;
		Intent.Action = Action;
		PendingDeliveries.Add(TPair<FDocOwnerScope, FDocEffectKey>(Owner, Intent.Key));
	}
}

void UDocUnlockSubsystem::DeliverIntent(const FDocOwnerScope& Owner, const FDocEffectKey& Key)
{
	FOwnerData* Data = Owners.Find(Owner);
	FDocUnlockActionIntent* Intent = Data ? Data->Intents.FindByPredicate([&Key](const FDocUnlockActionIntent& I) { return I.Key == Key; }) : nullptr;
	if (!Intent || Intent->State != EDocUnlockIntentState::Pending)
	{
		return;
	}
	const FDocUnlockAction Action = Intent->Action;
	FDocUnlockEffectRequest Request;
	Request.Action = &Action;
	Request.Key = Key;
	Request.Owner = Owner;
	Request.UnlockId = Intent->UnlockId;
	const FDocUnlockRecord* Record = Data->Records.Find(Intent->UnlockId);
	Request.bUnlocking = Record ? Record->bLastEffective : true;
	IDocUnlockEffectProvider* Provider = FindEffectProvider(Action);
	const FDocSystemResult Result = Provider ? Provider->DeliverUnlockEffect(Request)
		: FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, FString::Printf(TEXT("No effect provider for %s"), *Action.ActionId.ToString()), DocUnlockTags::Error_Unlock_ProviderMissing);
	// Re-find: the provider may have called back into the subsystem.
	Data = Owners.Find(Owner);
	Intent = Data ? Data->Intents.FindByPredicate([&Key](const FDocUnlockActionIntent& I) { return I.Key == Key; }) : nullptr;
	if (!Intent)
	{
		return;
	}
	++Intent->Attempts;
	Intent->LastResult = Result;
	// The entitlement is kept either way; delivery is retried, never re-triggered by toggling.
	Intent->State = Result.IsSuccess() ? EDocUnlockIntentState::Delivered : (IsRetryable(Result.Outcome) ? EDocUnlockIntentState::Pending : EDocUnlockIntentState::Failed);
}

FDocSystemResult UDocUnlockSubsystem::RetryPendingActions(const FDocOwnerScope& Owner)
{
	FApiScope Scope(*this);
	const FOwnerData* Data = FindOwnerData(Owner);
	if (!Data)
	{
		return FDocSystemResult::MakeNoChange(TEXT("No owner data"));
	}
	TArray<FDocEffectKey> Keys;
	for (const FDocUnlockActionIntent& I : Data->Intents)
	{
		if (I.State == EDocUnlockIntentState::Pending) { Keys.Add(I.Key); }
	}
	if (Keys.IsEmpty())
	{
		return FDocSystemResult::MakeNoChange(TEXT("Nothing pending"));
	}
	for (const FDocEffectKey& Key : Keys)
	{
		DeliverIntent(Owner, Key);
	}
	const bool bStillPending = FindOwnerData(Owner)->Intents.ContainsByPredicate([](const FDocUnlockActionIntent& I) { return I.State == EDocUnlockIntentState::Pending; });
	return bStillPending ? FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Some actions are still pending")) : FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

FDocUnlockSnapshot UDocUnlockSubsystem::GetUnlockSnapshot(const FDocOwnerScope& Owner, FName UnlockId) const
{
	FDocUnlockSnapshot S;
	S.UnlockId = UnlockId;
	const UDocUnlockDefinition* Def = FindDefinition(UnlockId);
	const FOwnerData* Data = FindOwnerData(Owner);
	const FDocUnlockRecord* R = Data ? Data->Records.Find(UnlockId) : nullptr;
	if (R)
	{
		S.Facts = *R;
	}
	S.Facts.UnlockId = UnlockId;
	S.OwnerRevision = Data ? Data->Revision : 0;
	if (!Def)
	{
		S.Eligibility = FDocConditionResult::Unavailable(DocUnlockTags::Error_Unlock_UnknownUnlock, TEXT("Unknown unlock"));
		S.bVisible = false;
		return S;
	}
	const FEvaluation E = Evaluate(Owner, *Def, R);
	S.Eligibility = E.Eligibility;
	S.Gate = E.Gate;
	S.bHasValidTemporaryGrant = E.bTemporaryValid;
	S.bEffectiveAvailable = E.bEffective;
	S.bPending = Data && Data->Dirty.Contains(UnlockId);
	S.PendingActions = Data ? Data->Intents.FilterByPredicate([UnlockId](const FDocUnlockActionIntent& I) { return I.UnlockId == UnlockId && I.State == EDocUnlockIntentState::Pending; }).Num() : 0;
	switch (S.Facts.Visibility)
	{
	case EDocUnlockVisibility::ForceVisible: S.bVisible = true; break;
	case EDocUnlockVisibility::ForceHidden: S.bVisible = false; break;
	default: S.bVisible = !(Def->bHiddenUntilAvailable && !E.bEffective); break;
	}
	if (S.Facts.bAdministrativelyDisabled)
	{
		S.Label = EDocUnlockDisplayLabel::Disabled;
	}
	else if (E.bEffective)
	{
		const bool bOwnRight = S.Facts.bPermanentEntitlement || (!Def->bPermanent && E.Eligibility.IsSatisfied());
		S.Label = bOwnRight ? EDocUnlockDisplayLabel::Unlocked : EDocUnlockDisplayLabel::TemporarilyUnlocked;
	}
	else
	{
		S.Label = EDocUnlockDisplayLabel::Locked;
	}
	return S;
}

bool UDocUnlockSubsystem::IsUnlocked(const FDocOwnerScope& Owner, FName UnlockId) const
{
	const FDocUnlockSnapshot S = GetUnlockSnapshot(Owner, UnlockId);
	return S.Facts.bPermanentEntitlement || S.bHasValidTemporaryGrant;
}

bool UDocUnlockSubsystem::IsAvailable(const FDocOwnerScope& Owner, FName UnlockId) const
{
	return GetUnlockSnapshot(Owner, UnlockId).bEffectiveAvailable;
}

TArray<FDocUnlockBlockingReason> UDocUnlockSubsystem::GetBlockingReasons(const FDocOwnerScope& Owner, FName UnlockId) const
{
	TArray<FDocUnlockBlockingReason> Reasons;
	const UDocUnlockDefinition* Def = FindDefinition(UnlockId);
	auto Add = [&Reasons](const FGameplayTag& Tag, const FString& Diagnostic)
	{
		FDocUnlockBlockingReason& R = Reasons.AddDefaulted_GetRef();
		R.ReasonTag = Tag;
		R.Diagnostic = Diagnostic;
	};
	if (!Def)
	{
		Add(DocUnlockTags::Error_Unlock_UnknownUnlock, TEXT("Unknown unlock"));
		return Reasons;
	}
	const FOwnerData* Data = FindOwnerData(Owner);
	const FDocUnlockRecord* R = Data ? Data->Records.Find(UnlockId) : nullptr;
	TArray<FDocUnlockBlockingReason> LeafReasons;
	const FEvaluation E = Evaluate(Owner, *Def, R, &LeafReasons);
	if (E.bEffective)
	{
		return Reasons;
	}
	if (R && R->bAdministrativelyDisabled)
	{
		Add(DocUnlockTags::Error_Unlock_Disabled, TEXT("Administratively disabled"));
	}
	if (!E.bEntitled)
	{
		Reasons.Append(LeafReasons);
		Add(DocUnlockTags::Error_Unlock_NoGrant, TEXT("No permanent entitlement or valid temporary grant"));
	}
	else if (!E.Gate.IsSatisfied())
	{
		FDocUnlockBlockingReason& G = Reasons.AddDefaulted_GetRef();
		G.ReasonTag = DocUnlockTags::Error_Unlock_Gated;
		G.State = E.Gate.State;
		G.Diagnostic = FString::Printf(TEXT("Availability gate: %s"), *E.Gate.Diagnostic);
	}
	return Reasons;
}

FGameplayTagContainer UDocUnlockSubsystem::GetGrantedTags(const FDocOwnerScope& Owner) const
{
	FGameplayTagContainer Tags;
	if (const FOwnerData* Data = FindOwnerData(Owner))
	{
		for (const TPair<FGameplayTag, TArray<FName>>& Pair : Data->TagClaims)
		{
			if (Pair.Value.Num() > 0) { Tags.AddTag(Pair.Key); }
		}
	}
	return Tags;
}

TArray<FDocUnlockAuditEntry> UDocUnlockSubsystem::GetAuditLog(const FDocOwnerScope& Owner) const
{
	const FOwnerData* Data = FindOwnerData(Owner);
	return Data ? Data->Audit : TArray<FDocUnlockAuditEntry>();
}

TArray<FDocUnlockActionIntent> UDocUnlockSubsystem::GetActionIntents(const FDocOwnerScope& Owner) const
{
	const FOwnerData* Data = FindOwnerData(Owner);
	return Data ? Data->Intents : TArray<FDocUnlockActionIntent>();
}

int64 UDocUnlockSubsystem::GetOwnerRevision(const FDocOwnerScope& Owner) const
{
	const FOwnerData* Data = FindOwnerData(Owner);
	return Data ? Data->Revision : 0;
}

FDocSystemResult UDocUnlockSubsystem::ValidateGate(const FDocOwnerScope& Owner, FName UnlockId, int64 ExpectedOwnerRevision) const
{
	if (!HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Gates are validated on the authority; client previews are advisory"));
	}
	if (!FindDefinition(UnlockId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown unlock"), DocUnlockTags::Error_Unlock_UnknownUnlock);
	}
	if (ExpectedOwnerRevision != GetOwnerRevision(Owner))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Unlock state changed since the request was built"), DocUnlockTags::Error_Unlock_StaleRevision);
	}
	if (!IsAvailable(Owner, UnlockId))
	{
		const TArray<FDocUnlockBlockingReason> Reasons = GetBlockingReasons(Owner, UnlockId);
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, Reasons.Num() > 0 ? Reasons[0].Diagnostic : FString(TEXT("Not available")),
			Reasons.Num() > 0 ? Reasons[0].ReasonTag : DocUnlockTags::Error_Unlock_Gated);
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUnlockSubsystem::GetSnapshotForViewer(const FDocOwnerScope& Viewer, const FDocOwnerScope& Owner, FName UnlockId, FDocUnlockSnapshot& OutSnapshot) const
{
	if (Owner.Kind == EDocOwnerScopeKind::PlayerProfile && Viewer != Owner)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Private entitlements are visible to their owner only"), DocUnlockTags::Error_Unlock_Private);
	}
	OutSnapshot = GetUnlockSnapshot(Owner, UnlockId);
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

FDocUnlockSaveData UDocUnlockSubsystem::CaptureState() const
{
	FDocUnlockSaveData Data;
	for (const TPair<FDocOwnerScope, FOwnerData>& Pair : Owners)
	{
		if (!Pair.Key.IsPersistable())
		{
			continue;
		}
		FDocUnlockOwnerSave& Save = Data.Owners.AddDefaulted_GetRef();
		Save.Owner = Pair.Key;
		for (const TPair<FName, FDocUnlockRecord>& R : Pair.Value.Records)
		{
			FDocUnlockRecord Copy = R.Value;
			// Session grants and non-persistent leases never become permanent state.
			Copy.TemporaryGrants.RemoveAll([](const FDocUnlockTemporaryGrant& G)
			{
				return G.Source.Kind == EDocUnlockTemporaryKind::SessionBased || !G.Source.bPersist;
			});
			for (FDocUnlockTemporaryGrant& G : Copy.TemporaryGrants) { G.bRegionInside = false; }
			Save.Records.Add(Copy);
		}
		Save.Records.Append(Pair.Value.Quarantined);
		Save.Records.Sort([](const FDocUnlockRecord& A, const FDocUnlockRecord& B) { return A.UnlockId.LexicalLess(B.UnlockId); });
		Pair.Value.Progress.GenerateValueArray(Save.Progress);
		for (const TPair<FGameplayTag, TArray<FName>>& Claim : Pair.Value.TagClaims)
		{
			FDocUnlockTagClaim& C = Save.TagClaims.AddDefaulted_GetRef();
			C.Tag = Claim.Key;
			C.Sources = Claim.Value;
		}
		Save.Intents = Pair.Value.Intents.FilterByPredicate([](const FDocUnlockActionIntent& I) { return I.State != EDocUnlockIntentState::Delivered; });
		Save.Audit = Pair.Value.Audit;
		if (Pair.Value.Ledger.IsValid()) { Save.Receipts = Pair.Value.Ledger->GetAll(); }
	}
	return Data;
}

FDocSystemResult UDocUnlockSubsystem::RestoreState(const FDocUnlockSaveData& Data)
{
	FApiScope Scope(*this);
	if (Data.SchemaVersion <= 0 || Data.SchemaVersion > FDocUnlockSaveData::CurrentSchemaVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Unsupported unlock schema %d"), Data.SchemaVersion));
	}
	TSet<FDocOwnerScope> Seen;
	for (const FDocUnlockOwnerSave& Save : Data.Owners)
	{
		bool bDup = false;
		Seen.Add(Save.Owner, &bDup);
		if (bDup || !Save.Owner.IsPersistable())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Owner duplicated or not persistable"));
		}
		for (const FDocUnlockProgressValue& P : Save.Progress)
		{
			if (P.Key.IsNone() || !FMath::IsFinite(P.Value))
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Malformed progress value"));
			}
		}
	}

	TMap<FDocOwnerScope, FOwnerData> Staged;
	for (const FDocUnlockOwnerSave& Save : Data.Owners)
	{
		FOwnerData& D = Staged.Add(Save.Owner);
		D.Ledger = MakeShared<FDocReceiptLedger>();
		D.Ledger->RestoreAll(Save.Receipts);
		D.Revision = 1;
		for (const FDocUnlockRecord& R : Save.Records)
		{
			if (FindDefinition(R.UnlockId)) { D.Records.Add(R.UnlockId, R); } else { D.Quarantined.Add(R); }
		}
		for (const FDocUnlockProgressValue& P : Save.Progress) { D.Progress.Add(P.Key, P); }
		for (const FDocUnlockTagClaim& C : Save.TagClaims) { if (C.Sources.Num() > 0) { D.TagClaims.Add(C.Tag, C.Sources); } }
		D.Intents = Save.Intents;
		D.Audit = Save.Audit;
	}
	Owners = MoveTemp(Staged);

	// Derived state is rebuilt; the effective baseline is re-established silently (no actions, no events).
	RebuildIndexes();
	TArray<FName> Order;
	TopoRank.GetKeys(Order);
	Order.Sort([this](FName A, FName B) { return TopoRank.FindRef(A) < TopoRank.FindRef(B); });
	for (TPair<FDocOwnerScope, FOwnerData>& Pair : Owners)
	{
		for (FName Id : Order)
		{
			if (Pair.Value.Records.Contains(Id))
			{
				Commit(Pair.Key, *FindDefinition(Id), false, TEXT("Restore"), true);
			}
		}
		Pair.Value.Dirty.Reset();
		Pair.Value.Revision = 1;
	}
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocUnlockSubsystem>(this)]()
	{
		if (UDocUnlockSubsystem* This = WeakThis.Get())
		{
			This->OnStateRefreshedNative.Broadcast();
			This->OnStateRefreshed.Broadcast();
		}
	});
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// UDocUnlockWorldFacade
// ---------------------------------------------------------------------------

bool UDocUnlockWorldFacade::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocUnlockWorldFacade::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UDocUnlockSubsystem* Service = UDocUnlockSubsystem::Get(&InWorld))
	{
		Service->AttachWorld(&InWorld);
	}
	LastRealSeconds = FPlatformTime::Seconds();
}

void UDocUnlockWorldFacade::Deinitialize()
{
	if (UDocUnlockSubsystem* Service = UDocUnlockSubsystem::Get(GetWorld()))
	{
		Service->DetachWorld(GetWorld());
	}
	Super::Deinitialize();
}

void UDocUnlockWorldFacade::Tick(float DeltaTime)
{
	UDocUnlockSubsystem* Service = UDocUnlockSubsystem::Get(GetWorld());
	if (!Service || !Service->IsAttachedTo(GetWorld()))
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const double RealDelta = LastRealSeconds >= 0.0 ? Now - LastRealSeconds : 0.0;
	LastRealSeconds = Now;
	Service->AdvanceClock(EDocClockDomain::WorldGameplay, DeltaTime); // pauses and dilates with the world
	Service->AdvanceClock(EDocClockDomain::Simulation, DeltaTime);
	Service->AdvanceClock(EDocClockDomain::RealTime, RealDelta);
}

TStatId UDocUnlockWorldFacade::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocUnlockWorldFacade, STATGROUP_Tickables);
}
