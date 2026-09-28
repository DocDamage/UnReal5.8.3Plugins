// DocWorldActivation automation tests (ACT-01..05, ACT-08 logic). Relevance sources
// are fixed locations; the world does not tick, the subsystem is advanced explicitly.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocWorldActivationSubsystem.h"
#include "Tests/DocActivationTestTypes.h"
#include "UObject/Package.h"

namespace DocActivationTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FDocActivationBand MakeBand(EDocActivationTier Tier, float Promote, float Demote)
	{
		FDocActivationBand Band;
		Band.Tier = Tier;
		Band.PromoteWithinCm = Promote;
		Band.DemoteBeyondCm = Demote;
		return Band;
	}

	UDocActivationPolicy* MakeDistancePolicy(float Dwell = 0.f)
	{
		UDocActivationPolicy* Policy = NewObject<UDocActivationPolicy>(GetTransientPackage());
		Policy->Kind = EDocActivationPolicyKind::DistanceBased;
		Policy->Bands = { MakeBand(EDocActivationTier::Active, 1000.f, 1200.f), MakeBand(EDocActivationTier::Lightweight, 5000.f, 5500.f) };
		Policy->FarTier = EDocActivationTier::Dormant;
		Policy->MinDwellSeconds = Dwell;
		return Policy;
	}

	UDocActivationProfile* MakeProfile()
	{
		UDocActivationProfile* Profile = NewObject<UDocActivationProfile>(GetTransientPackage());
		FDocActivationTierSettings Light;
		Light.Tier = EDocActivationTier::Lightweight;
		Light.TickInterval = 0.5f;
		FDocActivationTierSettings Dormant;
		Dormant.Tier = EDocActivationTier::Dormant;
		Dormant.bDisableTick = true;
		Dormant.bHidden = true;
		Profile->Tiers = { Light, Dormant };
		return Profile;
	}

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocWorldActivationSubsystem* S = nullptr;

		FFixture()
		{
			S = TW.GetSubsystem<UDocWorldActivationSubsystem>();
			if (S) { S->SetSourceLocationsOverride({ FVector::ZeroVector }); }
		}

		ADocActivationTestActor* Spawn(const FVector& Location, UDocActivationPolicy* Policy, UDocActivationProfile* Profile = nullptr)
		{
			ADocActivationTestActor* A = TW.Spawn<ADocActivationTestActor>(Location);
			A->Activation->Policy = Policy;
			A->Activation->Profile = Profile;
			A->Activation->Controlled.bVisibility = true;
			if (!A->HasActorBegunPlay())
			{
				A->DispatchBeginPlay();
			}
			return A;
		}

		void Sources(const TArray<FVector>& L) { S->SetSourceLocationsOverride(L); }
		void Run(float Seconds, float Step = 0.3f) { for (float T = 0.f; T < Seconds; T += Step) { S->AdvanceForTesting(Step); } }
	};
}

