#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocAssemblyTypes.h"
#include "DocAssemblyDefinitions.h"
#include "DocAssemblyComponent.h"
#include "DocAssemblyLocalProvider.h"
#include "DocAssemblySubsystem.h"
#include "GameplayTagsManager.h"
#include "GameFramework/Actor.h"

namespace DocAssemblyTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FGameplayTag Tag(const FName& Name)
	{
		UGameplayTagsManager& Mgr = UGameplayTagsManager::Get();
		const FGameplayTag T = Mgr.RequestGameplayTag(Name, false);
		return T.IsValid() ? T : Mgr.AddNativeGameplayTag(Name);
	}

	FGameplayTag Screwdriver() { return Tag(TEXT("Doc.Test.Tool.Screwdriver")); }

	UDocAssemblyPartDefinition* PartDef(FName Id)
	{
		UDocAssemblyPartDefinition* D = NewObject<UDocAssemblyPartDefinition>(GetTransientPackage());
		D->PartDefinitionId = Id;
		return D;
	}

	/** Generic machine: a cover held by two fasteners, an internal module behind it, a spare module and a wrong part. */
	struct FMachine
	{
		UDocAssemblyDefinition* Def = nullptr;
		UDocAssemblyComponent* Comp = nullptr;
		UDocAssemblyLocalProvider* Provider = nullptr;
		FGuid Cover, Module, Spare, Wrong;
		FGuid OperatorA = FGuid::NewGuid();
		FGuid OperatorB = FGuid::NewGuid();
		FGuid SessionA, SessionB;

		static UDocAssemblyDefinition* MakeDefinition(FGuid& Cover, FGuid& Module, FGuid& Spare, FGuid& Wrong)
		{
			UDocAssemblyDefinition* D = NewObject<UDocAssemblyDefinition>(GetTransientPackage());
			D->AssemblyId = TEXT("Machine.Generic");
			D->PartDefinitions = { PartDef(TEXT("CoverDef")), PartDef(TEXT("ModuleDef")), PartDef(TEXT("WrongDef")) };

			FDocFastenerDefinition F1; F1.FastenerId = TEXT("F1"); F1.RequiredToolTag = Screwdriver();
			FDocFastenerDefinition F2; F2.FastenerId = TEXT("F2"); F2.RequiredToolTag = Screwdriver();
			D->Fasteners = { F1, F2 };

			FDocPartSlotDefinition CoverSlot;
			CoverSlot.SlotId = TEXT("Cover");
			CoverSlot.AllowedPartDefinitionIds = { TEXT("CoverDef") };
			CoverSlot.RequiredFastenerIds = { TEXT("F1"), TEXT("F2") };
			FDocPartSlotDefinition ModuleSlot;
			ModuleSlot.SlotId = TEXT("Module");
			ModuleSlot.AllowedPartDefinitionIds = { TEXT("ModuleDef") };
			ModuleSlot.ObstructingSlotIds = { TEXT("Cover") };
			ModuleSlot.bRequiresLockout = true;
			D->Slots = { CoverSlot, ModuleSlot };

			FDocDiagnosticTestDefinition SelfTest;
			SelfTest.TestId = TEXT("SelfTest");
			SelfTest.bRequiresPower = true;
			SelfTest.EvaluatedSlotIds = { TEXT("Module") };
			SelfTest.DetectableFaultTags = { TEXT("Fault.Burnt") };
			SelfTest.bIsFunctionalTest = true;
			D->Diagnostics = { SelfTest };

			Cover = FGuid::NewGuid(); Module = FGuid::NewGuid(); Spare = FGuid::NewGuid(); Wrong = FGuid::NewGuid();
			FDocPartInstance C; C.PartInstanceId = Cover; C.PartDefinitionId = TEXT("CoverDef"); C.InstalledSlotId = TEXT("Cover");
			FDocPartInstance M; M.PartInstanceId = Module; M.PartDefinitionId = TEXT("ModuleDef"); M.InstalledSlotId = TEXT("Module");
			M.HiddenFaults = { TEXT("Fault.Burnt"), TEXT("Fault.Secret") };
			FDocPartInstance S; S.PartInstanceId = Spare; S.PartDefinitionId = TEXT("ModuleDef");
			FDocPartInstance W; W.PartInstanceId = Wrong; W.PartDefinitionId = TEXT("WrongDef");
			D->InitialParts = { C, M, S, W };
			return D;
		}

		explicit FMachine(bool bWithProvider = true)
		{
			Def = MakeDefinition(Cover, Module, Spare, Wrong);
			Comp = NewObject<UDocAssemblyComponent>(GetTransientPackage());
			Comp->InitializeAssembly(Def);
			Provider = NewObject<UDocAssemblyLocalProvider>(GetTransientPackage());
			Provider->GrantedTools.AddTag(Screwdriver());
			if (bWithProvider)
			{
				Comp->SetResourceProvider(Provider);
			}
			Comp->BeginMaintenanceSession(OperatorA, SessionA);
			Comp->BeginMaintenanceSession(OperatorB, SessionB);
		}

		FDocAssemblyOperation Op(EDocAssemblyOperationType Type, FName Slot = NAME_None, FName Fastener = NAME_None, FGuid Part = FGuid(), FGuid Replacement = FGuid(), bool bB = false) const
		{
			FDocAssemblyOperation O;
			O.SessionId = bB ? SessionB : SessionA;
			O.OperatorId = bB ? OperatorB : OperatorA;
			O.OperationType = Type;
			O.TargetSlotId = Slot;
			O.TargetFastenerId = Fastener;
			O.PartInstanceId = Part;
			O.ReplacementPartInstanceId = Replacement;
			return O;
		}

		FDocSystemResult Do(const FDocAssemblyOperation& O) { return Comp->ExecuteOperationImmediate(O); }
		FDocSystemResult Release(FName F) { return Do(Op(EDocAssemblyOperationType::ReleaseFastener, NAME_None, F)); }
		FDocSystemResult Secure(FName F) { return Do(Op(EDocAssemblyOperationType::SecureFastener, NAME_None, F)); }
		FDocSystemResult Remove(FName Slot) { return Do(Op(EDocAssemblyOperationType::RemovePart, Slot)); }
		FDocSystemResult Install(FName Slot, FGuid Part) { return Do(Op(EDocAssemblyOperationType::InstallPart, Slot, NAME_None, Part)); }

		/** Release both fasteners and take the cover off. */
		bool OpenUp() { return Release(TEXT("F1")).IsSuccess() && Release(TEXT("F2")).IsSuccess() && Remove(TEXT("Cover")).IsSuccess(); }
	};
}

