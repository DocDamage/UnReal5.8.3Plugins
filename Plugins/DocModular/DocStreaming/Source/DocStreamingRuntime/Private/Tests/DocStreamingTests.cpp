// DocStreaming automation tests (STR-01..STR-05 logic). The native backend is
// disabled and observed states are forced, so these prove lease/dependency logic,
// not real level loading (STR-06 needs a cooked host with real level assets).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocStreamingSubsystem.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformProcess.h"

namespace DocStreamingTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Intact, "Test.DocStreaming.Bridge.Intact");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Destroyed, "Test.DocStreaming.Bridge.Destroyed");

	UDocStreamingChunkDefinition* MakeChunk(FName Id)
	{
		UDocStreamingChunkDefinition* Def = NewObject<UDocStreamingChunkDefinition>(GetTransientPackage());
		Def->ChunkId = Id;
		Def->Level = TSoftObjectPtr<UWorld>(FSoftObjectPath(FString::Printf(TEXT("/Game/DocStreamingTest/%s.%s"), *Id.ToString(), *Id.ToString())));
		Def->TimeoutSeconds = 0.f;
		return Def;
	}

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocWorldStreamingSubsystem* S = nullptr;
		FFixture()
		{
			S = TW.GetSubsystem<UDocWorldStreamingSubsystem>();
			if (S) { S->SetNativeBackendEnabledForTesting(false); }
		}
		~FFixture()
		{
			if (S)
			{
				S->OnRequestChanged.Clear();
				S->OnRequestChangedDynamic.Clear();
			}
		}
		FDocChunkInstanceKey Key(FName Id, FGuid Scope = FGuid()) const { return FDocChunkInstanceKey{ Id, Scope }; }
	};
}

using namespace DocStreamingTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocStreamingRefCountTest, "Doc.Streaming.ReferenceCounting", Flags)
bool FDocStreamingRefCountTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocStreamingChunkDefinition* Town = MakeChunk(TEXT("Town"));

	const FDocStreamingRequestInfo A = F.S->RequestChunk(Town, nullptr, FGuid(), false, FTransform::Identity);
	const FDocStreamingRequestInfo B = F.S->RequestChunk(Town, nullptr, FGuid(), false, FTransform::Identity);
	TestEqual(TEXT("A pending"), A.State, EDocStreamingRequestState::Pending);
	TestNotEqual(TEXT("Independent leases"), A.Handle, B.Handle);
	TestEqual(TEXT("Two leases on one instance"), F.S->GetChunkStatus(F.Key(TEXT("Town"))).LeaseCount, 2);

	F.S->DebugForceObservedState(F.Key(TEXT("Town")), EDocChunkObservedState::Ready);
	F.S->PollNow();
	FDocStreamingRequestInfo Info;
	TestTrue(TEXT("A queryable"), F.S->GetRequestInfo(A.Handle, Info));
	TestEqual(TEXT("A ready"), Info.State, EDocStreamingRequestState::Ready);

	const FDocStreamingRequestInfo RelA = F.S->ReleaseChunk(A.Handle);
	TestEqual(TEXT("Release A"), RelA.State, EDocStreamingRequestState::Released);
	TestTrue(TEXT("Released but still resident for B"), RelA.bReleasedButResident);
	TestEqual(TEXT("Still observed ready"), F.S->GetChunkStatus(F.Key(TEXT("Town"))).Observed, EDocChunkObservedState::Ready);
	TestEqual(TEXT("Second release of A is NoChange"), F.S->ReleaseChunk(A.Handle).Result.Outcome, EDocResultOutcome::NoChange);

	const FDocStreamingRequestInfo RelB = F.S->ReleaseChunk(B.Handle);
	TestFalse(TEXT("Last release unloads"), RelB.bReleasedButResident);
	TestEqual(TEXT("Instance gone"), F.S->GetChunkStatus(F.Key(TEXT("Town"))).Observed, EDocChunkObservedState::Unrequested);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocStreamingDependencyTest, "Doc.Streaming.DependencyLoad", Flags)
