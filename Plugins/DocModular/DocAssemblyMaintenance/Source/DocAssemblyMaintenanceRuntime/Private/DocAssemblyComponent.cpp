#include "DocAssemblyComponent.h"
#include "DocAssemblySubsystem.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

UDocAssemblyComponent::UDocAssemblyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocAssemblyComponent::OnRegister()
{
	Super::OnRegister();
	if (AssemblyDefinition && AssemblyState.AssemblyId.IsNone())
	{
		InitializeAssembly(AssemblyDefinition);
	}
	if (UWorld* World = GetWorld())
	{
		if (UDocAssemblySubsystem* Subsystem = World->GetSubsystem<UDocAssemblySubsystem>())
		{
			Subsystem->RegisterAssembly(this);
		}
	}
}

void UDocAssemblyComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocAssemblySubsystem* Subsystem = World->GetSubsystem<UDocAssemblySubsystem>())
		{
			Subsystem->UnregisterAssembly(this);
		}
	}
	Super::OnUnregister();
}

IDocAssemblyResourceProvider* UDocAssemblyComponent::GetProvider() const
{
	return ResourceProviderObject ? Cast<IDocAssemblyResourceProvider>(ResourceProviderObject.Get()) : nullptr;
}

FDocSystemResult UDocAssemblyComponent::SetResourceProvider(UObject* Provider)
{
	if (Provider && !Cast<IDocAssemblyResourceProvider>(Provider))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Provider does not implement IDocAssemblyResourceProvider"));
	}
	ResourceProviderObject = Provider;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocAssemblyComponent::InitializeAssembly(UDocAssemblyDefinition* InDefinition)
{
	if (!InDefinition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No assembly definition"));
	}
	const FDocSystemResult Valid = InDefinition->ValidateDefinition();
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	AssemblyDefinition = InDefinition;
	AssemblyState = FDocAssemblyState();
	AssemblyState.AssemblyId = InDefinition->AssemblyId;
	AssemblyState.Revision = 1;
	ActiveClaims.Reset();
	StagedOperations.Reset();
	InDoubtOperations.Reset();
	ExternalDefinitions.Reset();
	PendingPresentationParts.Reset();
	VisualSpawnCounts.Reset();
	PresentationAttempts.Reset();

	for (const FDocFastenerDefinition& FastenerDef : InDefinition->Fasteners)
	{
		FDocFastenerRecord Record;
		Record.FastenerId = FastenerDef.FastenerId;
		AssemblyState.Fasteners.Add(Record.FastenerId, Record);
	}
	for (const FDocPartInstance& Part : InDefinition->InitialParts)
	{
		FDocPartInstance Instance = Part;
		if (!Instance.PartInstanceId.IsValid())
		{
			Instance.PartInstanceId = FGuid::NewGuid();
		}
		if (!Instance.InstalledSlotId.IsNone())
		{
			Instance.LocationKind = EDocPartLocationKind::Installed;
			AssemblyState.InstalledParts.Add(Instance.InstalledSlotId, Instance);
			TrySpawnPresentation(Instance.PartInstanceId);
		}
		else
		{
			Instance.LocationKind = EDocPartLocationKind::Detached;
			AssemblyState.DetachedParts.Add(Instance.PartInstanceId, Instance);
		}
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Sessions and claims
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocAssemblyComponent::BeginMaintenanceSession(FGuid OperatorId, FGuid& OutSessionId)
{
	OutSessionId.Invalidate();
	if (!OperatorId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Operator id required"));
	}
	if (!AssemblyDefinition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Assembly not initialized"));
	}
	OutSessionId = FGuid::NewGuid();
	Sessions.Add(OutSessionId, OperatorId);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocAssemblyComponent::EndMaintenanceSession(FGuid SessionId)
{
	if (!Sessions.Contains(SessionId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown session"));
	}
	TArray<FGuid> ToCancel;
	for (const TPair<FGuid, FDocAssemblyOperation>& Kvp : StagedOperations)
	{
		if (Kvp.Value.SessionId == SessionId)
		{
			ToCancel.Add(Kvp.Key);
		}
	}
	for (const FGuid& Id : ToCancel)
	{
		AbortStaged(Id, EDocAssemblyTransactionState::RolledBack, TEXT("Session ended"));
	}
	ActiveClaims.RemoveAll([SessionId](const FDocAssemblyClaim& C) { return C.SessionId == SessionId && !C.OperationId.IsValid(); });
	Sessions.Remove(SessionId);
	return FDocSystemResult::MakeSuccess();
}

bool UDocAssemblyComponent::IsClaimedAgainst(const FTargets& Targets, const FGuid& OperatorId, const FGuid& IgnoreOperationId) const
{
	for (const FDocAssemblyClaim& Claim : ActiveClaims)
	{
		if (IgnoreOperationId.IsValid() && Claim.OperationId == IgnoreOperationId)
		{
			continue;
		}
		const bool bOverlap = (!Targets.Slot.IsNone() && Claim.TargetSlotId == Targets.Slot)
			|| (!Targets.Fastener.IsNone() && Claim.TargetFastenerId == Targets.Fastener)
			|| (Claim.PartInstanceId.IsValid() && Targets.Parts.Contains(Claim.PartInstanceId));
		if (!bOverlap)
		{
			continue;
		}
		// Another operator's lease, or any other in-flight operation (even our own), blocks.
		if (Claim.OperatorId != OperatorId || Claim.OperationId.IsValid())
		{
			return true;
		}
	}
	return false;
}

FDocSystemResult UDocAssemblyComponent::AcquireClaim(FGuid SessionId, FName TargetSlotId, FName TargetFastenerId)
{
	const FGuid* Operator = Sessions.Find(SessionId);
	if (!Operator)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Unknown session"));
	}
	FTargets Targets;
	Targets.Slot = TargetSlotId;
	Targets.Fastener = TargetFastenerId;
	if (IsClaimedAgainst(Targets, *Operator, FGuid()))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Target is held by another operator or operation"));
	}
	FDocAssemblyClaim Claim;
	Claim.SessionId = SessionId;
	Claim.OperatorId = *Operator;
	Claim.TargetSlotId = TargetSlotId;
	Claim.TargetFastenerId = TargetFastenerId;
	ActiveClaims.Add(Claim);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocAssemblyComponent::ReleaseClaim(FGuid SessionId, FName TargetSlotId, FName TargetFastenerId)
{
	const int32 Removed = ActiveClaims.RemoveAll([&](const FDocAssemblyClaim& C)
	{
		return C.SessionId == SessionId && !C.OperationId.IsValid() && C.TargetSlotId == TargetSlotId && C.TargetFastenerId == TargetFastenerId;
	});
	return Removed > 0 ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("No such lease"));
}

void UDocAssemblyComponent::ReleaseOperationClaims(const FGuid& OperationId)
{
	ActiveClaims.RemoveAll([OperationId](const FDocAssemblyClaim& C) { return C.OperationId == OperationId; });
}

// ---------------------------------------------------------------------------------------------
// Structure queries
// ---------------------------------------------------------------------------------------------

bool UDocAssemblyComponent::IsSlotReachable(const FDocPartSlotDefinition& Slot) const
{
	for (FName Obstruction : Slot.ObstructingSlotIds)
	{
		if (AssemblyState.InstalledParts.Contains(Obstruction))
		{
			return false;
		}
	}
	return Slot.ParentSlotId.IsNone() || AssemblyState.InstalledParts.Contains(Slot.ParentSlotId);
}

bool UDocAssemblyComponent::IsSlotAccessible(FName SlotId) const
{
	const FDocPartSlotDefinition* Slot = AssemblyDefinition ? AssemblyDefinition->FindSlot(SlotId) : nullptr;
	return Slot && IsSlotReachable(*Slot);
}

bool UDocAssemblyComponent::AreFastenersReleased(FName SlotId) const
{
	const FDocPartSlotDefinition* Slot = AssemblyDefinition ? AssemblyDefinition->FindSlot(SlotId) : nullptr;
	if (!Slot)
	{
		return false;
	}
	for (FName FastenerId : Slot->RequiredFastenerIds)
	{
		const FDocFastenerRecord* Record = AssemblyState.Fasteners.Find(FastenerId);
		if (!Record || Record->State != EDocFastenerState::Released)
		{
			return false;
		}
	}
	return true;
}

bool UDocAssemblyComponent::IsPartInstalled(const FGuid& PartInstanceId) const
{
	for (const TPair<FName, FDocPartInstance>& Kvp : AssemblyState.InstalledParts)
	{
		if (Kvp.Value.PartInstanceId == PartInstanceId)
		{
			return true;
		}
	}
	return false;
}

int32 UDocAssemblyComponent::CountPartLocations(const FGuid& PartInstanceId) const
{
	int32 Count = AssemblyState.DetachedParts.Contains(PartInstanceId) ? 1 : 0;
	for (const TPair<FName, FDocPartInstance>& Kvp : AssemblyState.InstalledParts)
	{
		Count += Kvp.Value.PartInstanceId == PartInstanceId ? 1 : 0;
	}
	for (const FDocPartInstance& Q : AssemblyState.QuarantinedParts)
	{
		Count += Q.PartInstanceId == PartInstanceId ? 1 : 0;
	}
	return Count;
}

const FDocPartInstance* UDocAssemblyComponent::FindPart(const FGuid& PartInstanceId) const
{
	for (const TPair<FName, FDocPartInstance>& Kvp : AssemblyState.InstalledParts)
	{
		if (Kvp.Value.PartInstanceId == PartInstanceId)
		{
			return &Kvp.Value;
		}
	}
	if (const FDocPartInstance* Detached = AssemblyState.DetachedParts.Find(PartInstanceId))
	{
		return Detached;
	}
	return AssemblyState.QuarantinedParts.FindByPredicate([&](const FDocPartInstance& Q) { return Q.PartInstanceId == PartInstanceId; });
}

const FDocPartInstance* UDocAssemblyComponent::GetInstalledPartInSlot(FName SlotId) const
{
	return AssemblyState.InstalledParts.Find(SlotId);
}

const FDocAssemblyReceipt* UDocAssemblyComponent::FindReceipt(const FGuid& OperationId) const
{
	for (int32 Index = AssemblyState.Receipts.Num() - 1; Index >= 0; --Index)
	{
		if (AssemblyState.Receipts[Index].OperationId == OperationId)
		{
			return &AssemblyState.Receipts[Index];
		}
	}
	return nullptr;
}

int32 UDocAssemblyComponent::GetVisualMeshSpawnCount(const FGuid& PartInstanceId) const
{
	const int32* Count = VisualSpawnCounts.Find(PartInstanceId);
	return Count ? *Count : 0;
}

void UDocAssemblyComponent::QueryAssembly(FDocAssemblyState& OutState) const
{
	OutState = AssemblyState;
	for (TPair<FName, FDocPartInstance>& Kvp : OutState.InstalledParts)
	{
		Kvp.Value.HiddenFaults.Reset();
	}
	for (TPair<FGuid, FDocPartInstance>& Kvp : OutState.DetachedParts)
	{
		Kvp.Value.HiddenFaults.Reset();
	}
	for (FDocPartInstance& Q : OutState.QuarantinedParts)
	{
		Q.HiddenFaults.Reset();
	}
}

FDocAssemblyProgress UDocAssemblyComponent::QueryProcedureProgress() const
{
	FDocAssemblyProgress Progress;
	Progress.Revision = AssemblyState.Revision;
	for (const TPair<FName, FDocFastenerRecord>& Kvp : AssemblyState.Fasteners)
	{
		Progress.ReleasedFasteners += Kvp.Value.State == EDocFastenerState::Released ? 1 : 0;
	}
	if (AssemblyDefinition)
	{
		for (const FDocPartSlotDefinition& Slot : AssemblyDefinition->Slots)
		{
			Progress.EmptySlots += AssemblyState.InstalledParts.Contains(Slot.SlotId) ? 0 : 1;
		}
	}
	Progress.PendingOperations = StagedOperations.Num();
	Progress.InDoubtTransfers = InDoubtOperations.Num();
	Progress.bFunctionalTestCurrent = AssemblyState.LastFunctionalPassRevision == AssemblyState.Revision;
	Progress.bProcedureComplete = AssemblyState.bProcedureComplete;
	return Progress;
}

// ---------------------------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------------------------

UDocAssemblyComponent::FTargets UDocAssemblyComponent::GetTargets(const FDocAssemblyOperation& Op) const
{
	FTargets T;
	switch (Op.OperationType)
	{
	case EDocAssemblyOperationType::ReleaseFastener:
	case EDocAssemblyOperationType::SecureFastener:
		T.Fastener = Op.TargetFastenerId;
		break;
	case EDocAssemblyOperationType::RemovePart:
	case EDocAssemblyOperationType::InstallPart:
	case EDocAssemblyOperationType::ReplacePart:
		T.Slot = Op.TargetSlotId;
		if (const FDocPartInstance* Installed = AssemblyState.InstalledParts.Find(Op.TargetSlotId))
		{
			T.Parts.Add(Installed->PartInstanceId);
		}
		if (Op.PartInstanceId.IsValid())
		{
			T.Parts.AddUnique(Op.PartInstanceId);
		}
		if (Op.ReplacementPartInstanceId.IsValid())
		{
			T.Parts.AddUnique(Op.ReplacementPartInstanceId);
		}
		break;
	default:
		break;
	}
	return T;
}

FDocSystemResult UDocAssemblyComponent::CheckLockout(const FDocPartSlotDefinition& Slot) const
{
	if (!Slot.bRequiresLockout)
	{
		return FDocSystemResult::MakeSuccess();
	}
	IDocAssemblyResourceProvider* Provider = GetProvider();
	bool bPowered = false;
	bool bLockedOut = false;
	if (!Provider || !Provider->QueryMachineState(bPowered, bLockedOut))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Lockout required but machine state is unknown (no provider)"));
	}
	if (!bLockedOut)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Machine is not locked out"));
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocAssemblyComponent::ValidateOperation(const FDocAssemblyOperation& Op, bool bRevalidate, FName& OutExternalDefinition) const
{
	auto Fail = [](EDocResultOutcome Outcome, const TCHAR* Why) { return FDocSystemResult::MakeFailure(Outcome, Why); };
	const UDocAssemblyDefinition* Def = AssemblyDefinition;
	IDocAssemblyResourceProvider* Provider = GetProvider();

	// Resolves a part the operation wants to put into TargetSlot (local detached or external).
	auto ResolveIncoming = [&](const FGuid& PartId, FName& OutDefinition, bool& bOutExternal) -> FDocSystemResult
	{
		bOutExternal = false;
		if (!PartId.IsValid())
		{
			return Fail(EDocResultOutcome::InvalidInput, TEXT("Part id required"));
		}
		if (const FDocPartInstance* Detached = AssemblyState.DetachedParts.Find(PartId))
		{
			OutDefinition = Detached->PartDefinitionId;
			return FDocSystemResult::MakeSuccess();
		}
		if (IsPartInstalled(PartId))
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Part is already installed"));
		}
		if (AssemblyState.QuarantinedParts.ContainsByPredicate([&](const FDocPartInstance& Q) { return Q.PartInstanceId == PartId; }))
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Part is quarantined"));
		}
		bOutExternal = true;
		if (bRevalidate)
		{
			const FName* Known = ExternalDefinitions.Find(Op.OperationId);
			if (!Known)
			{
				return Fail(EDocResultOutcome::Conflict, TEXT("External reservation lost"));
			}
			OutDefinition = *Known;
			return FDocSystemResult::MakeSuccess();
		}
		if (!Provider)
		{
			return Fail(EDocResultOutcome::NotFound, TEXT("Part is not in this assembly and no provider can supply it"));
		}
		return FDocSystemResult::MakeSuccess(); // reservation happens in RequestOperation after all local checks
	};

	switch (Op.OperationType)
	{
	case EDocAssemblyOperationType::Inspect:
		return FDocSystemResult::MakeSuccess();

	case EDocAssemblyOperationType::Test:
		return Fail(EDocResultOutcome::InvalidInput, TEXT("Use RunDiagnostic for tests"));

	case EDocAssemblyOperationType::ReleaseFastener:
	case EDocAssemblyOperationType::SecureFastener:
	{
		const FDocFastenerRecord* Record = AssemblyState.Fasteners.Find(Op.TargetFastenerId);
		const FDocFastenerDefinition* FDef = Def->FindFastener(Op.TargetFastenerId);
		if (!Record || !FDef)
		{
			return Fail(EDocResultOutcome::NotFound, TEXT("Fastener not found"));
		}
		if (Record->State == EDocFastenerState::Damaged)
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Fastener is damaged"));
		}
		const bool bRelease = Op.OperationType == EDocAssemblyOperationType::ReleaseFastener;
		if ((bRelease && Record->State == EDocFastenerState::Released) || (!bRelease && Record->State == EDocFastenerState::Secured))
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Fastener is already in that state"));
		}
		if (FDef->RequiredToolTag.IsValid())
		{
			if (!Provider)
			{
				return Fail(EDocResultOutcome::Unavailable, TEXT("A tool is required but no capability provider is set"));
			}
			if (!Provider->HasToolCapability(Op.OperatorId, FDef->RequiredToolTag))
			{
				return Fail(EDocResultOutcome::PermissionDenied, TEXT("Operator lacks the required tool"));
			}
		}
		return FDocSystemResult::MakeSuccess();
	}

	case EDocAssemblyOperationType::RemovePart:
	case EDocAssemblyOperationType::ReplacePart:
	{
		const FDocPartSlotDefinition* Slot = Def->FindSlot(Op.TargetSlotId);
		const FDocPartInstance* Installed = AssemblyState.InstalledParts.Find(Op.TargetSlotId);
		if (!Slot)
		{
			return Fail(EDocResultOutcome::NotFound, TEXT("Slot does not exist"));
		}
		if (!Installed)
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("No part installed in slot"));
		}
		if (Op.OperationType == EDocAssemblyOperationType::RemovePart && Op.PartInstanceId.IsValid() && Op.PartInstanceId != Installed->PartInstanceId)
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("A different part is installed in that slot"));
		}
		if (!IsSlotReachable(*Slot))
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Slot is obstructed"));
		}
		for (const FDocPartSlotDefinition& Child : Def->Slots)
		{
			if (Child.ParentSlotId == Op.TargetSlotId && AssemblyState.InstalledParts.Contains(Child.SlotId) && Op.OperationType == EDocAssemblyOperationType::RemovePart)
			{
				return Fail(EDocResultOutcome::Conflict, TEXT("Child parts are still attached"));
			}
		}
		if (!AreFastenersReleased(Op.TargetSlotId))
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Required fasteners are not released"));
		}
		const FDocSystemResult Lockout = CheckLockout(*Slot);
		if (!Lockout.IsSuccess())
		{
			return Lockout;
		}
		if (Op.OperationType == EDocAssemblyOperationType::ReplacePart)
		{
			if (Op.ReplacementPartInstanceId == Installed->PartInstanceId)
			{
				return Fail(EDocResultOutcome::InvalidInput, TEXT("Replacement is the installed part"));
			}
			bool bExternal = false;
			const FDocSystemResult Incoming = ResolveIncoming(Op.ReplacementPartInstanceId, OutExternalDefinition, bExternal);
			if (!Incoming.IsSuccess())
			{
				return Incoming;
			}
			if (!bExternal && !Def->IsCompatible(Op.TargetSlotId, OutExternalDefinition))
			{
				return Fail(EDocResultOutcome::InvalidInput, TEXT("Replacement part is incompatible with the slot"));
			}
		}
		return FDocSystemResult::MakeSuccess();
	}

	case EDocAssemblyOperationType::InstallPart:
	{
		const FDocPartSlotDefinition* Slot = Def->FindSlot(Op.TargetSlotId);
		if (!Slot)
		{
			return Fail(EDocResultOutcome::NotFound, TEXT("Slot does not exist"));
		}
		if (AssemblyState.InstalledParts.Contains(Op.TargetSlotId))
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Slot already occupied"));
		}
		if (!IsSlotReachable(*Slot))
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Slot is obstructed or its parent is missing"));
		}
		if (!AreFastenersReleased(Op.TargetSlotId))
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Slot fasteners must be released to seat a part"));
		}
		const FDocSystemResult Lockout = CheckLockout(*Slot);
		if (!Lockout.IsSuccess())
		{
			return Lockout;
		}
		bool bExternal = false;
		const FDocSystemResult Incoming = ResolveIncoming(Op.PartInstanceId, OutExternalDefinition, bExternal);
		if (!Incoming.IsSuccess())
		{
			return Incoming;
		}
		if (!bExternal && !Def->IsCompatible(Op.TargetSlotId, OutExternalDefinition))
		{
			return Fail(EDocResultOutcome::InvalidInput, TEXT("Part is incompatible with the slot"));
		}
		return FDocSystemResult::MakeSuccess();
	}

	case EDocAssemblyOperationType::CompleteProcedure:
	{
		for (const TPair<FName, FDocFastenerRecord>& Kvp : AssemblyState.Fasteners)
		{
			if (Kvp.Value.State != EDocFastenerState::Secured)
			{
				return Fail(EDocResultOutcome::Conflict, TEXT("All fasteners must be secured"));
			}
		}
		const bool bHasFunctionalTest = Def->Diagnostics.ContainsByPredicate([](const FDocDiagnosticTestDefinition& D) { return D.bIsFunctionalTest; });
		if (bHasFunctionalTest && AssemblyState.LastFunctionalPassRevision != AssemblyState.Revision)
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("The functional test must pass on the current assembly first"));
		}
		if (!InDoubtOperations.IsEmpty())
		{
			return Fail(EDocResultOutcome::Conflict, TEXT("Resolve in-doubt transfers first"));
		}
		return FDocSystemResult::MakeSuccess();
	}
	default:
		return Fail(EDocResultOutcome::InvalidInput, TEXT("Unknown operation"));
	}
}

