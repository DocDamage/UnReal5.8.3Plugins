// DocUnlocksProgression automation tests (UNL-01..11 base logic). No quests,
// inventory, knowledge, UI or Events: conditions and effects use native test providers.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocUnlockSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DocUnlockTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Glow, "Test.Unlock.Glow");

	const FGuid Campaign(5, 5, 5, 5);
	FDocOwnerScope Player(int32 N) { return FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(0x0B0B, 1, 2, static_cast<uint32>(N)), Campaign); }
	FDocOwnerScope Shared() { return FDocOwnerScope(EDocOwnerScopeKind::SharedWorld, FGuid(0x0B0B, 9, 9, 9), Campaign); }

	FDocUnlockCondition Threshold(FName Key, double Value)
	{
		FDocUnlockCondition C;
		C.Type = EDocUnlockConditionType::NumericThreshold;
		C.ProgressKey = Key;
		C.Threshold = Value;
		return C;
	}

	FDocUnlockCondition Prereq(FName Id)
	{
		FDocUnlockCondition C;
		C.Type = EDocUnlockConditionType::Prerequisite;
		C.UnlockId = Id;
		return C;
	}

	FDocUnlockCondition Custom(FName Provider)
	{
		FDocUnlockCondition C;
		C.Type = EDocUnlockConditionType::Custom;
		C.ProviderId = Provider;
		return C;
	}

	using FExpr = FDocUnlockExpression;
	FExpr Leaf(const FDocUnlockCondition& C) { return FExpr::Leaf(C); }

	UDocUnlockDefinition* MakeUnlock(FName Id, const FExpr& Conditions = FExpr(), const FExpr& Prerequisites = FExpr(),
		bool bPermanent = true, EDocUnlockEvaluationPolicy Policy = EDocUnlockEvaluationPolicy::Auto)
	{
		UDocUnlockDefinition* D = NewObject<UDocUnlockDefinition>(GetTransientPackage());
		D->UnlockId = Id;
		D->Category = DocUnlockTags::Unlock_Feature;
		D->Conditions = Conditions;
		D->Prerequisites = Prerequisites;
		D->bPermanent = bPermanent;
		D->EvaluationPolicy = Policy;
		return D;
	}

	FDocUnlockAction Action(FName Id, EDocUnlockActionType Type, FName Provider = NAME_None, const FGameplayTag& Tag = FGameplayTag())
	{
		FDocUnlockAction A;
		A.ActionId = Id;
		A.Type = Type;
		A.ProviderId = Provider;
		A.Tag = Tag;
		return A;
	}

	FDocUnlockTemporarySource Duration(FName Source, float Seconds, EDocClockDomain Clock = EDocClockDomain::RealTime, bool bPersist = false)
	{
		FDocUnlockTemporarySource S;
		S.Kind = EDocUnlockTemporaryKind::DurationBased;
		S.SourceId = Source;
		S.DurationSeconds = Seconds;
		S.Clock = Clock;
		S.bPersist = bPersist;
		return S;
	}

	class FFlagProvider final : public IDocUnlockConditionProvider
	{
	public:
		EDocConditionState State = EDocConditionState::Satisfied;
		virtual FDocConditionResult EvaluateUnlockCondition(const FDocUnlockConditionQuery&) const override
		{
			return State == EDocConditionState::Satisfied ? FDocConditionResult::Satisfied()
				: State == EDocConditionState::Unsatisfied ? FDocConditionResult::Unsatisfied(FGameplayTag(), TEXT("No"))
				: FDocConditionResult::Unavailable(FGameplayTag(), TEXT("Down"));
		}
	};

	class FRecordingEffects final : public IDocUnlockEffectProvider
	{
	public:
		TArray<FDocEffectKey> Calls;
		TSet<FDocEffectKey> Applied;
		int32 FailNext = 0;
		virtual FDocSystemResult DeliverUnlockEffect(const FDocUnlockEffectRequest& Request) override
		{
			Calls.Add(Request.Key);
			if (Applied.Contains(Request.Key)) { return FDocSystemResult::MakeNoChange(TEXT("Receipt")); }
			if (FailNext > 0) { --FailNext; return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Full")); }
			Applied.Add(Request.Key);
			return FDocSystemResult::MakeSuccess();
		}
	};

	struct FFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocUnlockSubsystem> Keep;
		UDocUnlockSubsystem* S = nullptr;
		TArray<FDocUnlockChange> Changes;
		int32 Refreshes = 0;

		FFixture()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			S = NewService();
			Keep.Reset(S);
			S->OnUnlockChangedNative.AddLambda([this](const FDocUnlockChange& C) { Changes.Add(C); });
			S->OnStateRefreshedNative.AddLambda([this]() { ++Refreshes; });
		}
		~FFixture()
		{
			S->OnUnlockChangedNative.Clear();
			S->OnStateRefreshedNative.Clear();
		}
		UDocUnlockSubsystem* NewService() const { return NewObject<UDocUnlockSubsystem>(GameInstance.Get()); }
		bool Available(const FDocOwnerScope& O, FName Id) const { return S->IsAvailable(O, Id); }
	};
}