using namespace DocActivationTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocActivationDistanceTest, "Doc.Activation.DistanceAndManual", Flags)
bool FDocActivationDistanceTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	ADocActivationTestActor* A = F.Spawn(FVector(500, 0, 0), MakeDistancePolicy(), MakeProfile());
	const float HostInterval = A->GetActorTickInterval();
	F.Run(0.6f);
	TestEqual(TEXT("Near: Active"), A->Activation->GetCurrentTier(), EDocActivationTier::Active);

	F.Sources({ FVector(3500, 0, 0) }); // 3000 cm away
	F.Run(0.6f);
	TestEqual(TEXT("Mid: Lightweight"), A->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);
	TestEqual(TEXT("Reduced tick rate"), A->GetActorTickInterval(), 0.5f);
	TestTrue(TEXT("Lightweight still ticks"), A->IsActorTickEnabled());

	F.Sources({ FVector(20000, 0, 0) });
	F.Run(0.6f);
	TestEqual(TEXT("Far: Dormant"), A->Activation->GetCurrentTier(), EDocActivationTier::Dormant);
	TestFalse(TEXT("Dormant: tick off"), A->IsActorTickEnabled());
	TestTrue(TEXT("Dormant: hidden (declared capability)"), A->IsHidden());
	TestTrue(TEXT("Collision untouched (not declared)"), A->GetActorEnableCollision());

	F.Sources({ FVector::ZeroVector });
	F.Run(0.6f);
	TestEqual(TEXT("Back to Active"), A->Activation->GetCurrentTier(), EDocActivationTier::Active);
	TestTrue(TEXT("Tick restored"), A->IsActorTickEnabled());
	TestEqual(TEXT("Interval restored"), A->GetActorTickInterval(), HostInterval);
	TestFalse(TEXT("Visible again"), A->IsHidden());
	TestTrue(TEXT("Participant notified"), A->Participant->Changes >= 3);

	// Manual policy.
	UDocActivationPolicy* Manual = NewObject<UDocActivationPolicy>(GetTransientPackage());
	Manual->Kind = EDocActivationPolicyKind::Manual;
	ADocActivationTestActor* M = F.Spawn(FVector(100000, 0, 0), Manual, MakeProfile());
	F.Run(0.3f);
	TestEqual(TEXT("Manual default"), M->Activation->GetCurrentTier(), EDocActivationTier::Active);
	TestTrue(TEXT("Set manual"), F.S->SetManualTier(M->Activation, EDocActivationTier::Lightweight).IsSuccess());
	F.Run(0.3f);
	TestEqual(TEXT("Manual applied"), M->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);
	TestEqual(TEXT("Manual on distance policy refused"), F.S->SetManualTier(A->Activation, EDocActivationTier::Full).Outcome, EDocResultOutcome::InvalidConfiguration);

	// Custom policy.
	UDocActivationPolicy* Custom = NewObject<UDocActivationPolicy>(GetTransientPackage());
	Custom->Kind = EDocActivationPolicyKind::Custom;
	Custom->MinDwellSeconds = 0.f;
	ADocActivationTestActor* C = F.Spawn(FVector(0, 0, 0), Custom, MakeProfile());
	F.Run(0.3f);
	TestEqual(TEXT("Custom participant decides"), C->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocActivationHysteresisTest, "Doc.Activation.HysteresisAndDwell", Flags)
bool FDocActivationHysteresisTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	ADocActivationTestActor* A = F.Spawn(FVector(500, 0, 0), MakeDistancePolicy(0.f), MakeProfile());
	F.Run(0.6f);
	TestEqual(TEXT("Active"), A->Activation->GetCurrentTier(), EDocActivationTier::Active);
	F.Sources({ FVector(-600, 0, 0) }); // 1100: inside hysteresis gap
	F.Run(0.6f);
	TestEqual(TEXT("Stays Active inside the gap"), A->Activation->GetCurrentTier(), EDocActivationTier::Active);
	F.Sources({ FVector(-800, 0, 0) }); // 1300
	F.Run(0.6f);
	TestEqual(TEXT("Demoted beyond threshold"), A->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);
	F.Sources({ FVector(-600, 0, 0) }); // 1100 again
	F.Run(0.6f);
	TestEqual(TEXT("Not promoted until within promote distance"), A->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);

	// Oscillating around the boundary produces no flapping.
	const int32 Before = F.S->GetDebugInfo(A->Activation).Transitions;
	for (int32 i = 0; i < 10; ++i)
	{
		F.Sources({ FVector((i % 2) ? -550.f : -650.f, 0, 0) }); // 1050 / 1150
		F.Run(0.3f);
	}
	TestEqual(TEXT("No transitions while oscillating in the gap"), F.S->GetDebugInfo(A->Activation).Transitions, Before);

	// Dwell.
	ADocActivationTestActor* D = F.Spawn(FVector(0, 50000, 0), MakeDistancePolicy(2.f), MakeProfile());
	F.Sources({ FVector(0, 50000, 0) });
	F.Run(0.3f);
	F.Sources({ FVector(0, 53000, 0) }); // 3000 away
	F.Run(0.6f);
	TestEqual(TEXT("Dwell holds the tier"), D->Activation->GetCurrentTier(), EDocActivationTier::Active);
	F.Run(2.1f);
	TestEqual(TEXT("Changes after dwell"), D->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocActivationPinsTest, "Doc.Activation.SourcesAndPins", Flags)