// ---------------------------------------------------------------------------------------------
// Transaction
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocAssemblyComponent::RequestOperation(const FDocAssemblyOperation& InOperation, FGuid& OutOperationId)
{
	OutOperationId.Invalidate();
	if (!AssemblyDefinition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Assembly not initialized"));
	}
	const FGuid* SessionOperator = Sessions.Find(InOperation.SessionId);
	if (!SessionOperator || *SessionOperator != InOperation.OperatorId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Operation must come from the operator's active session"));
	}
	if (InOperation.ExpectedRevision != 0 && InOperation.ExpectedRevision != AssemblyState.Revision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Assembly changed since the caller's view (revision mismatch)"));
	}

	FDocAssemblyOperation Op = InOperation;
	Op.OperationId = FGuid::NewGuid();
	Op.TransactionState = EDocAssemblyTransactionState::Staged;
	Op.bCommitted = false;
	Op.bCancelled = false;
	Op.bExternalPart = false;

	const FTargets Targets = GetTargets(Op);
	if (IsClaimedAgainst(Targets, Op.OperatorId, FGuid()))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Target is being manipulated by another operation or operator"));
	}

	FName IncomingDefinition = NAME_None;
	const FDocSystemResult Valid = ValidateOperation(Op, /*bRevalidate*/ false, IncomingDefinition);
	if (!Valid.IsSuccess())
	{
		return Valid;
	}

	// External part: reserve with the provider and check compatibility with what it reports.
	const FGuid IncomingId = Op.OperationType == EDocAssemblyOperationType::ReplacePart ? Op.ReplacementPartInstanceId : Op.PartInstanceId;
	const bool bNeedsIncoming = Op.OperationType == EDocAssemblyOperationType::InstallPart || Op.OperationType == EDocAssemblyOperationType::ReplacePart;
	if (bNeedsIncoming && !AssemblyState.DetachedParts.Contains(IncomingId))
	{
		IDocAssemblyResourceProvider* Provider = GetProvider();
		FName ExternalDef = NAME_None;
		if (!Provider || !Provider->ReserveExternalPart(Op.OperatorId, IncomingId, ExternalDef))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("External part could not be reserved"));
		}
		if (!AssemblyDefinition->IsCompatible(Op.TargetSlotId, ExternalDef))
		{
			Provider->ReleaseExternalPart(Op.OperatorId, IncomingId);
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("External part is incompatible with the slot"));
		}
		Op.bExternalPart = true;
		ExternalDefinitions.Add(Op.OperationId, ExternalDef);
	}

	FDocAssemblyClaim Claim;
	Claim.SessionId = Op.SessionId;
	Claim.OperatorId = Op.OperatorId;
	Claim.OperationId = Op.OperationId;
	Claim.TargetSlotId = Targets.Slot;
	Claim.TargetFastenerId = Targets.Fastener;
	ActiveClaims.Add(Claim);
	for (const FGuid& PartId : Targets.Parts)
	{
		FDocAssemblyClaim PartClaim = Claim;
		PartClaim.TargetSlotId = NAME_None;
		PartClaim.TargetFastenerId = NAME_None;
		PartClaim.PartInstanceId = PartId;
		ActiveClaims.Add(PartClaim);
	}
	StagedOperations.Add(Op.OperationId, Op);
	OutOperationId = Op.OperationId;
	return FDocSystemResult::MakeSuccess();
}