// ASM-01
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyPrerequisiteOrderTest, FAutomationTestBase, "Doc.Assembly.PrerequisiteOrder", DocAssemblyTests::Flags)
bool FDocAssemblyPrerequisiteOrderTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	TArray<FDocAssemblyOperation> Available;
	TestTrue(TEXT("Query"), M.Comp->QueryAvailableOperations(M.SessionA, Available).IsSuccess());
	TestFalse(TEXT("Cover removal not offered while fastened"), Available.ContainsByPredicate([](const FDocAssemblyOperation& O) { return O.OperationType == EDocAssemblyOperationType::RemovePart; }));
	TestEqual(TEXT("Cover needs its fasteners released"), M.Remove(TEXT("Cover")).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Module is behind the cover"), M.Remove(TEXT("Module")).Outcome, EDocResultOutcome::Conflict);

	FMachine NoTools(/*bWithProvider*/ false);
	TestEqual(TEXT("Tool required but no provider: explicit failure"), NoTools.Release(TEXT("F1")).Outcome, EDocResultOutcome::Unavailable);
	M.Provider->GrantedTools.Reset();
	TestEqual(TEXT("Operator without the tool is refused"), M.Release(TEXT("F1")).Outcome, EDocResultOutcome::PermissionDenied);
	M.Provider->GrantedTools.AddTag(Screwdriver());

	TestTrue(TEXT("Release F1"), M.Release(TEXT("F1")).IsSuccess());
	TestEqual(TEXT("Still one fastener left"), M.Remove(TEXT("Cover")).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Releasing twice is refused"), M.Release(TEXT("F1")).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Release F2"), M.Release(TEXT("F2")).IsSuccess());
	TestTrue(TEXT("Cover comes off"), M.Remove(TEXT("Cover")).IsSuccess());

	M.Provider->bLockedOut = false;
	TestEqual(TEXT("Module needs a lockout"), M.Remove(TEXT("Module")).Outcome, EDocResultOutcome::PermissionDenied);
	M.Provider->bMachineStateKnown = false;
	TestEqual(TEXT("Unknown machine state is not assumed safe"), M.Remove(TEXT("Module")).Outcome, EDocResultOutcome::Unavailable);
	M.Provider->bMachineStateKnown = true;
	M.Provider->bLockedOut = true;
	TestTrue(TEXT("Module removed under lockout"), M.Remove(TEXT("Module")).IsSuccess());

	TestTrue(TEXT("Cover back on"), M.Install(TEXT("Cover"), M.Cover).IsSuccess());
	TestEqual(TEXT("Module slot now blocked again"), M.Install(TEXT("Module"), M.Spare).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Secure F1"), M.Secure(TEXT("F1")).IsSuccess());
	TestTrue(TEXT("Secure F2"), M.Secure(TEXT("F2")).IsSuccess());
	return true;
}