using namespace DocUnlockTests;

// UNL-01
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockBasicTest, "Doc.Unlock.Basic", DocUnlockTests::Flags)
bool FDocUnlockBasicTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TestTrue(TEXT("Register"), F.S->RegisterDefinitions({ MakeUnlock(TEXT("Dash"), Leaf(Threshold(TEXT("XP"), 10))),
		MakeUnlock(TEXT("Aura"), Leaf(Threshold(TEXT("XP"), 10)), FExpr(), false) }).IsSuccess());
	F.S->RegisterOwner(Player(1));
	F.S->RegisterOwner(Player(2));
	F.S->SetProgress(Player(1), TEXT("XP"), EDocUnlockProgressMode::SetValue, 5);
	TestFalse(TEXT("Below threshold"), F.Available(Player(1), TEXT("Dash")));
	F.S->SetProgress(Player(1), TEXT("XP"), EDocUnlockProgressMode::AddDelta, 5);
	TestTrue(TEXT("Unlocked at threshold"), F.Available(Player(1), TEXT("Dash")));
	TestFalse(TEXT("Owner-specific"), F.Available(Player(2), TEXT("Dash")));
	const FDocUnlockSnapshot Snap = F.S->GetUnlockSnapshot(Player(1), TEXT("Dash"));
	TestTrue(TEXT("Permanent entitlement"), Snap.Facts.bPermanentEntitlement && Snap.Label == EDocUnlockDisplayLabel::Unlocked && Snap.bVisible);
	F.S->SetProgress(Player(1), TEXT("XP"), EDocUnlockProgressMode::SetValue, 0);
	TestTrue(TEXT("Latched entitlement stays earned"), F.Available(Player(1), TEXT("Dash")));
	TestFalse(TEXT("Reversible unlock follows its input"), F.Available(Player(1), TEXT("Aura")));
	TestTrue(TEXT("Non-finite rejected"), F.S->SetProgress(Player(1), TEXT("XP"), EDocUnlockProgressMode::SetValue, std::numeric_limits<double>::quiet_NaN()).Outcome == EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Overflow rejected"), F.S->SetProgress(Player(1), TEXT("XP"), EDocUnlockProgressMode::AddDelta, 1.0e300).Outcome == EDocResultOutcome::InvalidInput);
	return true;
}

// UNL-02
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockPrerequisiteTest, "Doc.Unlock.Prerequisite", DocUnlockTests::Flags)
bool FDocUnlockPrerequisiteTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinitions({ MakeUnlock(TEXT("A"), Leaf(Threshold(TEXT("XP"), 10))),
		MakeUnlock(TEXT("C"), FExpr(), Leaf(Prereq(TEXT("A")))),
		MakeUnlock(TEXT("D"), FExpr(), Leaf(Prereq(TEXT("NotLoaded")))) });
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);

	FDocUnlockSnapshot C = F.S->GetUnlockSnapshot(P, TEXT("C"));
	TestTrue(TEXT("Unsatisfied prerequisite"), C.Eligibility.State == EDocConditionState::Unsatisfied && C.Eligibility.ReasonTag == DocUnlockTags::Error_Unlock_PrerequisiteLocked);
	FDocUnlockSnapshot D = F.S->GetUnlockSnapshot(P, TEXT("D"));
	TestTrue(TEXT("Missing prerequisite is Unavailable, not satisfied"), D.Eligibility.IsUnavailable() && D.Eligibility.ReasonTag == DocUnlockTags::Error_Unlock_MissingPrerequisite);
	TestFalse(TEXT("D locked"), F.Available(P, TEXT("D")));
	const TArray<FDocUnlockBlockingReason> Reasons = F.S->GetBlockingReasons(P, TEXT("C"));
	TestTrue(TEXT("Meaningful blocking reason"), Reasons.ContainsByPredicate([](const FDocUnlockBlockingReason& R) { return R.ReasonTag == DocUnlockTags::Error_Unlock_PrerequisiteLocked; }));
	F.S->SetProgress(P, TEXT("XP"), EDocUnlockProgressMode::SetValue, 10);
	TestTrue(TEXT("Chain unlocks in one batch"), F.Available(P, TEXT("A")) && F.Available(P, TEXT("C")));
	return true;
}