bool FDocStreamingDependencyTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocStreamingChunkDefinition* Base = MakeChunk(TEXT("Base"));
	UDocStreamingChunkDefinition* Interior = MakeChunk(TEXT("Interior"));
	Interior->Dependencies.Add(Base);

	const FDocStreamingRequestInfo Req = F.S->RequestChunk(Interior, nullptr, FGuid(), false, FTransform::Identity);
	TestEqual(TEXT("Dependency instance created"), F.S->GetChunkStatus(F.Key(TEXT("Base"))).LeaseCount, 1);

	F.S->DebugForceObservedState(F.Key(TEXT("Interior")), EDocChunkObservedState::Ready);
	F.S->PollNow();
	FDocStreamingRequestInfo Info;
	F.S->GetRequestInfo(Req.Handle, Info);
	TestEqual(TEXT("Not ready while dependency loads"), Info.State, EDocStreamingRequestState::Pending);

	F.S->DebugForceObservedState(F.Key(TEXT("Base")), EDocChunkObservedState::Ready);
	F.S->PollNow();
	F.S->GetRequestInfo(Req.Handle, Info);
	TestEqual(TEXT("Ready once dependency ready"), Info.State, EDocStreamingRequestState::Ready);

	// A second requester of Base alone keeps it resident when Interior releases.
	const FDocStreamingRequestInfo BaseOnly = F.S->RequestChunk(Base, nullptr, FGuid(), false, FTransform::Identity);
	F.S->ReleaseChunk(Req.Handle);
	TestEqual(TEXT("Shared dependency kept for other lease"), F.S->GetChunkStatus(F.Key(TEXT("Base"))).LeaseCount, 1);
	F.S->ReleaseChunk(BaseOnly.Handle);
	TestEqual(TEXT("All released"), F.S->GetChunkStatus(F.Key(TEXT("Base"))).LeaseCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocStreamingCycleTest, "Doc.Streaming.DependencyCycle", Flags)
bool FDocStreamingCycleTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocStreamingChunkDefinition* A = MakeChunk(TEXT("CycA"));
	UDocStreamingChunkDefinition* B = MakeChunk(TEXT("CycB"));
	A->Dependencies.Add(B);
	B->Dependencies.Add(A);

	const FDocStreamingRequestInfo Req = F.S->RequestChunk(A, nullptr, FGuid(), false, FTransform::Identity);
	TestEqual(TEXT("Cycle rejected as invalid configuration"), Req.Result.Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("No partial activation of A"), F.S->GetChunkStatus(F.Key(TEXT("CycA"))).LeaseCount, 0);
	TestEqual(TEXT("No partial activation of B"), F.S->GetChunkStatus(F.Key(TEXT("CycB"))).LeaseCount, 0);

	UDocStreamingChunkDefinition* Broken = MakeChunk(TEXT("Broken"));
	Broken->Dependencies.Add(nullptr);
	TestEqual(TEXT("Missing dependency rejected"), F.S->RequestChunk(Broken, nullptr, FGuid(), false, FTransform::Identity).Result.Outcome, EDocResultOutcome::InvalidConfiguration);

	// Failure after acquisition unwinds this request's dependency leases only.
	UDocStreamingChunkDefinition* Dep = MakeChunk(TEXT("Dep"));
	UDocStreamingChunkDefinition* Top = MakeChunk(TEXT("Top"));
	Top->Dependencies.Add(Dep);
	const FDocStreamingRequestInfo Other = F.S->RequestChunk(Dep, nullptr, FGuid(), false, FTransform::Identity);
	const FDocStreamingRequestInfo TopReq = F.S->RequestChunk(Top, nullptr, FGuid(), false, FTransform::Identity);
	F.S->DebugForceObservedState(F.Key(TEXT("Top")), EDocChunkObservedState::Failed);
	F.S->PollNow();
	FDocStreamingRequestInfo Info;
	TestFalse(TEXT("Failed request removed"), F.S->GetRequestInfo(TopReq.Handle, Info));
	TestEqual(TEXT("Other owner's lease on Dep survives"), F.S->GetChunkStatus(F.Key(TEXT("Dep"))).LeaseCount, 1);
	F.S->ReleaseChunk(Other.Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocStreamingCancelTest, "Doc.Streaming.CancelStaleCompletion", Flags)
bool FDocStreamingCancelTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocStreamingChunkDefinition* Cave = MakeChunk(TEXT("Cave"));

	TArray<EDocStreamingRequestState> Seen;
	F.S->OnRequestChanged.AddLambda([&Seen](const FDocStreamingRequestInfo& I) { Seen.Add(I.State); });

	const FDocStreamingRequestInfo Req = F.S->RequestChunk(Cave, nullptr, FGuid(), false, FTransform::Identity);
	const FDocStreamingRequestInfo Cancel = F.S->ReleaseChunk(Req.Handle);
	TestEqual(TEXT("Cancelled while loading"), Cancel.State, EDocStreamingRequestState::Cancelled);

	// Late "completion" for the old request has nothing to resurrect.
	F.S->DebugForceObservedState(F.Key(TEXT("Cave")), EDocChunkObservedState::Ready);
	F.S->PollNow();
	TestEqual(TEXT("Exactly one terminal notification"), Seen.Num(), 1);
	TestTrue(TEXT("It was Cancelled"), Seen.Num() == 1 && Seen[0] == EDocStreamingRequestState::Cancelled);

	// Re-request starts fresh.
	const FDocStreamingRequestInfo Again = F.S->RequestChunk(Cave, nullptr, FGuid(), false, FTransform::Identity);
	TestEqual(TEXT("Re-request pending"), Again.State, EDocStreamingRequestState::Pending);

	// Timeout.
	UDocStreamingChunkDefinition* Slow = MakeChunk(TEXT("Slow"));
	Slow->TimeoutSeconds = 0.0001f;
	const FDocStreamingRequestInfo SlowReq = F.S->RequestChunk(Slow, nullptr, FGuid(), false, FTransform::Identity);
	FPlatformProcess::Sleep(0.01f);
	F.S->PollNow();
	TestEqual(TEXT("Timed out"), Seen.Last(), EDocStreamingRequestState::TimedOut);
	FDocStreamingRequestInfo Info;
	TestFalse(TEXT("Timed-out lease released"), F.S->GetRequestInfo(SlowReq.Handle, Info));
	F.S->ReleaseChunk(Again.Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocStreamingInstancesTest, "Doc.Streaming.RepeatedInstances", Flags)