// ASM-02
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyPartUniquenessTest, FAutomationTestBase, "Doc.Assembly.PartUniqueness", DocAssemblyTests::Flags)
bool FDocAssemblyPartUniquenessTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	auto AllOnce = [&]()
	{
		return M.Comp->CountPartLocations(M.Cover) == 1 && M.Comp->CountPartLocations(M.Module) == 1
			&& M.Comp->CountPartLocations(M.Spare) == 1 && M.Comp->CountPartLocations(M.Wrong) == 1;
	};
	TestTrue(TEXT("Initial layout"), AllOnce());
	TestTrue(TEXT("Open"), M.OpenUp());
	TestTrue(TEXT("After cover removal"), AllOnce());
	TestEqual(TEXT("Cover is detached, not installed"), M.Comp->FindPart(M.Cover)->LocationKind, EDocPartLocationKind::Detached);
	TestTrue(TEXT("Replace module with spare"), M.Do(M.Op(EDocAssemblyOperationType::ReplacePart, TEXT("Module"), NAME_None, FGuid(), M.Spare)).IsSuccess());
	TestTrue(TEXT("After replacement"), AllOnce());
	TestTrue(TEXT("Spare installed"), M.Comp->IsPartInstalled(M.Spare));
	TestFalse(TEXT("Old module detached"), M.Comp->IsPartInstalled(M.Module));
	TestEqual(TEXT("An installed part cannot be installed again"), M.Install(TEXT("Cover"), M.Spare).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Occupied slot"), M.Install(TEXT("Module"), M.Module).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Remove spare"), M.Remove(TEXT("Module")).IsSuccess());
	TestTrue(TEXT("Reinstall original"), M.Install(TEXT("Module"), M.Module).IsSuccess());
	TestTrue(TEXT("Still exactly one location each"), AllOnce());
	return true;
}

// ASM-03
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblySlotCompatibilityTest, FAutomationTestBase, "Doc.Assembly.SlotCompatibility", DocAssemblyTests::Flags)
bool FDocAssemblySlotCompatibilityTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	TestTrue(TEXT("Open"), M.OpenUp());
	TestTrue(TEXT("Remove module"), M.Remove(TEXT("Module")).IsSuccess());
	const FDocAssemblyState Before = M.Comp->CaptureAssemblyState();
	TestEqual(TEXT("Wrong part refused"), M.Install(TEXT("Module"), M.Wrong).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Unknown slot"), M.Install(TEXT("Nowhere"), M.Spare).Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Unknown part with nothing to supply it"), M.Install(TEXT("Module"), FGuid::NewGuid()).Outcome, EDocResultOutcome::NotFound);
	const FDocAssemblyState After = M.Comp->CaptureAssemblyState();
	TestEqual(TEXT("No partial mutation (revision)"), After.Revision, Before.Revision);
	TestEqual(TEXT("No partial mutation (detached parts)"), After.DetachedParts.Num(), Before.DetachedParts.Num());
	TestFalse(TEXT("Slot still empty"), After.InstalledParts.Contains(TEXT("Module")));

	TestTrue(TEXT("Install spare"), M.Install(TEXT("Module"), M.Spare).IsSuccess());
	TestEqual(TEXT("Incompatible replacement refused"), M.Do(M.Op(EDocAssemblyOperationType::ReplacePart, TEXT("Module"), NAME_None, FGuid(), M.Wrong)).Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Original still installed"), M.Comp->IsPartInstalled(M.Spare));

	// Definition validation.
	FGuid A, B, C, D;
	UDocAssemblyDefinition* Big = FMachine::MakeDefinition(A, B, C, D);
	Big->Slots[0].Capacity = 2;
	TestEqual(TEXT("Capacity > 1 unsupported"), Big->ValidateDefinition().Outcome, EDocResultOutcome::Unsupported);
	UDocAssemblyDefinition* Cyclic = FMachine::MakeDefinition(A, B, C, D);
	Cyclic->Slots[0].ParentSlotId = TEXT("Module");
	Cyclic->Slots[1].ParentSlotId = TEXT("Cover");
	TestEqual(TEXT("Cyclic parents rejected"), Cyclic->ValidateDefinition().Outcome, EDocResultOutcome::InvalidConfiguration);
	UDocAssemblyDefinition* BadRef = FMachine::MakeDefinition(A, B, C, D);
	BadRef->Slots[0].RequiredFastenerIds.Add(TEXT("F9"));
	TestEqual(TEXT("Unknown fastener reference rejected"), BadRef->ValidateDefinition().Outcome, EDocResultOutcome::InvalidConfiguration);
	const int64 Revision = M.Comp->GetRevision();
	TestFalse(TEXT("Invalid definition refused"), M.Comp->InitializeAssembly(BadRef).IsSuccess());
	TestEqual(TEXT("Existing machine untouched"), M.Comp->GetRevision(), Revision);
	return true;
}