// UNL-03
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockAndConditionTest, "Doc.Unlock.AndCondition", DocUnlockTests::Flags)
bool FDocUnlockAndConditionTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeUnlock(TEXT("Both"), FExpr::Combine(EDocUnlockExprOp::And, { Leaf(Threshold(TEXT("XP"), 5)), Leaf(Threshold(TEXT("Gold"), 3)) })));
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);
	F.S->SetProgress(P, TEXT("XP"), EDocUnlockProgressMode::SetValue, 5);
	TestFalse(TEXT("One of two"), F.Available(P, TEXT("Both")));
	F.S->SetProgress(P, TEXT("Gold"), EDocUnlockProgressMode::SetValue, 3);
	TestTrue(TEXT("Both satisfied"), F.Available(P, TEXT("Both")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockOrConditionTest, "Doc.Unlock.OrCondition", DocUnlockTests::Flags)
bool FDocUnlockOrConditionTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinitions({
		MakeUnlock(TEXT("Either"), FExpr::Combine(EDocUnlockExprOp::Or, { Leaf(Threshold(TEXT("XP"), 100)), Leaf(Custom(TEXT("Quest"))) })),
		MakeUnlock(TEXT("Nested"), FExpr::Combine(EDocUnlockExprOp::And, {
			FExpr::Combine(EDocUnlockExprOp::Or, { Leaf(Threshold(TEXT("XP"), 100)), Leaf(Threshold(TEXT("Gold"), 3)) }),
			FExpr::Combine(EDocUnlockExprOp::Not, { Leaf(Custom(TEXT("Quest"))) }) })) });
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);
	F.S->SetProgress(P, TEXT("Gold"), EDocUnlockProgressMode::SetValue, 3);
	TestFalse(TEXT("Missing provider never satisfies an OR"), F.Available(P, TEXT("Either")));
	TestFalse(TEXT("NOT of Unavailable stays Unavailable"), F.Available(P, TEXT("Nested")));

	TSharedPtr<FFlagProvider> Quest = MakeShared<FFlagProvider>();
	F.S->RegisterConditionProvider(TEXT("Quest"), Quest);
	TestTrue(TEXT("OR satisfied by provider"), F.Available(P, TEXT("Either")));
	TestFalse(TEXT("NOT(Satisfied) blocks"), F.Available(P, TEXT("Nested")));
	Quest->State = EDocConditionState::Unsatisfied;
	F.S->NotifyProviderChanged(P, TEXT("Quest"));
	TestTrue(TEXT("Nested expression satisfied"), F.Available(P, TEXT("Nested")));
	return true;
}

// UNL-04
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockCycleDetectionTest, "Doc.Unlock.CycleDetection", DocUnlockTests::Flags)
bool FDocUnlockCycleDetectionTest::RunTest(const FString& Parameters)
{
	FFixture F;
	FDocSystemResult R = F.S->RegisterDefinition(MakeUnlock(TEXT("Self"), FExpr(), Leaf(Prereq(TEXT("Self")))));
	TestTrue(TEXT("Self-edge"), !R.IsSuccess() && R.Diagnostic.Contains(TEXT("Self -> Self")));
	R = F.S->RegisterDefinitions({ MakeUnlock(TEXT("X"), FExpr(), Leaf(Prereq(TEXT("Y")))), MakeUnlock(TEXT("Y"), FExpr(), Leaf(Prereq(TEXT("X")))) });
	TestTrue(TEXT("Indirect cycle with path"), R.ErrorTag == DocUnlockTags::Error_Unlock_Cycle && R.Diagnostic.Contains(TEXT("X -> Y -> X")));
	TestTrue(TEXT("First asset"), F.S->RegisterDefinition(MakeUnlock(TEXT("M"), FExpr(), Leaf(Prereq(TEXT("N"))))).IsSuccess());
	R = F.S->RegisterDefinition(MakeUnlock(TEXT("N"), FExpr(), Leaf(Prereq(TEXT("M")))));
	TestTrue(TEXT("Cross-asset cycle"), R.ErrorTag == DocUnlockTags::Error_Unlock_Cycle && R.Diagnostic.Contains(TEXT("M -> N -> M")));
	TestNull(TEXT("Rejected atomically"), F.S->FindDefinition(TEXT("N")));
	FDocUnlockExpression Bad;
	Bad.Nodes.AddDefaulted();
	Bad.Nodes[0].Op = EDocUnlockExprOp::And;
	Bad.Nodes[0].Children = { 0 };
	FString Error;
	TestFalse(TEXT("Invalid expression"), Bad.Validate(Error));
	return true;
}