void UDocAssemblyComponent::AddReceipt(const FDocAssemblyOperation& Op, EDocAssemblyTransactionState State, const FString& Note, FName ExternalDefinition)
{
	for (FDocAssemblyReceipt& Existing : AssemblyState.Receipts)
	{
		if (Existing.OperationId == Op.OperationId)
		{
			Existing.State = State;
			Existing.Note = Note;
			Existing.AssemblyRevision = AssemblyState.Revision;
			return;
		}
	}
	FDocAssemblyReceipt Receipt;
	Receipt.OperationId = Op.OperationId;
	Receipt.OperationType = Op.OperationType;
	Receipt.State = State;
	Receipt.TargetSlotId = Op.TargetSlotId;
	Receipt.PartInstanceId = Op.OperationType == EDocAssemblyOperationType::ReplacePart ? Op.ReplacementPartInstanceId : Op.PartInstanceId;
	Receipt.PartDefinitionId = ExternalDefinition;
	Receipt.bExternalPart = Op.bExternalPart;
	Receipt.OperatorId = Op.OperatorId;
	Receipt.AssemblyRevision = AssemblyState.Revision;
	Receipt.Note = Note;
	AssemblyState.Receipts.Add(Receipt);
	if (AssemblyState.Receipts.Num() > MaxReceipts)
	{
		// Never drop an unresolved in-doubt receipt.
		const int32 Index = AssemblyState.Receipts.IndexOfByPredicate([](const FDocAssemblyReceipt& R) { return R.State != EDocAssemblyTransactionState::InDoubt; });
		if (Index != INDEX_NONE)
		{
			AssemblyState.Receipts.RemoveAt(Index);
		}
	}
}

