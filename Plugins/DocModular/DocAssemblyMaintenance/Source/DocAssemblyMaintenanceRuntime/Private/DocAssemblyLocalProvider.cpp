#include "DocAssemblyLocalProvider.h"

bool UDocAssemblyLocalProvider::HasToolCapability(const FGuid& OperatorId, const FGameplayTag& ToolTag)
{
	return OperatorId.IsValid() && ToolTag.IsValid() && GrantedTools.HasTag(ToolTag);
}

bool UDocAssemblyLocalProvider::QueryMachineState(bool& bOutPowered, bool& bOutLockedOut)
{
	bOutPowered = bPowered;
	bOutLockedOut = bLockedOut;
	return bMachineStateKnown;
}

bool UDocAssemblyLocalProvider::ReserveExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId, FName& OutPartDefinitionId)
{
	const FName* Def = Stock.Find(PartInstanceId);
	if (!Def || Reserved.Contains(PartInstanceId))
	{
		return false;
	}
	OutPartDefinitionId = *Def;
	Reserved.Add(PartInstanceId);
	return true;
}

EDocAssemblyTransferOutcome UDocAssemblyLocalProvider::CommitExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId)
{
	if (!Reserved.Contains(PartInstanceId))
	{
		return EDocAssemblyTransferOutcome::Failed;
	}
	const EDocAssemblyTransferOutcome Outcome = NextCommitOutcome;
	NextCommitOutcome = EDocAssemblyTransferOutcome::Committed;
	if (Outcome == EDocAssemblyTransferOutcome::Committed)
	{
		Reserved.Remove(PartInstanceId);
		Stock.Remove(PartInstanceId);
		Transferred.Add(PartInstanceId);
	}
	else if (Outcome == EDocAssemblyTransferOutcome::Failed)
	{
		Reserved.Remove(PartInstanceId);
	}
	return Outcome;
}

void UDocAssemblyLocalProvider::ReleaseExternalPart(const FGuid& OperatorId, const FGuid& PartInstanceId)
{
	Reserved.Remove(PartInstanceId);
}

EDocAssemblyTransferOutcome UDocAssemblyLocalProvider::QueryExternalTransfer(const FGuid& OperatorId, const FGuid& PartInstanceId)
{
	if (Transferred.Contains(PartInstanceId))
	{
		return EDocAssemblyTransferOutcome::Committed;
	}
	if (InDoubtResolution == EDocAssemblyTransferOutcome::Committed && Reserved.Contains(PartInstanceId))
	{
		Reserved.Remove(PartInstanceId);
		Stock.Remove(PartInstanceId);
		Transferred.Add(PartInstanceId);
		return EDocAssemblyTransferOutcome::Committed;
	}
	if (InDoubtResolution == EDocAssemblyTransferOutcome::Failed)
	{
		Reserved.Remove(PartInstanceId);
		return EDocAssemblyTransferOutcome::Failed;
	}
	return EDocAssemblyTransferOutcome::InDoubt;
}