// UNL-05
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockTemporaryTest, "Doc.Unlock.Temporary", DocUnlockTests::Flags)
bool FDocUnlockTemporaryTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeUnlock(TEXT("Fly"), Leaf(Threshold(TEXT("Never"), 1))));
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);
	FGuid Short, Long, Shrine;
	F.S->AcquireTemporaryGrant(P, TEXT("Fly"), Duration(TEXT("Potion"), 5.f), Short);
	F.S->AcquireTemporaryGrant(P, TEXT("Fly"), Duration(TEXT("Scroll"), 20.f), Long);
	FDocUnlockTemporarySource Region;
	Region.Kind = EDocUnlockTemporaryKind::RegionBased;
	Region.SourceId = TEXT("Shrine");
	Region.RegionId = TEXT("HolyGround");
	F.S->AcquireTemporaryGrant(P, TEXT("Fly"), Region, Shrine);
	TestTrue(TEXT("Temporarily unlocked"), F.S->GetUnlockSnapshot(P, TEXT("Fly")).Label == EDocUnlockDisplayLabel::TemporarilyUnlocked);
	F.S->AdvanceClock(EDocClockDomain::RealTime, 6.0);
	TestTrue(TEXT("Overlapping grant survives the other's expiry"), F.Available(P, TEXT("Fly")));
	TestTrue(TEXT("Only the owning source may release"), F.S->ReleaseTemporaryGrant(P, TEXT("Fly"), Long, TEXT("Potion")).Outcome == EDocResultOutcome::PermissionDenied);
	F.S->ReleaseTemporaryGrant(P, TEXT("Fly"), Long, TEXT("Scroll"));
	TestFalse(TEXT("Region grant needs membership"), F.Available(P, TEXT("Fly")));
	F.S->NotifyRegionMembership(P, TEXT("HolyGround"), true);
	TestTrue(TEXT("Inside the region"), F.Available(P, TEXT("Fly")));
	F.S->GrantPermanent(P, TEXT("Fly"), TEXT("Test"), FDocEffectKey());
	F.S->NotifyRegionMembership(P, TEXT("HolyGround"), false);
	F.S->ReleaseTemporaryGrant(P, TEXT("Fly"), Shrine, TEXT("Shrine"));
	TestTrue(TEXT("Temporary release never removes the permanent entitlement"), F.Available(P, TEXT("Fly")) && F.S->GetUnlockSnapshot(P, TEXT("Fly")).Label == EDocUnlockDisplayLabel::Unlocked);
	return true;
}