// ASM-04
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyConcurrentManipulationTest, FAutomationTestBase, "Doc.Assembly.ConcurrentManipulation", DocAssemblyTests::Flags)
bool FDocAssemblyConcurrentManipulationTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	TestTrue(TEXT("Release F1"), M.Release(TEXT("F1")).IsSuccess());
	TestTrue(TEXT("Release F2"), M.Release(TEXT("F2")).IsSuccess());

	FGuid OpA;
	TestTrue(TEXT("A stages cover removal"), M.Comp->RequestOperation(M.Op(EDocAssemblyOperationType::RemovePart, TEXT("Cover")), OpA).IsSuccess());
	FGuid OpB;
	TestEqual(TEXT("B cannot remove the same part"), M.Comp->RequestOperation(M.Op(EDocAssemblyOperationType::RemovePart, TEXT("Cover"), NAME_None, FGuid(), FGuid(), true), OpB).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("B may work on an unrelated target"), M.Comp->RequestOperation(M.Op(EDocAssemblyOperationType::SecureFastener, NAME_None, TEXT("F1"), FGuid(), FGuid(), true), OpB).Outcome, EDocResultOutcome::Succeeded);
	TestTrue(TEXT("B backs out"), M.Comp->CancelOperation(OpB).IsSuccess());

	TestTrue(TEXT("A takes a lease on F2"), M.Comp->AcquireClaim(M.SessionA, NAME_None, TEXT("F2")).IsSuccess());
	TestEqual(TEXT("B cannot touch A's leased fastener"), M.Do(M.Op(EDocAssemblyOperationType::SecureFastener, NAME_None, TEXT("F2"), FGuid(), FGuid(), true)).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("B cannot lease it either"), M.Comp->AcquireClaim(M.SessionB, NAME_None, TEXT("F2")).Outcome, EDocResultOutcome::Conflict);

	const int64 SeenRevision = M.Comp->GetRevision();
	TestTrue(TEXT("A commits"), M.Comp->CommitOperation(OpA).IsSuccess());
	FDocAssemblyOperation Stale = M.Op(EDocAssemblyOperationType::InstallPart, TEXT("Cover"), NAME_None, M.Cover, FGuid(), true);
	Stale.ExpectedRevision = SeenRevision;
	TestEqual(TEXT("B's stale view is a conflict, not last-writer-wins"), M.Do(Stale).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Removing an already-removed part"), M.Do(M.Op(EDocAssemblyOperationType::RemovePart, TEXT("Cover"), NAME_None, FGuid(), FGuid(), true)).Outcome, EDocResultOutcome::Conflict);

	FDocAssemblyOperation Impostor = M.Op(EDocAssemblyOperationType::InstallPart, TEXT("Cover"), NAME_None, M.Cover);
	Impostor.OperatorId = M.OperatorB; // A's session, B's identity
	TestEqual(TEXT("Cannot act through another operator's session"), M.Do(Impostor).Outcome, EDocResultOutcome::PermissionDenied);
	TestEqual(TEXT("Exactly one cover"), M.Comp->CountPartLocations(M.Cover), 1);
	return true;
}

// ASM-05
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyCancelCommitBoundaryTest, FAutomationTestBase, "Doc.Assembly.CancelCommitBoundary", DocAssemblyTests::Flags)
bool FDocAssemblyCancelCommitBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	TestTrue(TEXT("Release F1"), M.Release(TEXT("F1")).IsSuccess());
	TestTrue(TEXT("Release F2"), M.Release(TEXT("F2")).IsSuccess());
	const int64 Revision = M.Comp->GetRevision();

	FGuid Staged;
	TestTrue(TEXT("Stage removal"), M.Comp->RequestOperation(M.Op(EDocAssemblyOperationType::RemovePart, TEXT("Cover")), Staged).IsSuccess());
	TestTrue(TEXT("Cancel before commit"), M.Comp->CancelOperation(Staged).IsChanged());
	TestTrue(TEXT("Cover still installed"), M.Comp->IsPartInstalled(M.Cover));
	TestEqual(TEXT("Revision unchanged"), M.Comp->GetRevision(), Revision);
	const FDocAssemblyReceipt* Receipt = M.Comp->FindReceipt(Staged);
	TestTrue(TEXT("Rolled-back receipt"), Receipt && Receipt->State == EDocAssemblyTransactionState::RolledBack);
	FGuid Other;
	TestTrue(TEXT("Claims released: B can stage it now"), M.Comp->RequestOperation(M.Op(EDocAssemblyOperationType::RemovePart, TEXT("Cover"), NAME_None, FGuid(), FGuid(), true), Other).IsSuccess());
	TestTrue(TEXT("B commits"), M.Comp->CommitOperation(Other).IsSuccess());
	TestEqual(TEXT("Cancel after commit reports the committed outcome"), M.Comp->CancelOperation(Other).Outcome, EDocResultOutcome::NoChange);
	TestFalse(TEXT("Cover stays removed"), M.Comp->IsPartInstalled(M.Cover));
	TestEqual(TEXT("Unknown handle"), M.Comp->CancelOperation(FGuid::NewGuid()).Outcome, EDocResultOutcome::NotFound);

	// Cancelling releases an external reservation.
	TestTrue(TEXT("Remove module"), M.Remove(TEXT("Module")).IsSuccess());
	const FGuid External = FGuid::NewGuid();
	M.Provider->Stock.Add(External, TEXT("ModuleDef"));
	FGuid Install;
	TestTrue(TEXT("Stage external install"), M.Comp->RequestOperation(M.Op(EDocAssemblyOperationType::InstallPart, TEXT("Module"), NAME_None, External), Install).IsSuccess());
	TestTrue(TEXT("Reserved"), M.Provider->Reserved.Contains(External));
	TestTrue(TEXT("Cancel"), M.Comp->CancelOperation(Install).IsSuccess());
	TestFalse(TEXT("Reservation released on cancel"), M.Provider->Reserved.Contains(External));

	TestTrue(TEXT("Stage again"), M.Comp->RequestOperation(M.Op(EDocAssemblyOperationType::InstallPart, TEXT("Module"), NAME_None, External), Install).IsSuccess());
	TestTrue(TEXT("End session"), M.Comp->EndMaintenanceSession(M.SessionA).IsSuccess());
	TestFalse(TEXT("Ending the session releases it too"), M.Provider->Reserved.Contains(External));
	TestEqual(TEXT("Nothing installed"), M.Comp->CountPartLocations(External), 0);
	return true;
}