bool FDocActivationPinsTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	ADocActivationTestActor* A = F.Spawn(FVector::ZeroVector, MakeDistancePolicy(0.f), MakeProfile());
	F.Sources({ FVector(100000, 0, 0), FVector(300, 0, 0) });
	F.Run(0.6f);
	TestEqual(TEXT("Any near source keeps it Active"), A->Activation->GetCurrentTier(), EDocActivationTier::Active);
	F.Sources({ FVector(100000, 0, 0) });
	F.Run(0.6f);
	TestEqual(TEXT("All sources far: Dormant"), A->Activation->GetCurrentTier(), EDocActivationTier::Dormant);

	AActor* Owner = F.TW.Spawn<AActor>();
	const FDocRequestHandle Pin = F.S->PinTier(A->Activation, EDocActivationTier::Active, Owner, TEXT("Interaction"));
	TestTrue(TEXT("Pinned"), Pin.IsSet());
	F.Run(0.3f);
	TestEqual(TEXT("Pin raises the tier"), A->Activation->GetCurrentTier(), EDocActivationTier::Active);
	TestEqual(TEXT("Wrong owner cannot release"), F.S->ReleasePin(Pin, A).Outcome, EDocResultOutcome::PermissionDenied);
	Owner->Destroy();
	F.Run(0.6f);
	TestEqual(TEXT("Dead owner's pin released"), A->Activation->GetCurrentTier(), EDocActivationTier::Dormant);

	AActor* Timed = F.TW.Spawn<AActor>();
	F.S->PinTier(A->Activation, EDocActivationTier::Lightweight, Timed, TEXT("Cinematic"), 1.f);
	F.Run(0.3f);
	TestEqual(TEXT("Timed pin applies"), A->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);
	F.Run(1.5f);
	TestEqual(TEXT("Expired pin released"), A->Activation->GetCurrentTier(), EDocActivationTier::Dormant);
	return true;
}

namespace DocActivationTests
{
	class FTestAdapter final : public IDocActivationRepresentationAdapter
	{
	public:
		bool bFailPrepare = false;
		bool bFailCommit = false;
		int32 Commits = 0;
		int32 Rollbacks = 0;
		virtual FName GetAdapterName() const override { return TEXT("TestISM"); }
		virtual bool SupportsTier(const UDocWorldActivationComponent*, EDocActivationTier Tier) const override { return Tier == EDocActivationTier::Representation; }
		virtual FDocSystemResult PrepareTransition(UDocWorldActivationComponent*, EDocActivationTier, EDocActivationTier) override
		{
			return bFailPrepare ? FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("instance creation failed")) : FDocSystemResult::MakeSuccess();
		}
		virtual FDocSystemResult CommitTransition(UDocWorldActivationComponent*, EDocActivationTier, EDocActivationTier) override
		{
			if (bFailCommit) { return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("swap failed")); }
			++Commits;
			return FDocSystemResult::MakeSuccess();
		}
		virtual void RollbackTransition(UDocWorldActivationComponent*, EDocActivationTier, EDocActivationTier) override { ++Rollbacks; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocActivationStateTest, "Doc.Activation.OwnedStateAndAdapters", Flags)