// UNL-06
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockSaveRestoreTest, "Doc.Unlock.SaveRestore", DocUnlockTests::Flags)
bool FDocUnlockSaveRestoreTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TSharedPtr<FRecordingEffects> Bag = MakeShared<FRecordingEffects>();
	auto Build = [&Bag](UDocUnlockSubsystem* S, bool bWithGone)
	{
		S->RegisterEffectProvider(TEXT("Bag"), Bag);
		UDocUnlockDefinition* Dash = MakeUnlock(TEXT("Dash"), Leaf(Threshold(TEXT("XP"), 10)));
		Dash->UnlockActions = { Action(TEXT("Gift"), EDocUnlockActionType::Custom, TEXT("Bag")) };
		TArray<UDocUnlockDefinition*> Defs = { Dash, MakeUnlock(TEXT("Fly"), Leaf(Threshold(TEXT("Never"), 1))) };
		if (bWithGone) { Defs.Add(MakeUnlock(TEXT("Gone"), FExpr(), FExpr(), true, EDocUnlockEvaluationPolicy::Manual)); }
		S->RegisterDefinitions(Defs);
	};
	Build(F.S, true);
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);
	Bag->FailNext = 1;
	F.S->SetProgress(P, TEXT("XP"), EDocUnlockProgressMode::SetValue, 10);
	FGuid Kept, Dropped, Session;
	F.S->AcquireTemporaryGrant(P, TEXT("Fly"), Duration(TEXT("Potion"), 30.f, EDocClockDomain::RealTime, true), Kept);
	F.S->AcquireTemporaryGrant(P, TEXT("Fly"), Duration(TEXT("Buff"), 30.f), Dropped);
	FDocUnlockTemporarySource SessionSource;
	SessionSource.Kind = EDocUnlockTemporaryKind::SessionBased;
	SessionSource.SourceId = TEXT("Match");
	SessionSource.SessionId = FGuid::NewGuid();
	F.S->AcquireTemporaryGrant(P, TEXT("Fly"), SessionSource, Session);
	F.S->AdvanceClock(EDocClockDomain::RealTime, 10.0);
	FDocEffectKey Receipt;
	Receipt.Owner = P;
	Receipt.ProducerInstanceId = FGuid::NewGuid();
	Receipt.ActionId = TEXT("GrantGone");
	F.S->GrantPermanent(P, TEXT("Gone"), TEXT("Store purchase"), Receipt);
	TestEqual(TEXT("One pending action"), F.S->GetActionIntents(P).FilterByPredicate([](const FDocUnlockActionIntent& I) { return I.State == EDocUnlockIntentState::Pending; }).Num(), 1);

	TArray<uint8> Bytes;
	DocCoreSerialization::Encode(F.S->CaptureState(), Bytes);
	FDocUnlockSaveData Saved;
	TestTrue(TEXT("Round trip"), DocCoreSerialization::Decode(Saved, Bytes));

	UDocUnlockSubsystem* S2 = F.NewService();
	TStrongObjectPtr<UDocUnlockSubsystem> Keep2(S2);
	Build(S2, false);
	const int32 Calls = Bag->Calls.Num();
	TestTrue(TEXT("Restore"), S2->RestoreState(Saved).IsSuccess());
	TestEqual(TEXT("No actions on restore"), Bag->Calls.Num(), Calls);
	TestTrue(TEXT("Entitlement restored"), S2->IsAvailable(P, TEXT("Dash")));
	const FDocUnlockSnapshot Fly = S2->GetUnlockSnapshot(P, TEXT("Fly"));
	TestEqual(TEXT("Only the persistent lease survives"), Fly.Facts.TemporaryGrants.Num(), 1);
	TestTrue(TEXT("Remaining time kept"), Fly.Facts.TemporaryGrants.Num() == 1 && FMath::IsNearlyEqual(Fly.Facts.TemporaryGrants[0].RemainingSeconds, 20.0));
	TestTrue(TEXT("Unknown unlock preserved (quarantined)"), S2->CaptureState().Owners[0].Records.ContainsByPredicate([](const FDocUnlockRecord& R) { return R.UnlockId == FName(TEXT("Gone")); }));
	TestTrue(TEXT("Pending action retried with its original key"), S2->RetryPendingActions(P).IsSuccess() && Bag->Applied.Num() == 1);

	UDocUnlockSubsystem* S3 = F.NewService();
	TStrongObjectPtr<UDocUnlockSubsystem> Keep3(S3);
	Build(S3, true);
	S3->RestoreState(Saved);
	const int32 Audits = S3->GetAuditLog(P).Num();
	TestTrue(TEXT("Receipt returns the original result"), S3->GrantPermanent(P, TEXT("Gone"), TEXT("Store purchase"), Receipt).IsChanged());
	TestEqual(TEXT("No duplicate grant"), S3->GetAuditLog(P).Num(), Audits);
	return true;
}