void UDocAssemblyComponent::AbortStaged(const FGuid& OperationId, EDocAssemblyTransactionState FinalState, const FString& Note)
{
	FDocAssemblyOperation Op;
	if (!StagedOperations.RemoveAndCopyValue(OperationId, Op))
	{
		return;
	}
	if (Op.bExternalPart)
	{
		if (IDocAssemblyResourceProvider* Provider = GetProvider())
		{
			Provider->ReleaseExternalPart(Op.OperatorId, Op.OperationType == EDocAssemblyOperationType::ReplacePart ? Op.ReplacementPartInstanceId : Op.PartInstanceId);
		}
	}
	ExternalDefinitions.Remove(OperationId);
	ReleaseOperationClaims(OperationId);
	Op.TransactionState = FinalState;
	Op.bCancelled = FinalState == EDocAssemblyTransactionState::RolledBack;
	AddReceipt(Op, FinalState, Note);
}

void UDocAssemblyComponent::TrySpawnPresentation(const FGuid& PartInstanceId)
{
	int32& Attempts = PresentationAttempts.FindOrAdd(PartInstanceId);
	++Attempts;
	if (PresentationFailuresRemaining > 0)
	{
		--PresentationFailuresRemaining;
		PendingPresentationParts.Add(PartInstanceId);
		return;
	}
	PendingPresentationParts.Remove(PartInstanceId);
	VisualSpawnCounts.FindOrAdd(PartInstanceId) = 1; // one proxy per logical part, never more
	PresentationAttempts.Remove(PartInstanceId);
}

