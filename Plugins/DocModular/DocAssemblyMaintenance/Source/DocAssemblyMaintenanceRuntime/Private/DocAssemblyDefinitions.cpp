#include "DocAssemblyDefinitions.h"

const UDocAssemblyPartDefinition* UDocAssemblyDefinition::FindPartDefinition(FName PartDefinitionId) const
{
	for (const TObjectPtr<UDocAssemblyPartDefinition>& Def : PartDefinitions)
	{
		if (Def && Def->PartDefinitionId == PartDefinitionId)
		{
			return Def.Get();
		}
	}
	return nullptr;
}

bool UDocAssemblyDefinition::IsCompatible(FName SlotId, FName PartDefinitionId) const
{
	const FDocPartSlotDefinition* Slot = FindSlot(SlotId);
	const UDocAssemblyPartDefinition* Part = FindPartDefinition(PartDefinitionId);
	if (!Slot || !Part)
	{
		return false;
	}
	if (Slot->AllowedPartTags.IsEmpty() && Slot->AllowedPartDefinitionIds.IsEmpty())
	{
		return true;
	}
	return Slot->AllowedPartDefinitionIds.Contains(PartDefinitionId)
		|| (!Slot->AllowedPartTags.IsEmpty() && Part->PartTags.HasAny(Slot->AllowedPartTags));
}

FDocSystemResult UDocAssemblyDefinition::ValidateDefinition() const
{
	auto Fail = [](const FString& Why) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, Why); };
	if (AssemblyId.IsNone())
	{
		return Fail(TEXT("AssemblyId is None"));
	}
	TSet<FName> PartDefIds;
	for (const TObjectPtr<UDocAssemblyPartDefinition>& Def : PartDefinitions)
	{
		bool bDup = false;
		if (!Def || Def->PartDefinitionId.IsNone())
		{
			return Fail(TEXT("Null or unnamed part definition"));
		}
		PartDefIds.Add(Def->PartDefinitionId, &bDup);
		if (bDup)
		{
			return Fail(FString::Printf(TEXT("Duplicate part definition %s"), *Def->PartDefinitionId.ToString()));
		}
	}
	TSet<FName> FastenerIds;
	for (const FDocFastenerDefinition& F : Fasteners)
	{
		bool bDup = false;
		FastenerIds.Add(F.FastenerId, &bDup);
		if (F.FastenerId.IsNone() || bDup)
		{
			return Fail(TEXT("Fastener ids must be unique and non-empty"));
		}
	}
	TSet<FName> SlotIds;
	for (const FDocPartSlotDefinition& S : Slots)
	{
		bool bDup = false;
		SlotIds.Add(S.SlotId, &bDup);
		if (S.SlotId.IsNone() || bDup)
		{
			return Fail(TEXT("Slot ids must be unique and non-empty"));
		}
		if (S.Capacity != 1)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("Slot %s: only capacity 1 is supported"), *S.SlotId.ToString()));
		}
	}
	for (const FDocPartSlotDefinition& S : Slots)
	{
		for (FName F : S.RequiredFastenerIds)
		{
			if (!FastenerIds.Contains(F))
			{
				return Fail(FString::Printf(TEXT("Slot %s references unknown fastener %s"), *S.SlotId.ToString(), *F.ToString()));
			}
		}
		for (FName O : S.ObstructingSlotIds)
		{
			if (O == S.SlotId || !SlotIds.Contains(O))
			{
				return Fail(FString::Printf(TEXT("Slot %s has an invalid obstruction %s"), *S.SlotId.ToString(), *O.ToString()));
			}
		}
		if (!S.ParentSlotId.IsNone() && !SlotIds.Contains(S.ParentSlotId))
		{
			return Fail(FString::Printf(TEXT("Slot %s has unknown parent %s"), *S.SlotId.ToString(), *S.ParentSlotId.ToString()));
		}
		// Parent chain must terminate (acyclic).
		FName Cursor = S.ParentSlotId;
		for (int32 Depth = 0; !Cursor.IsNone(); ++Depth)
		{
			if (Cursor == S.SlotId || Depth > Slots.Num())
			{
				return Fail(FString::Printf(TEXT("Slot %s has a cyclic parent chain"), *S.SlotId.ToString()));
			}
			const FDocPartSlotDefinition* Parent = FindSlot(Cursor);
			Cursor = Parent ? Parent->ParentSlotId : NAME_None;
		}
	}
	for (const FDocDiagnosticTestDefinition& D : Diagnostics)
	{
		if (D.TestId.IsNone())
		{
			return Fail(TEXT("Diagnostic without TestId"));
		}
		for (FName S : D.EvaluatedSlotIds)
		{
			if (!SlotIds.Contains(S))
			{
				return Fail(FString::Printf(TEXT("Diagnostic %s evaluates unknown slot %s"), *D.TestId.ToString(), *S.ToString()));
			}
		}
	}
	TSet<FGuid> PartIds;
	TSet<FName> Filled;
	for (const FDocPartInstance& P : InitialParts)
	{
		if (!PartDefIds.Contains(P.PartDefinitionId))
		{
			return Fail(FString::Printf(TEXT("Initial part uses unknown definition %s"), *P.PartDefinitionId.ToString()));
		}
		if (P.PartInstanceId.IsValid())
		{
			bool bDup = false;
			PartIds.Add(P.PartInstanceId, &bDup);
			if (bDup)
			{
				return Fail(TEXT("Duplicate initial PartInstanceId"));
			}
		}
		if (!P.InstalledSlotId.IsNone())
		{
			bool bDup = false;
			Filled.Add(P.InstalledSlotId, &bDup);
			if (bDup || !IsCompatible(P.InstalledSlotId, P.PartDefinitionId))
			{
				return Fail(FString::Printf(TEXT("Initial part in slot %s is duplicated or incompatible"), *P.InstalledSlotId.ToString()));
			}
		}
	}
	return FDocSystemResult::MakeSuccess();
}