// UNL-07
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockDisabledAndHiddenTest, "Doc.Unlock.DisabledAndHidden", DocUnlockTests::Flags)
bool FDocUnlockDisabledAndHiddenTest::RunTest(const FString& Parameters)
{
	FFixture F;
	UDocUnlockDefinition* Secret = MakeUnlock(TEXT("Secret"), Leaf(Threshold(TEXT("XP"), 10)));
	Secret->bHiddenUntilAvailable = true;
	F.S->RegisterDefinitions({ MakeUnlock(TEXT("Map"), Leaf(Threshold(TEXT("XP"), 1))), Secret });
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);
	TestFalse(TEXT("Hidden until available"), F.S->GetUnlockSnapshot(P, TEXT("Secret")).bVisible);
	F.S->SetProgress(P, TEXT("XP"), EDocUnlockProgressMode::SetValue, 10);
	TestTrue(TEXT("Visible once available"), F.S->GetUnlockSnapshot(P, TEXT("Secret")).bVisible);

	F.S->SetDisabled(P, TEXT("Map"), true, TEXT("Maintenance"));
	FDocUnlockSnapshot Map = F.S->GetUnlockSnapshot(P, TEXT("Map"));
	TestTrue(TEXT("Disabled suppresses availability"), !Map.bEffectiveAvailable && Map.Label == EDocUnlockDisplayLabel::Disabled);
	TestTrue(TEXT("Entitlement not erased"), Map.Facts.bPermanentEntitlement && F.S->IsUnlocked(P, TEXT("Map")));
	F.S->SetDisabled(P, TEXT("Map"), false, TEXT("Done"));
	F.S->SetVisibility(P, TEXT("Map"), EDocUnlockVisibility::ForceHidden);
	Map = F.S->GetUnlockSnapshot(P, TEXT("Map"));
	TestTrue(TEXT("Hidden is presentation only"), !Map.bVisible && Map.bEffectiveAvailable);
	return true;
}

// UNL-08
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockDirtyPropagationTest, "Doc.Unlock.DirtyPropagation", DocUnlockTests::Flags)
bool FDocUnlockDirtyPropagationTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinitions({
		MakeUnlock(TEXT("C"), FExpr(), Leaf(Prereq(TEXT("B")))),
		MakeUnlock(TEXT("A"), Leaf(Threshold(TEXT("XP"), 10))),
		MakeUnlock(TEXT("B"), FExpr(), Leaf(Prereq(TEXT("A")))),
		MakeUnlock(TEXT("D"), Leaf(Threshold(TEXT("Other"), 10))),
		MakeUnlock(TEXT("E"), FExpr(), Leaf(Prereq(TEXT("A"))), true, EDocUnlockEvaluationPolicy::Batch) });
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);
	const int32 Before = F.S->GetEvaluationCountForTesting();
	F.Changes.Reset();
	F.S->SetProgress(P, TEXT("XP"), EDocUnlockProgressMode::SetValue, 10);
	TestEqual(TEXT("Only affected nodes evaluated"), F.S->GetEvaluationCountForTesting() - Before, 3);
	TestTrue(TEXT("Stable topological order"), F.Changes.Num() == 3 && F.Changes[0].UnlockId == FName(TEXT("A")) && F.Changes[1].UnlockId == FName(TEXT("B")) && F.Changes[2].UnlockId == FName(TEXT("C")));
	TestTrue(TEXT("Batch node is pending"), F.S->GetUnlockSnapshot(P, TEXT("E")).bPending);
	TestEqual(TEXT("Budgeted batch drains"), F.S->EvaluateDirty(P, 1), 0);
	TestTrue(TEXT("E unlocked"), F.Available(P, TEXT("E")));
	const int32 After = F.S->GetEvaluationCountForTesting();
	TestTrue(TEXT("Repeated input is NoChange"), F.S->SetProgress(P, TEXT("XP"), EDocUnlockProgressMode::SetValue, 10).Outcome == EDocResultOutcome::NoChange);
	TestEqual(TEXT("No work for no change"), F.S->GetEvaluationCountForTesting(), After);

	int32 Reentries = 0;
	F.S->OnUnlockChangedNative.AddLambda([&F, &Reentries, P](const FDocUnlockChange&)
	{
		++Reentries;
		F.S->SetProgress(P, TEXT("Other"), EDocUnlockProgressMode::AddDelta, 10); // runs after the committed batch
	});
	F.S->RevokePermanent(P, TEXT("D"), TEXT("noop"));
	F.S->SetProgress(P, TEXT("Other"), EDocUnlockProgressMode::SetValue, 10);
	TestTrue(TEXT("Reentrancy bounded"), Reentries > 0 && Reentries < 64);
	F.S->OnUnlockChangedNative.Clear();
	return true;
}