bool FDocStreamingInstancesTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocStreamingChunkDefinition* House = MakeChunk(TEXT("House"));
	House->Readiness = EDocChunkReadiness::Visible;

	const FGuid Left(1, 0, 0, 0), Right(2, 0, 0, 0);
	const FDocStreamingRequestInfo L = F.S->RequestChunk(House, nullptr, Left, true, FTransform(FVector(0, 0, 0)));
	const FDocStreamingRequestInfo R = F.S->RequestChunk(House, nullptr, Right, true, FTransform(FVector(5000, 0, 0)));
	TestEqual(TEXT("Left instance"), F.S->GetChunkStatus(F.Key(TEXT("House"), Left)).LeaseCount, 1);
	TestEqual(TEXT("Right instance"), F.S->GetChunkStatus(F.Key(TEXT("House"), Right)).LeaseCount, 1);

	const FDocStreamingRequestInfo Clash = F.S->RequestChunk(House, nullptr, Left, true, FTransform(FVector(100, 0, 0)));
	TestEqual(TEXT("Same key, different transform → Conflict"), Clash.Result.Outcome, EDocResultOutcome::Conflict);

	// Loaded is not Ready for a Visible-readiness chunk.
	F.S->DebugForceObservedState(F.Key(TEXT("House"), Left), EDocChunkObservedState::Loaded);
	F.S->PollNow();
	FDocStreamingRequestInfo Info;
	F.S->GetRequestInfo(L.Handle, Info);
	TestEqual(TEXT("Loaded-not-visible is still pending"), Info.State, EDocStreamingRequestState::Pending);
	House->Readiness = EDocChunkReadiness::Loaded;
	F.S->PollNow();
	F.S->GetRequestInfo(L.Handle, Info);
	TestEqual(TEXT("Loaded readiness accepts Loaded"), Info.State, EDocStreamingRequestState::Ready);
	F.S->ReleaseChunk(L.Handle);
	F.S->ReleaseChunk(R.Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocStreamingWorldStateTest, "Doc.Streaming.WorldState", Flags)
bool FDocStreamingWorldStateTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }

	UDocWorldStateGroup* Group = NewObject<UDocWorldStateGroup>(GetTransientPackage());
	Group->GroupId = TEXT("Bridge");
	FDocWorldStateVariant Intact; Intact.StateTag = TAG_Intact; Intact.Chunks.Add(MakeChunk(TEXT("BridgeIntact")));
	FDocWorldStateVariant Broken; Broken.StateTag = TAG_Destroyed; Broken.Chunks.Add(MakeChunk(TEXT("BridgeDestroyed")));
	Group->Variants = { Intact, Broken };
	Group->DefaultState = TAG_Intact;

	TStrongObjectPtr<UObject> Story(NewObject<UDocWorldStateGroup>(GetTransientPackage()));
	TStrongObjectPtr<UObject> Cutscene(NewObject<UDocWorldStateGroup>(GetTransientPackage()));

	const FDocRequestHandle A = F.S->RequestWorldState(Group, TAG_Intact, 0, Story.Get());
	TestEqual(TEXT("Intact effective"), F.S->GetEffectiveWorldState(Group), FGameplayTag(TAG_Intact));
	const FDocRequestHandle B = F.S->RequestWorldState(Group, TAG_Destroyed, 5, Cutscene.Get());
	TestEqual(TEXT("Higher priority wins"), F.S->GetEffectiveWorldState(Group), FGameplayTag(TAG_Destroyed));
	TestTrue(TEXT("Transitioning until chunks are ready"), F.S->IsWorldStateTransitioning(Group));
	TestEqual(TEXT("Old variant chunk released"), F.S->GetChunkStatus(F.Key(TEXT("BridgeIntact"))).LeaseCount, 0);

	F.S->ReleaseWorldState(B);
	TestEqual(TEXT("Release recomputes (not blind deactivate)"), F.S->GetEffectiveWorldState(Group), FGameplayTag(TAG_Intact));
	TestEqual(TEXT("Second release NoChange"), F.S->ReleaseWorldState(B).Outcome, EDocResultOutcome::NoChange);
	F.S->ReleaseWorldState(A);
	TestEqual(TEXT("Default state when no requests"), F.S->GetEffectiveWorldState(Group), FGameplayTag(TAG_Intact));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