FDocSystemResult UDocAssemblyComponent::RetryPresentation(const FGuid& PartInstanceId)
{
	if (!PendingPresentationParts.Contains(PartInstanceId))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Presentation not pending"));
	}
	if (!IsPartInstalled(PartInstanceId))
	{
		PendingPresentationParts.Remove(PartInstanceId);
		return FDocSystemResult::MakeNoChange(TEXT("Part no longer installed"));
	}
	const int32* Attempts = PresentationAttempts.Find(PartInstanceId);
	if (Attempts && *Attempts >= MaxPresentationAttempts)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Presentation retries exhausted; logical state is unaffected"));
	}
	TrySpawnPresentation(PartInstanceId);
	return PendingPresentationParts.Contains(PartInstanceId)
		? FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Presentation still pending"))
		: FDocSystemResult::MakeSuccess();
}

void UDocAssemblyComponent::ApplyOperation(const FDocAssemblyOperation& Op, FName ExternalDefinition)
{
	auto Detach = [this](FName SlotId)
	{
		FDocPartInstance Part;
		if (AssemblyState.InstalledParts.RemoveAndCopyValue(SlotId, Part))
		{
			Part.LocationKind = EDocPartLocationKind::Detached;
			Part.InstalledSlotId = NAME_None;
			++Part.Revision;
			PendingPresentationParts.Remove(Part.PartInstanceId);
			VisualSpawnCounts.Remove(Part.PartInstanceId);
			AssemblyState.DetachedParts.Add(Part.PartInstanceId, Part);
		}
	};
	auto Install = [this, &Op, ExternalDefinition](const FGuid& PartId)
	{
		FDocPartInstance Part;
		if (!AssemblyState.DetachedParts.RemoveAndCopyValue(PartId, Part))
		{
			// External part transferred in by the provider.
			Part.PartInstanceId = PartId;
			Part.PartDefinitionId = ExternalDefinition;
			if (const UDocAssemblyPartDefinition* Def = AssemblyDefinition->FindPartDefinition(ExternalDefinition))
			{
				Part.DefinitionVersion = Def->Version;
				Part.Condition = Def->DefaultCondition;
			}
		}
		Part.LocationKind = EDocPartLocationKind::Installed;
		Part.InstalledSlotId = Op.TargetSlotId;
		++Part.Revision;
		AssemblyState.InstalledParts.Add(Op.TargetSlotId, Part);
		TrySpawnPresentation(PartId);
	};

	switch (Op.OperationType)
	{
	case EDocAssemblyOperationType::ReleaseFastener:
	case EDocAssemblyOperationType::SecureFastener:
		if (FDocFastenerRecord* Record = AssemblyState.Fasteners.Find(Op.TargetFastenerId))
		{
			const bool bRelease = Op.OperationType == EDocAssemblyOperationType::ReleaseFastener;
			Record->State = bRelease ? EDocFastenerState::Released : EDocFastenerState::Secured;
			Record->Tightness = bRelease ? 0.0f : 1.0f;
		}
		break;
	case EDocAssemblyOperationType::RemovePart:
		Detach(Op.TargetSlotId);
		break;
	case EDocAssemblyOperationType::InstallPart:
		Install(Op.PartInstanceId);
		break;
	case EDocAssemblyOperationType::ReplacePart:
		Detach(Op.TargetSlotId);
		Install(Op.ReplacementPartInstanceId);
		break;
	case EDocAssemblyOperationType::CompleteProcedure:
		AssemblyState.bProcedureComplete = true;
		return; // completing does not change structure
	default:
		return;
	}
	++AssemblyState.Revision;
	AssemblyState.bProcedureComplete = false;
}