// ASM-06
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyResourceReconciliationTest, FAutomationTestBase, "Doc.Assembly.ResourceReconciliation", DocAssemblyTests::Flags)
bool FDocAssemblyResourceReconciliationTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	TestTrue(TEXT("Open"), M.OpenUp());
	TestTrue(TEXT("Remove module"), M.Remove(TEXT("Module")).IsSuccess());
	const FGuid X = FGuid::NewGuid();
	M.Provider->Stock.Add(X, TEXT("ModuleDef"));

	M.Provider->NextCommitOutcome = EDocAssemblyTransferOutcome::Failed;
	TestEqual(TEXT("Provider failure"), M.Install(TEXT("Module"), X).Outcome, EDocResultOutcome::Failed);
	TestEqual(TEXT("Not created here"), M.Comp->CountPartLocations(X), 0);
	TestTrue(TEXT("Not lost: provider still owns it"), M.Provider->Stock.Contains(X));
	TestFalse(TEXT("Slot empty"), M.Comp->CaptureAssemblyState().InstalledParts.Contains(TEXT("Module")));

	M.Provider->NextCommitOutcome = EDocAssemblyTransferOutcome::InDoubt;
	FGuid Doubtful;
	TestTrue(TEXT("Stage"), M.Comp->RequestOperation(M.Op(EDocAssemblyOperationType::InstallPart, TEXT("Module"), NAME_None, X), Doubtful).IsSuccess());
	const FDocSystemResult InDoubt = M.Comp->CommitOperation(Doubtful);
	TestEqual(TEXT("In doubt is reported, not claimed as success"), InDoubt.Outcome, EDocResultOutcome::Unavailable);
	TestEqual(TEXT("Nothing applied while in doubt"), M.Comp->CountPartLocations(X), 0);
	TestTrue(TEXT("Still reserved at the provider"), M.Provider->Reserved.Contains(X));
	TestEqual(TEXT("Slot held while in doubt"), M.Install(TEXT("Module"), M.Spare).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("In-doubt work cannot be cancelled"), M.Comp->CancelOperation(Doubtful).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Still unresolved"), M.Comp->ReconcileTransfers().Outcome, EDocResultOutcome::NoChange);

	// The in-doubt intent is durable: restore into a fresh machine and reconcile there.
	const FDocAssemblyState Saved = M.Comp->CaptureAssemblyState();
	UDocAssemblyComponent* Reloaded = NewObject<UDocAssemblyComponent>(GetTransientPackage());
	TestTrue(TEXT("Init"), Reloaded->InitializeAssembly(M.Def).IsSuccess());
	Reloaded->SetResourceProvider(M.Provider);
	TestTrue(TEXT("Restore"), Reloaded->StageRestore(Saved).IsSuccess());
	TestEqual(TEXT("Restored as in doubt"), Reloaded->QueryProcedureProgress().InDoubtTransfers, 1);
	M.Provider->InDoubtResolution = EDocAssemblyTransferOutcome::Committed;
	TestTrue(TEXT("Reconcile"), Reloaded->ReconcileTransfers().IsChanged());
	TestEqual(TEXT("Transferred part now exists exactly once"), Reloaded->CountPartLocations(X), 1);
	TestTrue(TEXT("Installed"), Reloaded->IsPartInstalled(X));
	TestFalse(TEXT("Provider no longer owns it"), M.Provider->Stock.Contains(X));
	const FDocAssemblyReceipt* Receipt = Reloaded->FindReceipt(Doubtful);
	TestTrue(TEXT("Receipt resolved"), Receipt && Receipt->State == EDocAssemblyTransactionState::Committed);
	return true;
}