// UNL-09
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockClockPolicyTest, "Doc.Unlock.ClockPolicy", DocUnlockTests::Flags)
bool FDocUnlockClockPolicyTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinition(MakeUnlock(TEXT("Haste"), Leaf(Threshold(TEXT("Never"), 1))));
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);
	FGuid G;
	F.S->AcquireTemporaryGrant(P, TEXT("Haste"), Duration(TEXT("Potion"), 10.f, EDocClockDomain::WorldGameplay), G);
	F.S->AdvanceClock(EDocClockDomain::RealTime, 100.0);
	TestTrue(TEXT("Paused world clock does not age it"), F.Available(P, TEXT("Haste")));
	F.S->AdvanceClock(EDocClockDomain::WorldGameplay, -50.0);
	F.Changes.Reset();
	F.S->AdvanceClock(EDocClockDomain::WorldGameplay, 1000.0);
	TestTrue(TEXT("Jump expires it once"), !F.Available(P, TEXT("Haste")) && F.Changes.Num() == 1);
	F.S->AdvanceClock(EDocClockDomain::WorldGameplay, -2000.0);
	TestFalse(TEXT("Backward time never resurrects"), F.Available(P, TEXT("Haste")));

	FDocUnlockTemporarySource SessionSource;
	SessionSource.Kind = EDocUnlockTemporaryKind::SessionBased;
	SessionSource.SourceId = TEXT("Match");
	SessionSource.SessionId = FGuid::NewGuid();
	F.S->AcquireTemporaryGrant(P, TEXT("Haste"), SessionSource, G);
	TestTrue(TEXT("Session grant"), F.Available(P, TEXT("Haste")));
	F.S->EndSession(SessionSource.SessionId);
	TestFalse(TEXT("Expires with its session"), F.Available(P, TEXT("Haste")));

	TSharedPtr<FFlagProvider> Buff = MakeShared<FFlagProvider>();
	F.S->RegisterConditionProvider(TEXT("Buff"), Buff);
	FDocUnlockTemporarySource Conditional;
	Conditional.Kind = EDocUnlockTemporaryKind::ConditionBased;
	Conditional.SourceId = TEXT("Aura");
	Conditional.Condition = Leaf(Custom(TEXT("Buff")));
	F.S->AcquireTemporaryGrant(P, TEXT("Haste"), Conditional, G);
	TestTrue(TEXT("Condition grant"), F.Available(P, TEXT("Haste")));
	F.S->RegisterConditionProvider(TEXT("Buff"), nullptr);
	TestFalse(TEXT("Provider loss fails closed"), F.Available(P, TEXT("Haste")));
	return true;
}