FDocSystemResult UDocAssemblyComponent::CommitOperation(const FGuid& OperationId)
{
	FDocAssemblyOperation* Staged = StagedOperations.Find(OperationId);
	if (!Staged)
	{
		if (InDoubtOperations.Contains(OperationId))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Transfer is in doubt; reconcile first"));
		}
		const FDocAssemblyReceipt* Receipt = FindReceipt(OperationId);
		return Receipt
			? FDocSystemResult::MakeNoChange(FString::Printf(TEXT("Operation already finished (state %d)"), static_cast<int32>(Receipt->State)))
			: FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown operation"));
	}
	const FDocAssemblyOperation Op = *Staged;
	const FName* ExternalPtr = ExternalDefinitions.Find(OperationId);
	FName ExternalDefinition = ExternalPtr ? *ExternalPtr : NAME_None;

	FName Revalidated = NAME_None;
	const FDocSystemResult Valid = ValidateOperation(Op, /*bRevalidate*/ true, Revalidated);
	if (!Valid.IsSuccess())
	{
		AbortStaged(OperationId, EDocAssemblyTransactionState::Failed, Valid.Diagnostic);
		OnOperationCommitted.Broadcast(Op, false);
		return Valid;
	}

	if (Op.bExternalPart)
	{
		IDocAssemblyResourceProvider* Provider = GetProvider();
		const FGuid IncomingId = Op.OperationType == EDocAssemblyOperationType::ReplacePart ? Op.ReplacementPartInstanceId : Op.PartInstanceId;
		const EDocAssemblyTransferOutcome Outcome = Provider ? Provider->CommitExternalPart(Op.OperatorId, IncomingId) : EDocAssemblyTransferOutcome::Failed;
		if (Outcome == EDocAssemblyTransferOutcome::Failed)
		{
			AbortStaged(OperationId, EDocAssemblyTransactionState::Failed, TEXT("Provider refused the transfer"));
			OnOperationCommitted.Broadcast(Op, false);
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("External transfer failed; assembly unchanged"));
		}
		if (Outcome == EDocAssemblyTransferOutcome::InDoubt)
		{
			// Nothing is applied locally; claims stay so nobody else uses the slot or part.
			FDocAssemblyOperation Doubt = Op;
			Doubt.TransactionState = EDocAssemblyTransactionState::InDoubt;
			StagedOperations.Remove(OperationId);
			InDoubtOperations.Add(OperationId, Doubt);
			AddReceipt(Doubt, EDocAssemblyTransactionState::InDoubt, TEXT("Provider could not confirm the transfer"), ExternalDefinition);
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("InDoubt: transfer unconfirmed; call ReconcileTransfers"));
		}
	}

	ApplyOperation(Op, ExternalDefinition);
	StagedOperations.Remove(OperationId);
	ExternalDefinitions.Remove(OperationId);
	ReleaseOperationClaims(OperationId);
	FDocAssemblyOperation Committed = Op;
	Committed.TransactionState = EDocAssemblyTransactionState::Committed;
	Committed.bCommitted = true;
	AddReceipt(Committed, EDocAssemblyTransactionState::Committed, FString(), ExternalDefinition);
	OnOperationCommitted.Broadcast(Committed, true);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocAssemblyComponent::CancelOperation(const FGuid& OperationId)
{
	if (StagedOperations.Contains(OperationId))
	{
		AbortStaged(OperationId, EDocAssemblyTransactionState::RolledBack, TEXT("Cancelled before commit"));
		return FDocSystemResult::MakeSuccess();
	}
	if (InDoubtOperations.Contains(OperationId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Transfer is in doubt; it cannot be cancelled, only reconciled"));
	}
	if (const FDocAssemblyReceipt* Receipt = FindReceipt(OperationId))
	{
		return FDocSystemResult::MakeNoChange(Receipt->State == EDocAssemblyTransactionState::Committed
			? TEXT("Already committed; the committed outcome stands (only owned presentation stops)")
			: TEXT("Already finished"));
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown operation"));
}

FDocSystemResult UDocAssemblyComponent::ExecuteOperationImmediate(const FDocAssemblyOperation& InOperation)
{
	FGuid OperationId;
	const FDocSystemResult Requested = RequestOperation(InOperation, OperationId);
	if (!Requested.IsSuccess())
	{
		return Requested;
	}
	return CommitOperation(OperationId);
}

FDocSystemResult UDocAssemblyComponent::ReconcileTransfers()
{
	if (InDoubtOperations.IsEmpty())
	{
		return FDocSystemResult::MakeNoChange(TEXT("Nothing in doubt"));
	}
	IDocAssemblyResourceProvider* Provider = GetProvider();
	if (!Provider)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No provider to reconcile with"));
	}
	TArray<FGuid> Ids;
	InDoubtOperations.GetKeys(Ids);
	int32 Resolved = 0;
	for (const FGuid& Id : Ids)
	{
		const FDocAssemblyOperation Op = InDoubtOperations[Id];
		const FGuid IncomingId = Op.OperationType == EDocAssemblyOperationType::ReplacePart ? Op.ReplacementPartInstanceId : Op.PartInstanceId;
		const EDocAssemblyTransferOutcome Outcome = Provider->QueryExternalTransfer(Op.OperatorId, IncomingId);
		if (Outcome == EDocAssemblyTransferOutcome::InDoubt)
		{
			continue;
		}
		const FName* ExternalPtr = ExternalDefinitions.Find(Id);
		const FName ExternalDefinition = ExternalPtr ? *ExternalPtr : NAME_None;
		InDoubtOperations.Remove(Id);
		ReleaseOperationClaims(Id);
		if (Outcome == EDocAssemblyTransferOutcome::Committed)
		{
			// The provider gave the part up: it must now exist exactly once, here.
			if (Op.OperationType == EDocAssemblyOperationType::ReplacePart)
			{
				// Re-check the slot still holds what we expect; otherwise keep the part detached rather than lose it.
				ApplyOperation(Op, ExternalDefinition);
			}
			else if (!AssemblyState.InstalledParts.Contains(Op.TargetSlotId))
			{
				ApplyOperation(Op, ExternalDefinition);
			}
			else
			{
				FDocPartInstance Part;
				Part.PartInstanceId = IncomingId;
				Part.PartDefinitionId = ExternalDefinition;
				Part.LocationKind = EDocPartLocationKind::Detached;
				AssemblyState.DetachedParts.Add(IncomingId, Part);
				++AssemblyState.Revision;
			}
			AddReceipt(Op, EDocAssemblyTransactionState::Committed, TEXT("Reconciled: committed"), ExternalDefinition);
		}
		else
		{
			AddReceipt(Op, EDocAssemblyTransactionState::Failed, TEXT("Reconciled: not transferred"), ExternalDefinition);
		}
		ExternalDefinitions.Remove(Id);
		++Resolved;
	}
	if (Resolved == 0)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Still in doubt"));
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocAssemblyComponent::RunDiagnostic(FName TestId, FDocDiagnosticReport& OutReport)
{
	OutReport = FDocDiagnosticReport();
	OutReport.TestId = TestId;
	OutReport.Timestamp = FPlatformTime::Seconds();
	OutReport.AssemblyRevision = AssemblyState.Revision;
	const FDocDiagnosticTestDefinition* Test = AssemblyDefinition ? AssemblyDefinition->FindDiagnostic(TestId) : nullptr;
	if (!Test)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown diagnostic"));
	}
	if (Test->bRequiresPower)
	{
		IDocAssemblyResourceProvider* Provider = GetProvider();
		bool bPowered = false;
		bool bLockedOut = false;
		if (!Provider || !Provider->QueryMachineState(bPowered, bLockedOut))
		{
			// Never assume the machine is powered.
			OutReport.Outcome = EDocDiagnosticOutcome::Inconclusive;
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Test needs power state but no provider reports it"));
		}
		if (!bPowered)
		{
			OutReport.Outcome = EDocDiagnosticOutcome::BlockedByPower;
			OutReport.bIsConclusive = false;
			AssemblyState.DiagnosticHistory.Add(OutReport);
			OnDiagnosticCompleted.Broadcast(OutReport);
			return FDocSystemResult::MakeSuccess();
		}
	}
	bool bFault = false;
	for (FName SlotId : Test->EvaluatedSlotIds)
	{
		const FDocPartInstance* Installed = AssemblyState.InstalledParts.Find(SlotId);
		if (!Installed)
		{
			bFault = true;
			OutReport.ObservedFaults.Add(FName(*FString::Printf(TEXT("MissingPart.%s"), *SlotId.ToString())));
			continue;
		}
		for (FName Fault : Installed->HiddenFaults)
		{
			if (Test->DetectableFaultTags.Contains(Fault))
			{
				bFault = true;
				OutReport.ObservedFaults.AddUnique(Fault);
			}
		}
	}
	OutReport.Outcome = bFault ? EDocDiagnosticOutcome::Fail : EDocDiagnosticOutcome::Pass;
	OutReport.bIsConclusive = true;
	if (!bFault && Test->bIsFunctionalTest)
	{
		AssemblyState.LastFunctionalPassRevision = AssemblyState.Revision;
	}
	AssemblyState.DiagnosticHistory.Add(OutReport);
	if (AssemblyState.DiagnosticHistory.Num() > MaxHistory)
	{
		AssemblyState.DiagnosticHistory.RemoveAt(0);
	}
	OnDiagnosticCompleted.Broadcast(OutReport);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocAssemblyComponent::QueryAvailableOperations(FGuid SessionId, TArray<FDocAssemblyOperation>& OutOperations) const
{
	OutOperations.Reset();
	const FGuid* Operator = Sessions.Find(SessionId);
	if (!AssemblyDefinition || !Operator)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Unknown session"));
	}
	auto Offer = [&](FDocAssemblyOperation Op)
	{
		Op.SessionId = SessionId;
		Op.OperatorId = *Operator;
		FName Unused;
		if (!IsClaimedAgainst(GetTargets(Op), *Operator, FGuid()) && ValidateOperation(Op, false, Unused).IsSuccess())
		{
			OutOperations.Add(Op);
		}
	};
	for (const TPair<FName, FDocFastenerRecord>& Kvp : AssemblyState.Fasteners)
	{
		FDocAssemblyOperation Op;
		Op.TargetFastenerId = Kvp.Key;
		Op.OperationType = Kvp.Value.State == EDocFastenerState::Secured ? EDocAssemblyOperationType::ReleaseFastener : EDocAssemblyOperationType::SecureFastener;
		Offer(Op);
	}
	for (const FDocPartSlotDefinition& Slot : AssemblyDefinition->Slots)
	{
		if (const FDocPartInstance* Installed = AssemblyState.InstalledParts.Find(Slot.SlotId))
		{
			FDocAssemblyOperation Op;
			Op.OperationType = EDocAssemblyOperationType::RemovePart;
			Op.TargetSlotId = Slot.SlotId;
			Op.PartInstanceId = Installed->PartInstanceId;
			Offer(Op);
			continue;
		}
		for (const TPair<FGuid, FDocPartInstance>& Detached : AssemblyState.DetachedParts)
		{
			FDocAssemblyOperation Op;
			Op.OperationType = EDocAssemblyOperationType::InstallPart;
			Op.TargetSlotId = Slot.SlotId;
			Op.PartInstanceId = Detached.Key;
			Offer(Op);
		}
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------------------------
// Restore
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocAssemblyComponent::StageRestore(const FDocAssemblyState& InState)
{
	if (!AssemblyDefinition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Initialize the assembly before restoring"));
	}
	if (InState.AssemblyId != AssemblyDefinition->AssemblyId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Saved state belongs to another assembly"));
	}

	// Transient work from before the load is dropped (reservations released, nothing applied).
	TArray<FGuid> Staged;
	StagedOperations.GetKeys(Staged);
	for (const FGuid& Id : Staged)
	{
		AbortStaged(Id, EDocAssemblyTransactionState::RolledBack, TEXT("Restore"));
	}
	ActiveClaims.Reset();
	Sessions.Reset();
	InDoubtOperations.Reset();
	ExternalDefinitions.Reset();
	PendingPresentationParts.Reset();
	VisualSpawnCounts.Reset();
	PresentationAttempts.Reset();

	FDocAssemblyState Out;
	Out.AssemblyId = InState.AssemblyId;
	Out.Revision = InState.Revision;
	Out.DiagnosticHistory = InState.DiagnosticHistory;
	Out.Receipts = InState.Receipts;
	Out.LastFunctionalPassRevision = InState.LastFunctionalPassRevision;
	Out.bProcedureComplete = InState.bProcedureComplete;

	int32 Migrated = 0;
	int32 Quarantined = 0;
	int32 DuplicatesMerged = 0;
	TSet<FGuid> Seen;
	auto Quarantine = [&](FDocPartInstance Part, const TCHAR* Reason)
	{
		Part.LocationKind = EDocPartLocationKind::Quarantined;
		Part.InstalledSlotId = NAME_None;
		Part.QuarantineReason = Reason;
		Out.QuarantinedParts.Add(Part);
		++Quarantined;
	};
	auto Detach = [&](FDocPartInstance Part)
	{
		Part.LocationKind = EDocPartLocationKind::Detached;
		Part.InstalledSlotId = NAME_None;
		Out.DetachedParts.Add(Part.PartInstanceId, Part);
		Seen.Add(Part.PartInstanceId);
		++Migrated;
	};

	for (const TPair<FName, FDocPartInstance>& Kvp : InState.InstalledParts)
	{
		const FDocPartInstance& Part = Kvp.Value;
		if (!Part.PartInstanceId.IsValid())
		{
			FDocPartInstance Orphan = Part;
			Orphan.PartInstanceId = FGuid::NewGuid();
			Quarantine(Orphan, TEXT("InvalidRecord"));
			continue;
		}
		if (Seen.Contains(Part.PartInstanceId))
		{
			++DuplicatesMerged; // same part recorded twice: keep its first location only
			continue;
		}
		if (!AssemblyDefinition->FindPartDefinition(Part.PartDefinitionId))
		{
			Seen.Add(Part.PartInstanceId);
			Quarantine(Part, TEXT("MissingContent"));
			continue;
		}
		if (!AssemblyDefinition->FindSlot(Kvp.Key) || Part.InstalledSlotId != Kvp.Key || !AssemblyDefinition->IsCompatible(Kvp.Key, Part.PartDefinitionId))
		{
			Detach(Part); // slot removed/changed: keep the part, detached
			continue;
		}
		FDocPartInstance Installed = Part;
		Installed.LocationKind = EDocPartLocationKind::Installed;
		Out.InstalledParts.Add(Kvp.Key, Installed);
		Seen.Add(Part.PartInstanceId);
	}
	for (const TPair<FGuid, FDocPartInstance>& Kvp : InState.DetachedParts)
	{
		FDocPartInstance Part = Kvp.Value;
		Part.PartInstanceId = Kvp.Key;
		if (!Part.PartInstanceId.IsValid())
		{
			Part.PartInstanceId = FGuid::NewGuid();
			Quarantine(Part, TEXT("InvalidRecord"));
			continue;
		}
		if (Seen.Contains(Part.PartInstanceId))
		{
			++DuplicatesMerged;
			continue;
		}
		Seen.Add(Part.PartInstanceId);
		if (!AssemblyDefinition->FindPartDefinition(Part.PartDefinitionId))
		{
			Quarantine(Part, TEXT("MissingContent"));
			continue;
		}
		Part.LocationKind = EDocPartLocationKind::Detached;
		Part.InstalledSlotId = NAME_None;
		Out.DetachedParts.Add(Part.PartInstanceId, Part);
	}
	// Previously quarantined parts are kept (never dropped), unless the same part now has a real location.
	for (const FDocPartInstance& Q : InState.QuarantinedParts)
	{
		if (Q.PartInstanceId.IsValid() && !Seen.Contains(Q.PartInstanceId))
		{
			Seen.Add(Q.PartInstanceId);
			Out.QuarantinedParts.Add(Q);
		}
	}
	for (const FDocFastenerDefinition& FDef : AssemblyDefinition->Fasteners)
	{
		const FDocFastenerRecord* Saved = InState.Fasteners.Find(FDef.FastenerId);
		FDocFastenerRecord Record = Saved ? *Saved : FDocFastenerRecord();
		Record.FastenerId = FDef.FastenerId;
		Migrated += Saved ? 0 : 1;
		Out.Fasteners.Add(FDef.FastenerId, Record);
	}
	Migrated += InState.Fasteners.Num() > AssemblyDefinition->Fasteners.Num() ? 1 : 0;
	if (Migrated > 0 || Quarantined > 0 || DuplicatesMerged > 0)
	{
		++Out.Revision;
		Out.bProcedureComplete = false;
	}
	AssemblyState = MoveTemp(Out);

	// Re-arm in-doubt external transfers from their durable receipts.
	for (const FDocAssemblyReceipt& Receipt : AssemblyState.Receipts)
	{
		if (Receipt.State != EDocAssemblyTransactionState::InDoubt)
		{
			continue;
		}
		FDocAssemblyOperation Op;
		Op.OperationId = Receipt.OperationId;
		Op.OperationType = Receipt.OperationType;
		Op.TargetSlotId = Receipt.TargetSlotId;
		Op.OperatorId = Receipt.OperatorId;
		Op.bExternalPart = true;
		Op.TransactionState = EDocAssemblyTransactionState::InDoubt;
		if (Op.OperationType == EDocAssemblyOperationType::ReplacePart)
		{
			Op.ReplacementPartInstanceId = Receipt.PartInstanceId;
		}
		else
		{
			Op.PartInstanceId = Receipt.PartInstanceId;
		}
		InDoubtOperations.Add(Op.OperationId, Op);
		ExternalDefinitions.Add(Op.OperationId, Receipt.PartDefinitionId);
		FDocAssemblyClaim Claim;
		Claim.OperationId = Op.OperationId;
		Claim.OperatorId = Op.OperatorId;
		Claim.TargetSlotId = Op.TargetSlotId;
		ActiveClaims.Add(Claim);
	}

	// Logical identity first, then proxies.
	for (const TPair<FName, FDocPartInstance>& Kvp : AssemblyState.InstalledParts)
	{
		TrySpawnPresentation(Kvp.Value.PartInstanceId);
	}

	FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	Result.Diagnostic = FString::Printf(TEXT("Migrated %d, quarantined %d, duplicate records merged %d"), Migrated, Quarantined, DuplicatesMerged);
	return Result;
}