bool FDocActivationStateTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	// Host change while overridden survives the return to Active.
	ADocActivationTestActor* A = F.Spawn(FVector(3000, 0, 0), MakeDistancePolicy(0.f), MakeProfile());
	F.Run(0.6f);
	TestEqual(TEXT("Lightweight"), A->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);
	A->SetActorTickInterval(2.f); // host decision made during the override
	F.Sources({ FVector(2500, 0, 0) });
	F.Run(0.6f);
	TestEqual(TEXT("Active"), A->Activation->GetCurrentTier(), EDocActivationTier::Active);
	TestEqual(TEXT("Host change not overwritten by a stale snapshot"), A->GetActorTickInterval(), 2.f);

	// Representation without an adapter: safe fallback, never deletion.
	UDocActivationPolicy* Manual = NewObject<UDocActivationPolicy>(GetTransientPackage());
	Manual->Kind = EDocActivationPolicyKind::Manual;
	Manual->UnsupportedFallbackTier = EDocActivationTier::Lightweight;
	ADocActivationTestActor* R = F.Spawn(FVector::ZeroVector, Manual, MakeProfile());
	F.S->SetManualTier(R->Activation, EDocActivationTier::Representation);
	F.Run(0.3f);
	TestEqual(TEXT("Unsupported tier falls back"), R->Activation->GetCurrentTier(), EDocActivationTier::Lightweight);
	TestTrue(TEXT("Reported"), F.S->GetDebugInfo(R->Activation).LastDiagnostic.Contains(TEXT("unsupported")));
	TestTrue(TEXT("Actor still exists"), IsValid(R) && !R->IsActorBeingDestroyed());

	// Adapter whose prepare fails: prior representation kept.
	TSharedPtr<FTestAdapter> Adapter = MakeShared<FTestAdapter>();
	Adapter->bFailPrepare = true;
	F.S->RegisterRepresentationAdapter(Adapter);
	F.S->SetManualTier(R->Activation, EDocActivationTier::Active);
	F.Run(0.3f);
	F.S->SetManualTier(R->Activation, EDocActivationTier::Representation);
	F.Run(0.3f);
	TestEqual(TEXT("Failed promotion keeps prior tier"), R->Activation->GetCurrentTier(), EDocActivationTier::Active);
	TestTrue(TEXT("Failure reported"), F.S->GetDebugInfo(R->Activation).LastDiagnostic.Contains(TEXT("Prepare")));

	Adapter->bFailPrepare = false;
	Adapter->bFailCommit = true;
	F.Run(0.3f);
	TestTrue(TEXT("Failed commit rolled back"), Adapter->Rollbacks > 0);
	TestEqual(TEXT("Still Active"), R->Activation->GetCurrentTier(), EDocActivationTier::Active);

	Adapter->bFailCommit = false;
	F.Run(0.3f);
	TestEqual(TEXT("Adapter commits the swap"), R->Activation->GetCurrentTier(), EDocActivationTier::Representation);
	TestEqual(TEXT("Representation named"), F.S->GetDebugInfo(R->Activation).Representation, FName(TEXT("TestISM")));
	F.S->UnregisterRepresentationAdapter(Adapter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocActivationBudgetTest, "Doc.Activation.BudgetsAndUnload", Flags)
bool FDocActivationBudgetTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocWorldActivationSettings* Settings = GetMutableDefault<UDocWorldActivationSettings>();
	const int32 SavedTransitions = Settings->MaxTransitionsPerTick;
	const int32 SavedEvaluations = Settings->MaxEvaluationsPerTick;
	Settings->MaxTransitionsPerTick = 5;
	Settings->MaxEvaluationsPerTick = 1000;

	UDocActivationPolicy* Policy = MakeDistancePolicy(0.f);
	UDocActivationProfile* Profile = MakeProfile();
	TArray<ADocActivationTestActor*> Actors;
	for (int32 i = 0; i < 40; ++i) { Actors.Add(F.Spawn(FVector(20000 + i * 10, 0, 0), Policy, Profile)); }
	F.S->AdvanceForTesting(0.3f);
	const FDocActivationStats First = F.S->GetStats();
	TestTrue(TEXT("Transition budget obeyed"), First.TransitionsLastTick <= 5);
	TestTrue(TEXT("Remaining transitions queued"), First.PendingTransitions > 0);
	for (int32 i = 0; i < 12; ++i) { F.S->AdvanceForTesting(0.3f); }
	TestEqual(TEXT("Queue drained over later ticks"), F.S->GetStats().CountByTier.FindRef(EDocActivationTier::Dormant), 40);

	// Unload is not destruction: record and pins drop, no destruction semantics.
	ADocActivationTestActor* Victim = Actors[0];
	AActor* PinOwner = F.TW.Spawn<AActor>();
	F.S->PinTier(Victim->Activation, EDocActivationTier::Active, PinOwner, TEXT("Test"));
	const int32 Registered = F.S->GetStats().Registered;
	const int32 ChangesBefore = Victim->Participant->Changes;
	Victim->Destroy();
	F.S->AdvanceForTesting(0.3f);
	TestEqual(TEXT("Unregistered on unload"), F.S->GetStats().Registered, Registered - 1);
	TestEqual(TEXT("Counted as unload"), F.S->GetStats().UnregisteredByUnload, 1);
	TestEqual(TEXT("No tier change reported for the unloaded object"), Victim->Participant->Changes, ChangesBefore);

	Settings->MaxTransitionsPerTick = SavedTransitions;
	Settings->MaxEvaluationsPerTick = SavedEvaluations;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