// UNL-10
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockActionReceiptTest, "Doc.Unlock.ActionReceipt", DocUnlockTests::Flags)
bool FDocUnlockActionReceiptTest::RunTest(const FString& Parameters)
{
	FFixture F;
	TSharedPtr<FRecordingEffects> Bag = MakeShared<FRecordingEffects>();
	F.S->RegisterEffectProvider(TEXT("Bag"), Bag);
	UDocUnlockDefinition* U = MakeUnlock(TEXT("U"), FExpr(), FExpr(), true, EDocUnlockEvaluationPolicy::Manual);
	U->UnlockActions = { Action(TEXT("Glow"), EDocUnlockActionType::GrantGameplayTag, NAME_None, TAG_Glow), Action(TEXT("Gift"), EDocUnlockActionType::Custom, TEXT("Bag")) };
	U->LockActions = { Action(TEXT("Unglow"), EDocUnlockActionType::RemoveGameplayTag, NAME_None, TAG_Glow) };
	UDocUnlockDefinition* V = MakeUnlock(TEXT("V"), FExpr(), FExpr(), true, EDocUnlockEvaluationPolicy::Manual);
	V->UnlockActions = { Action(TEXT("Glow"), EDocUnlockActionType::GrantGameplayTag, NAME_None, TAG_Glow) };
	F.S->RegisterDefinitions({ U, V });
	const FDocOwnerScope P = Player(1);

	Bag->FailNext = 1;
	F.S->GrantPermanent(P, TEXT("U"), TEXT("Test"), FDocEffectKey());
	TestTrue(TEXT("Consumer failure keeps the entitlement"), F.Available(P, TEXT("U")) && F.S->GetUnlockSnapshot(P, TEXT("U")).PendingActions == 1);
	TestTrue(TEXT("Tag granted"), F.S->GetGrantedTags(P).HasTagExact(TAG_Glow));
	TestTrue(TEXT("Retry"), F.S->RetryPendingActions(P).IsSuccess());
	TestTrue(TEXT("Same key on retry"), Bag->Calls.Num() == 2 && Bag->Calls[0] == Bag->Calls[1]);
	const int32 Intents = F.S->GetActionIntents(P).Num();
	TestTrue(TEXT("Duplicate grant is NoChange"), F.S->GrantPermanent(P, TEXT("U"), TEXT("Test"), FDocEffectKey()).Outcome == EDocResultOutcome::NoChange);
	F.S->EvaluateUnlock(P, TEXT("U"));
	TestEqual(TEXT("No repeated actions"), F.S->GetActionIntents(P).Num(), Intents);

	F.S->GrantPermanent(P, TEXT("V"), TEXT("Test"), FDocEffectKey());
	TestTrue(TEXT("Revocation needs an audit reason"), F.S->RevokePermanent(P, TEXT("U"), TEXT("")).Outcome == EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Authorized revoke"), F.S->RevokePermanent(P, TEXT("U"), TEXT("Refund")).IsSuccess());
	TestFalse(TEXT("Revoked"), F.Available(P, TEXT("U")));
	TestTrue(TEXT("Overlapping claim keeps the tag"), F.S->GetGrantedTags(P).HasTagExact(TAG_Glow));
	TestTrue(TEXT("Audited"), F.S->GetAuditLog(P).ContainsByPredicate([](const FDocUnlockAuditEntry& E) { return E.Operation == TEXT("RevokePermanent") && E.Reason == TEXT("Refund"); }));
	return true;
}

// UNL-11 (local part)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUnlockAuthoritativeGateTest, "Doc.Unlock.AuthoritativeGate", DocUnlockTests::Flags)
bool FDocUnlockAuthoritativeGateTest::RunTest(const FString& Parameters)
{
	FFixture F;
	F.S->RegisterDefinitions({ MakeUnlock(TEXT("Door"), Leaf(Threshold(TEXT("Keys"), 1))), MakeUnlock(TEXT("Vault"), Leaf(Threshold(TEXT("Keys"), 5))) });
	const FDocOwnerScope P = Player(1);
	F.S->RegisterOwner(P);
	F.S->SetProgress(P, TEXT("Keys"), EDocUnlockProgressMode::SetValue, 1);
	const int64 Rev = F.S->GetOwnerRevision(P);
	TestTrue(TEXT("Valid gate"), F.S->ValidateGate(P, TEXT("Door"), Rev).IsSuccess());
	TestTrue(TEXT("Stale client view rejected"), F.S->ValidateGate(P, TEXT("Door"), Rev - 1).ErrorTag == DocUnlockTags::Error_Unlock_StaleRevision);
	TestTrue(TEXT("Locked gate reports why"), F.S->ValidateGate(P, TEXT("Vault"), Rev).Outcome == EDocResultOutcome::NotReady);

	F.S->SetAuthorityOverrideForTesting(false);
	TestTrue(TEXT("Client cannot validate gates"), F.S->ValidateGate(P, TEXT("Door"), Rev).Outcome == EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Client cannot forge progress"), F.S->SetProgress(P, TEXT("Keys"), EDocUnlockProgressMode::SetValue, 5).Outcome == EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Client cannot grant"), F.S->GrantPermanent(P, TEXT("Vault"), TEXT("Forged"), FDocEffectKey()).Outcome == EDocResultOutcome::PermissionDenied);
	F.S->SetAuthorityOverrideForTesting(TOptional<bool>());

	FDocUnlockSnapshot Snap;
	TestTrue(TEXT("Private to its owner"), F.S->GetSnapshotForViewer(Player(2), P, TEXT("Door"), Snap).ErrorTag == DocUnlockTags::Error_Unlock_Private);
	TestTrue(TEXT("Owner may view"), F.S->GetSnapshotForViewer(P, P, TEXT("Door"), Snap).IsSuccess());
	TestTrue(TEXT("Shared scope is public"), F.S->GetSnapshotForViewer(Player(2), Shared(), TEXT("Door"), Snap).IsSuccess());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