// ASM-07
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyVisualFailureTest, FAutomationTestBase, "Doc.Assembly.VisualFailure", DocAssemblyTests::Flags)
bool FDocAssemblyVisualFailureTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	TestTrue(TEXT("Open"), M.OpenUp());
	TestTrue(TEXT("Remove module"), M.Remove(TEXT("Module")).IsSuccess());
	M.Comp->SetSimulatedPresentationFailures(1);
	TestTrue(TEXT("Logical install commits despite the visual failure"), M.Install(TEXT("Module"), M.Spare).IsSuccess());
	TestTrue(TEXT("Presentation pending"), M.Comp->IsPresentationPending(M.Spare));
	TestEqual(TEXT("No proxy yet"), M.Comp->GetVisualMeshSpawnCount(M.Spare), 0);
	TestEqual(TEXT("One logical location"), M.Comp->CountPartLocations(M.Spare), 1);
	TestTrue(TEXT("Retry succeeds"), M.Comp->RetryPresentation(M.Spare).IsChanged());
	TestEqual(TEXT("Exactly one proxy"), M.Comp->GetVisualMeshSpawnCount(M.Spare), 1);
	TestEqual(TEXT("Extra retries do nothing"), M.Comp->RetryPresentation(M.Spare).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Still one proxy"), M.Comp->GetVisualMeshSpawnCount(M.Spare), 1);

	TestTrue(TEXT("Remove spare"), M.Remove(TEXT("Module")).IsSuccess());
	M.Comp->SetSimulatedPresentationFailures(10);
	TestTrue(TEXT("Install original"), M.Install(TEXT("Module"), M.Module).IsSuccess());
	M.Comp->RetryPresentation(M.Module);
	M.Comp->RetryPresentation(M.Module);
	TestEqual(TEXT("Retries are bounded"), M.Comp->RetryPresentation(M.Module).Outcome, EDocResultOutcome::Failed);
	TestTrue(TEXT("Logical state unaffected"), M.Comp->IsPartInstalled(M.Module));
	TestEqual(TEXT("Never duplicated"), M.Comp->CountPartLocations(M.Module), 1);
	return true;
}

// ASM-08
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyDiagnosticTruthTest, FAutomationTestBase, "Doc.Assembly.DiagnosticTruth", DocAssemblyTests::Flags)
bool FDocAssemblyDiagnosticTruthTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	FDocAssemblyState View;
	M.Comp->QueryAssembly(View);
	TestTrue(TEXT("Player view hides faults"), View.InstalledParts.Contains(TEXT("Module")) && View.InstalledParts[TEXT("Module")].HiddenFaults.IsEmpty());
	TestEqual(TEXT("Authoritative state keeps them"), M.Comp->CaptureAssemblyState().InstalledParts[TEXT("Module")].HiddenFaults.Num(), 2);

	FDocDiagnosticReport Report;
	FMachine NoPower(/*bWithProvider*/ false);
	TestEqual(TEXT("Power never assumed"), NoPower.Comp->RunDiagnostic(TEXT("SelfTest"), Report).Outcome, EDocResultOutcome::Unavailable);
	M.Provider->bPowered = false;
	TestTrue(TEXT("Run unpowered"), M.Comp->RunDiagnostic(TEXT("SelfTest"), Report).IsSuccess());
	TestEqual(TEXT("Blocked by power"), Report.Outcome, EDocDiagnosticOutcome::BlockedByPower);
	M.Provider->bPowered = true;
	TestTrue(TEXT("Run powered"), M.Comp->RunDiagnostic(TEXT("SelfTest"), Report).IsSuccess());
	TestEqual(TEXT("Fault observed"), Report.Outcome, EDocDiagnosticOutcome::Fail);
	TestTrue(TEXT("Detectable fault reported"), Report.ObservedFaults.Contains(TEXT("Fault.Burnt")));
	TestFalse(TEXT("Undetectable fault stays hidden"), Report.ObservedFaults.Contains(TEXT("Fault.Secret")));

	TestTrue(TEXT("Open"), M.OpenUp());
	TestTrue(TEXT("Replace faulty module"), M.Do(M.Op(EDocAssemblyOperationType::ReplacePart, TEXT("Module"), NAME_None, FGuid(), M.Spare)).IsSuccess());
	TestTrue(TEXT("Cover on"), M.Install(TEXT("Cover"), M.Cover).IsSuccess());
	TestTrue(TEXT("Secure F1"), M.Secure(TEXT("F1")).IsSuccess());
	TestTrue(TEXT("Secure F2"), M.Secure(TEXT("F2")).IsSuccess());
	TestEqual(TEXT("Replacing a part does not prove the machine works"), M.Do(M.Op(EDocAssemblyOperationType::CompleteProcedure)).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Post-repair test"), M.Comp->RunDiagnostic(TEXT("SelfTest"), Report).IsSuccess());
	TestEqual(TEXT("Passes"), Report.Outcome, EDocDiagnosticOutcome::Pass);
	TestTrue(TEXT("Complete"), M.Do(M.Op(EDocAssemblyOperationType::CompleteProcedure)).IsSuccess());
	TestTrue(TEXT("Procedure complete"), M.Comp->QueryProcedureProgress().bProcedureComplete);
	return true;
}

// ASM-09
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyRestoreMigrationTest, FAutomationTestBase, "Doc.Assembly.RestoreMigration", DocAssemblyTests::Flags)
bool FDocAssemblyRestoreMigrationTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FMachine M;
	TestTrue(TEXT("Open"), M.OpenUp());
	FDocAssemblyState Saved = M.Comp->CaptureAssemblyState();

	// Simulate content/definition changes between save and load.
	FDocPartInstance InOldSlot;
	InOldSlot.PartInstanceId = FGuid::NewGuid();
	InOldSlot.PartDefinitionId = TEXT("ModuleDef");
	InOldSlot.InstalledSlotId = TEXT("OldSlot");
	InOldSlot.LocationKind = EDocPartLocationKind::Installed;
	Saved.InstalledParts.Add(TEXT("OldSlot"), InOldSlot);
	FDocPartInstance Ghost;
	Ghost.PartInstanceId = FGuid::NewGuid();
	Ghost.PartDefinitionId = TEXT("GhostDef");
	Saved.DetachedParts.Add(Ghost.PartInstanceId, Ghost);
	FDocPartInstance DupOfModule = Saved.InstalledParts[TEXT("Module")];
	DupOfModule.InstalledSlotId = NAME_None;
	Saved.DetachedParts.Add(DupOfModule.PartInstanceId, DupOfModule);
	FDocPartInstance OldQuarantine;
	OldQuarantine.PartInstanceId = FGuid::NewGuid();
	OldQuarantine.PartDefinitionId = TEXT("Legacy");
	OldQuarantine.LocationKind = EDocPartLocationKind::Quarantined;
	Saved.QuarantinedParts.Add(OldQuarantine);
	Saved.Fasteners.Remove(TEXT("F2"));
	FDocFastenerRecord Unknown;
	Unknown.FastenerId = TEXT("F9");
	Saved.Fasteners.Add(TEXT("F9"), Unknown);

	UDocAssemblyComponent* Loaded = NewObject<UDocAssemblyComponent>(GetTransientPackage());
	TestTrue(TEXT("Init"), Loaded->InitializeAssembly(M.Def).IsSuccess());
	const FDocSystemResult Restored = Loaded->StageRestore(Saved);
	TestTrue(TEXT("Restore"), Restored.IsSuccess());
	TestTrue(TEXT("Migration reported"), Restored.Diagnostic.Contains(TEXT("Migrated")));

	TestEqual(TEXT("Part from a removed slot is kept, detached"), Loaded->FindPart(InOldSlot.PartInstanceId) ? Loaded->FindPart(InOldSlot.PartInstanceId)->LocationKind : EDocPartLocationKind::InTransit, EDocPartLocationKind::Detached);
	const FDocPartInstance* GhostNow = Loaded->FindPart(Ghost.PartInstanceId);
	TestTrue(TEXT("Missing content quarantined, not deleted"), GhostNow && GhostNow->LocationKind == EDocPartLocationKind::Quarantined && GhostNow->QuarantineReason == TEXT("MissingContent"));
	TestEqual(TEXT("Duplicate record merged into one location"), Loaded->CountPartLocations(M.Module), 1);
	TestTrue(TEXT("Module stays installed"), Loaded->IsPartInstalled(M.Module));
	TestEqual(TEXT("Earlier quarantine kept"), Loaded->CountPartLocations(OldQuarantine.PartInstanceId), 1);
	TestTrue(TEXT("Missing fastener record defaults to secured"), Loaded->FindFastenerRecord(TEXT("F2")) && Loaded->FindFastenerRecord(TEXT("F2"))->State == EDocFastenerState::Secured);
	TestTrue(TEXT("Unknown fastener dropped"), Loaded->FindFastenerRecord(TEXT("F9")) == nullptr);
	TestTrue(TEXT("Revision advanced for the migration"), Loaded->GetRevision() > Saved.Revision);
	Loaded->SetResourceProvider(M.Provider);
	FGuid Session;
	Loaded->BeginMaintenanceSession(M.OperatorA, Session);
	FDocAssemblyOperation ReleaseF2;
	ReleaseF2.SessionId = Session;
	ReleaseF2.OperatorId = M.OperatorA;
	ReleaseF2.OperationType = EDocAssemblyOperationType::ReleaseFastener;
	ReleaseF2.TargetFastenerId = TEXT("F2");
	TestTrue(TEXT("Release the defaulted fastener"), Loaded->ExecuteOperationImmediate(ReleaseF2).IsSuccess());
	FDocAssemblyOperation InstallGhost = ReleaseF2;
	InstallGhost.OperationType = EDocAssemblyOperationType::InstallPart;
	InstallGhost.TargetFastenerId = NAME_None;
	InstallGhost.TargetSlotId = TEXT("Cover");
	InstallGhost.PartInstanceId = Ghost.PartInstanceId;
	const FDocSystemResult GhostResult = Loaded->ExecuteOperationImmediate(InstallGhost);
	TestEqual(TEXT("Quarantined parts cannot be installed"), GhostResult.Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Refused because it is quarantined"), GhostResult.Diagnostic.Contains(TEXT("quarantined")));

	FDocAssemblyState Foreign = Saved;
	Foreign.AssemblyId = TEXT("Machine.Other");
	TestEqual(TEXT("Foreign state refused"), Loaded->StageRestore(Foreign).Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

// ASM-10
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocAssemblyIsolatedMachineTest, FAutomationTestBase, "Doc.Assembly.IsolatedMachine", DocAssemblyTests::Flags)
bool FDocAssemblyIsolatedMachineTest::RunTest(const FString& Parameters)
{
	using namespace DocAssemblyTests;
	FDocScopedTestWorld TW;
	FGuid Cover, Module, Spare, Wrong;
	UDocAssemblyDefinition* Def = FMachine::MakeDefinition(Cover, Module, Spare, Wrong);
	AActor* Actor = TW.Spawn<AActor>();
	UDocAssemblyComponent* Comp = NewObject<UDocAssemblyComponent>(Actor);
	Comp->AssemblyDefinition = Def;
	Actor->AddInstanceComponent(Comp);
	Comp->RegisterComponent();
	UDocAssemblySubsystem* Subsystem = TW.GetSubsystem<UDocAssemblySubsystem>();
	TestTrue(TEXT("Registered with the world"), Subsystem && Subsystem->FindAssembly(TEXT("Machine.Generic")) == Comp);

	UDocAssemblyLocalProvider* Local = NewObject<UDocAssemblyLocalProvider>(Comp);
	Local->GrantedTools.AddTag(Screwdriver());
	TestTrue(TEXT("Local provider"), Comp->SetResourceProvider(Local).IsSuccess());
	const FGuid Operator = FGuid::NewGuid();
	FGuid Session;
	TestTrue(TEXT("Session"), Comp->BeginMaintenanceSession(Operator, Session).IsSuccess());
	auto Run = [&](EDocAssemblyOperationType Type, FName Slot, FName Fastener, FGuid Part, FGuid Replacement)
	{
		FDocAssemblyOperation Op;
		Op.SessionId = Session;
		Op.OperatorId = Operator;
		Op.OperationType = Type;
		Op.TargetSlotId = Slot;
		Op.TargetFastenerId = Fastener;
		Op.PartInstanceId = Part;
		Op.ReplacementPartInstanceId = Replacement;
		Op.ExpectedRevision = Comp->GetRevision();
		return Comp->ExecuteOperationImmediate(Op).IsSuccess();
	};

	FDocDiagnosticReport Report;
	TestTrue(TEXT("Diagnose"), Comp->RunDiagnostic(TEXT("SelfTest"), Report).IsSuccess());
	TestEqual(TEXT("Faulty"), Report.Outcome, EDocDiagnosticOutcome::Fail);
	TestTrue(TEXT("Release F1"), Run(EDocAssemblyOperationType::ReleaseFastener, NAME_None, TEXT("F1"), FGuid(), FGuid()));
	TestTrue(TEXT("Release F2"), Run(EDocAssemblyOperationType::ReleaseFastener, NAME_None, TEXT("F2"), FGuid(), FGuid()));
	TestTrue(TEXT("Remove cover"), Run(EDocAssemblyOperationType::RemovePart, TEXT("Cover"), NAME_None, FGuid(), FGuid()));
	TestTrue(TEXT("Replace module"), Run(EDocAssemblyOperationType::ReplacePart, TEXT("Module"), NAME_None, FGuid(), Spare));
	TestTrue(TEXT("Reinstall cover"), Run(EDocAssemblyOperationType::InstallPart, TEXT("Cover"), NAME_None, Cover, FGuid()));
	TestTrue(TEXT("Secure F1"), Run(EDocAssemblyOperationType::SecureFastener, NAME_None, TEXT("F1"), FGuid(), FGuid()));
	TestTrue(TEXT("Secure F2"), Run(EDocAssemblyOperationType::SecureFastener, NAME_None, TEXT("F2"), FGuid(), FGuid()));
	TestTrue(TEXT("Functional test"), Comp->RunDiagnostic(TEXT("SelfTest"), Report).IsSuccess() && Report.Outcome == EDocDiagnosticOutcome::Pass);
	TestTrue(TEXT("Complete"), Run(EDocAssemblyOperationType::CompleteProcedure, NAME_None, NAME_None, FGuid(), FGuid()));
	TestTrue(TEXT("Done without Inventory, Interaction or physics"), Comp->QueryProcedureProgress().bProcedureComplete);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
