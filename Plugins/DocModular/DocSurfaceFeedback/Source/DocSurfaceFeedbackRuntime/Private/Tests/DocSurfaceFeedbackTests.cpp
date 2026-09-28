// DocSurfaceFeedback automation tests (SFC-01..11 base-applicable logic). A recording
// executor stands in for audio/decals: it proves dispatch and ownership, not audible
// or visible output.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocSurfaceFeedbackSubsystem.h"
#include "NativeGameplayTags.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include <limits>

namespace DocSurfaceTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Wet, "Test.DocSurface.Wet");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Crouch, "Test.DocSurface.Crouch");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Damage, "Test.DocSurface.Damage");

	class FRecordingExecutor final : public IDocSurfaceResponseExecutor
	{
	public:
		virtual EDocSurfaceAdmission Execute(UWorld*, const FDocSurfaceFeedbackRequest& Request, const FDocSurfaceResponse&, UObject* Asset, float, int32& OutInstance, FString&) override
		{
			OutInstance = Next++;
			Live.Add(OutInstance);
			Executed.Add(Asset);
			return EDocSurfaceAdmission::Executed;
		}
		virtual bool IsInstanceActive(int32 Instance) const override { return Live.Contains(Instance); }
		virtual void StopInstance(int32 Instance) override { Live.Remove(Instance); Stopped.Add(Instance); }
		void FinishAll() { Live.Reset(); }
		TSet<int32> Live;
		TArray<int32> Stopped;
		TArray<UObject*> Executed;
		int32 Next = 1;
	};

	class FGameplayProvider final : public IDocSurfaceGameplayProvider
	{
	public:
		int32 Applied = 0;
		virtual FDocSystemResult ApplySurfaceGameplayResponse(const FDocSurfaceFeedbackRequest&, const FDocSurfaceResponse&) override { ++Applied; return FDocSystemResult::MakeSuccess(); }
	};

	FDocSurfaceResponseRule MakeRule(FName Id, const FGameplayTag& Event, const FGameplayTag& Surface = FGameplayTag(), int32 Priority = 0)
	{
		FDocSurfaceResponseRule Rule;
		Rule.RuleId = Id;
		Rule.EventTag = Event;
		Rule.Surface = Surface;
		Rule.Priority = Priority;
		FDocSurfaceResponse Response;
		Response.Kind = EDocSurfaceResponseKind::Sound;
		Response.Variations.Add(TSoftObjectPtr<UObject>(NewObject<USoundWave>(GetTransientPackage())));
		Rule.Responses.Add(Response);
		return Rule;
	}

	FDocSurfaceMaterialEntry MakeEntry(UPhysicalMaterial* Material, const FGameplayTag& Surface)
	{
		FDocSurfaceMaterialEntry Entry;
		Entry.Material = Material;
		Entry.Surface = Surface;
		return Entry;
	}

	UPhysicalMaterial* MakeMaterial(EPhysicalSurface Type)
	{
		UPhysicalMaterial* M = NewObject<UPhysicalMaterial>(GetTransientPackage());
		M->SurfaceType = Type;
		return M;
	}

	FDocSurfaceFeedbackRequest MakeRequest(const FGameplayTag& Event, UPhysicalMaterial* Material = nullptr, float Magnitude = 1.f)
	{
		FDocSurfaceFeedbackRequest R;
		R.EventTag = Event;
		R.PhysicalMaterial = Material;
		R.Magnitude = Magnitude;
		return R;
	}

	struct FFixture
	{
		FDocScopedTestWorld TW;
		UDocSurfaceFeedbackSubsystem* S = nullptr;
		TSharedPtr<FRecordingExecutor> Sound = MakeShared<FRecordingExecutor>();
		UDocSurfaceResponseProfile* Profile = nullptr;
		UDocSurfaceMappingAsset* Mapping = nullptr;

		FFixture()
		{
			S = TW.GetSubsystem<UDocSurfaceFeedbackSubsystem>();
			if (S)
			{
				S->RegisterExecutor(EDocSurfaceResponseKind::Sound, Sound);
				S->SetViewLocationsOverride({ FVector::ZeroVector });
			}
			Mapping = NewObject<UDocSurfaceMappingAsset>(GetTransientPackage());
			Profile = NewObject<UDocSurfaceResponseProfile>(GetTransientPackage());
			Profile->Mapping = Mapping;
		}
	};
}

using namespace DocSurfaceTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceMaterialTest, "Doc.Surface.PhysicalMaterialResolution", Flags)
bool FDocSurfaceMaterialTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UPhysicalMaterial* Oak = MakeMaterial(SurfaceType2);
	UPhysicalMaterial* Plate = MakeMaterial(SurfaceType2);
	UPhysicalMaterial* Other = MakeMaterial(SurfaceType5);
	F.Mapping->Materials.Add(MakeEntry(Oak, DocSurfaceTags::Wood));
	FDocSurfaceTypeEntry TypeEntry;
	TypeEntry.SurfaceType = SurfaceType2;
	TypeEntry.Surface = DocSurfaceTags::Metal;
	F.Mapping->SurfaceTypes.Add(TypeEntry);

	EDocSurfaceOrigin Origin;
	TestEqual(TEXT("Explicit material wins"), F.S->ResolveSurface(MakeRequest(DocSurfaceTags::Event_Footstep, Oak), F.Mapping, Origin), DocSurfaceTags::Wood.GetTag());
	TestEqual(TEXT("Origin material"), Origin, EDocSurfaceOrigin::PhysicalMaterial);
	TestEqual(TEXT("Surface type mapping"), F.S->ResolveSurface(MakeRequest(DocSurfaceTags::Event_Footstep, Plate), F.Mapping, Origin), DocSurfaceTags::Metal.GetTag());
	TestEqual(TEXT("Origin surface type"), Origin, EDocSurfaceOrigin::PhysicalSurfaceType);
	TestFalse(TEXT("Unmapped without default"), F.S->ResolveSurface(MakeRequest(DocSurfaceTags::Event_Footstep, Other), F.Mapping, Origin).IsValid());
	TestEqual(TEXT("Origin unmapped"), Origin, EDocSurfaceOrigin::Unmapped);

	F.Mapping->DefaultSurface = DocSurfaceTags::Concrete;
	TestEqual(TEXT("Configured default"), F.S->ResolveSurface(MakeRequest(DocSurfaceTags::Event_Footstep, Other), F.Mapping, Origin), DocSurfaceTags::Concrete.GetTag());
	TestEqual(TEXT("Default is distinguishable from unmapped"), Origin, EDocSurfaceOrigin::ConfiguredDefault);
	F.S->ResolveSurface(MakeRequest(DocSurfaceTags::Event_Footstep, nullptr), F.Mapping, Origin);
	TestEqual(TEXT("Null material uses default"), Origin, EDocSurfaceOrigin::ConfiguredDefault);
	FDocSurfaceFeedbackRequest Air = MakeRequest(DocSurfaceTags::Event_Footstep, Oak);
	Air.bHasHit = false;
	F.S->ResolveSurface(Air, F.Mapping, Origin);
	TestEqual(TEXT("No hit"), Origin, EDocSurfaceOrigin::NoHit);

	F.Mapping->Precedence = { EDocSurfaceMappingStage::PhysicalSurfaceType, EDocSurfaceMappingStage::PhysicalMaterial };
	TestEqual(TEXT("Configurable precedence"), F.S->ResolveSurface(MakeRequest(DocSurfaceTags::Event_Footstep, Oak), F.Mapping, Origin), DocSurfaceTags::Metal.GetTag());
	TestEqual(TEXT("Host surface index untouched"), (int32)Oak->SurfaceType, (int32)SurfaceType2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceExactTest, "Doc.Surface.ExactMatch", Flags)
bool FDocSurfaceExactTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UPhysicalMaterial* Oak = MakeMaterial(SurfaceType1);
	F.Mapping->Materials.Add(MakeEntry(Oak, DocSurfaceTags::Wood));
	F.Profile->Rules = { MakeRule(TEXT("Specific"), DocSurfaceTags::Event_Footstep, DocSurfaceTags::Wood, 0), MakeRule(TEXT("Loud"), DocSurfaceTags::Event_Footstep, FGameplayTag(), 5) };
	TestEqual(TEXT("Priority first"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep, Oak), F.Profile).SelectedRule, FName(TEXT("Loud")));

	F.Profile->Rules = { MakeRule(TEXT("Impact"), DocSurfaceTags::Event_Impact), MakeRule(TEXT("Projectile"), DocSurfaceTags::Event_Projectile) };
	TestEqual(TEXT("Event specificity"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Projectile, Oak), F.Profile).SelectedRule, FName(TEXT("Projectile")));

	F.Profile->Rules = { MakeRule(TEXT("Any"), DocSurfaceTags::Event_Footstep), MakeRule(TEXT("Wood"), DocSurfaceTags::Event_Footstep, DocSurfaceTags::Wood) };
	TestEqual(TEXT("Surface specificity"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep, Oak), F.Profile).SelectedRule, FName(TEXT("Wood")));

	F.Profile->Rules = { MakeRule(TEXT("B"), DocSurfaceTags::Event_Footstep), MakeRule(TEXT("A"), DocSurfaceTags::Event_Footstep) };
	for (int32 i = 0; i < 5; ++i)
	{
		TestEqual(TEXT("Stable RuleId tie-break"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep, Oak), F.Profile).SelectedRule, FName(TEXT("A")));
	}
	const TArray<FString> Problems = F.Profile->FindAuthoringProblems();
	TestTrue(TEXT("Ambiguous tie reported to authors"), Problems.ContainsByPredicate([](const FString& P) { return P.Contains(TEXT("ambiguous")); }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceFallbackTest, "Doc.Surface.Fallback", Flags)
bool FDocSurfaceFallbackTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UPhysicalMaterial* Oak = MakeMaterial(SurfaceType1);
	UPhysicalMaterial* Steel = MakeMaterial(SurfaceType2);
	F.Mapping->Materials = { MakeEntry(Oak, DocSurfaceTags::Wood), MakeEntry(Steel, DocSurfaceTags::Metal) };
	FDocSurfaceResponseRule Wet = MakeRule(TEXT("WetWood"), DocSurfaceTags::Event_Footstep, DocSurfaceTags::Wood);
	Wet.RequiredContext.AddTag(TAG_Wet);
	F.Profile->Rules = { Wet, MakeRule(TEXT("Wood"), DocSurfaceTags::Event_Footstep, DocSurfaceTags::Wood),
		MakeRule(TEXT("Step"), DocSurfaceTags::Event_Footstep), MakeRule(TEXT("Default"), FGameplayTag()) };

	FDocSurfaceFeedbackRequest R = MakeRequest(DocSurfaceTags::Event_Footstep, Oak);
	R.ContextTags.AddTag(TAG_Wet);
	FDocSurfaceFeedbackResult Result = F.S->ResolveFeedback(R, F.Profile);
	TestEqual(TEXT("Event+surface+context"), Result.MatchPath, EDocSurfaceMatchPath::EventSurfaceContext);
	TestEqual(TEXT("Wet rule"), Result.SelectedRule, FName(TEXT("WetWood")));
	Result = F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep, Oak), F.Profile);
	TestEqual(TEXT("Event+surface"), Result.MatchPath, EDocSurfaceMatchPath::EventSurface);
	Result = F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep, Steel), F.Profile);
	TestEqual(TEXT("Event only"), Result.MatchPath, EDocSurfaceMatchPath::EventOnly);
	Result = F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Jump, Steel), F.Profile);
	TestEqual(TEXT("Default"), Result.MatchPath, EDocSurfaceMatchPath::Default);
	TestTrue(TEXT("Rejected candidates summarized"), Result.Rejected.Num() > 0 || Result.CandidateCount > 0);

	// Missing variation uses the response fallback, not another rule.
	FDocSurfaceResponseRule Broken = MakeRule(TEXT("Broken"), DocSurfaceTags::Event_Break);
	Broken.Responses[0].Variations = { TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/DocSurfaceTest/Missing.Missing"))) };
	USoundWave* FallbackSound = NewObject<USoundWave>(GetTransientPackage());
	Broken.Responses[0].Fallback = FallbackSound;
	F.Profile->Rules.Add(Broken);
	F.S->SetLoaderForTesting([](const TArray<FSoftObjectPath>&, TFunction<void()> Done) { Done(); });
	Result = F.S->SubmitFeedback(MakeRequest(DocSurfaceTags::Event_Break, Steel), F.Profile);
	TestEqual(TEXT("Broken rule still selected"), Result.SelectedRule, FName(TEXT("Broken")));
	TestTrue(TEXT("Fallback asset executed"), F.Sound->Executed.Contains(FallbackSound));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceContextTest, "Doc.Surface.ContextModifier", Flags)
bool FDocSurfaceContextTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	FDocSurfaceResponseRule Quiet = MakeRule(TEXT("Quiet"), DocSurfaceTags::Event_Footstep, FGameplayTag(), 1);
	Quiet.BlockedContext.AddTag(TAG_Crouch);
	Quiet.MinMagnitude = 0.5f;
	Quiet.MaxMagnitude = 1.f;
	FDocSurfaceResponseRule ExactImpact = MakeRule(TEXT("ExactImpact"), DocSurfaceTags::Event_Impact, FGameplayTag(), 2);
	ExactImpact.bExactEvent = true;
	F.Profile->Rules = { Quiet, ExactImpact, MakeRule(TEXT("AnyImpact"), DocSurfaceTags::Event_Impact), MakeRule(TEXT("Default"), FGameplayTag()) };

	TestEqual(TEXT("Min inclusive"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep, nullptr, 0.5f), F.Profile).SelectedRule, FName(TEXT("Quiet")));
	TestEqual(TEXT("Max inclusive"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep, nullptr, 1.f), F.Profile).SelectedRule, FName(TEXT("Quiet")));
	TestEqual(TEXT("Above max rejected"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep, nullptr, 1.01f), F.Profile).SelectedRule, FName(TEXT("Default")));
	FDocSurfaceFeedbackRequest Crouched = MakeRequest(DocSurfaceTags::Event_Footstep, nullptr, 0.7f);
	Crouched.ContextTags.AddTag(TAG_Crouch);
	TestEqual(TEXT("Blocked context"), F.S->ResolveFeedback(Crouched, F.Profile).SelectedRule, FName(TEXT("Default")));
	TestEqual(TEXT("Hierarchy: child event matches ancestor rule"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Melee), F.Profile).SelectedRule, FName(TEXT("AnyImpact")));
	TestEqual(TEXT("Exact rule only for exact event"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Impact), F.Profile).SelectedRule, FName(TEXT("ExactImpact")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceNoMatchTest, "Doc.Surface.NoMatch", Flags)
bool FDocSurfaceNoMatchTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	TestEqual(TEXT("No rules"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep), F.Profile).Result.Outcome, EDocResultOutcome::NotFound);
	TestEqual(TEXT("Missing profile"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep), nullptr).Result.Outcome, EDocResultOutcome::NotFound);
	FDocSurfaceFeedbackRequest Bad = MakeRequest(DocSurfaceTags::Event_Footstep);
	Bad.Magnitude = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("Non-finite rejected"), F.S->ResolveFeedback(Bad, F.Profile).Result.Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Missing event"), F.S->ResolveFeedback(FDocSurfaceFeedbackRequest(), F.Profile).Result.Outcome, EDocResultOutcome::InvalidInput);

	F.Profile->Rules = { MakeRule(TEXT("Step"), DocSurfaceTags::Event_Footstep) };
	FDocSurfaceFeedbackRequest ZeroNormal = MakeRequest(DocSurfaceTags::Event_Footstep);
	ZeroNormal.Normal = FVector::ZeroVector;
	const FDocSurfaceFeedbackResult R = F.S->SubmitFeedback(ZeroNormal, F.Profile);
	TestTrue(TEXT("Invalid normal: safe fallback, reported"), R.Result.IsSuccess() && R.bNormalFallback);
	FDocSurfaceFeedbackRequest Air = MakeRequest(DocSurfaceTags::Event_Footstep);
	Air.bHasHit = false;
	TestEqual(TEXT("No hit resolves with NoHit origin"), F.S->ResolveFeedback(Air, F.Profile).SurfaceOrigin, EDocSurfaceOrigin::NoHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceProducersTest, "Doc.Surface.FootstepProducers", Flags)
bool FDocSurfaceProducersTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	F.Profile->Rules = { MakeRule(TEXT("Step"), DocSurfaceTags::Event_Footstep), MakeRule(TEXT("Land"), DocSurfaceTags::Event_Landing) };
	int32 Steps = 0, Landings = 0;
	F.S->OnFeedbackDispatchedNative.AddLambda([&](const FDocSurfaceFeedbackResult& R)
	{
		if (!R.Result.IsChanged()) { return; }
		Steps += R.EventTag == DocSurfaceTags::Event_Footstep ? 1 : 0;
		Landings += R.EventTag == DocSurfaceTags::Event_Landing ? 1 : 0;
	});
	AActor* Walker = F.TW.Spawn<AActor>();
	UDocSurfaceFeedbackComponent* C = NewObject<UDocSurfaceFeedbackComponent>(Walker);
	C->Profile = F.Profile;
	C->bDistanceFootsteps = true;
	C->StrideCm = 100.f;
	C->RegisterComponent();

	C->UpdateMovement(FVector::ZeroVector, true, FVector::ZeroVector);
	C->UpdateMovement(FVector(50, 0, 0), true, FVector::ZeroVector);
	TestEqual(TEXT("Below stride"), Steps, 0);
	C->UpdateMovement(FVector(120, 0, 0), true, FVector::ZeroVector);
	TestEqual(TEXT("One step at stride"), Steps, 1);
	C->UpdateMovement(FVector(5000, 0, 0), true, FVector::ZeroVector);
	TestEqual(TEXT("Teleport emits nothing"), Steps, 1);
	C->UpdateMovement(FVector(5450, 0, 0), true, FVector::ZeroVector);
	TestEqual(TEXT("Hitch emits at most one step"), Steps, 2);
	C->UpdateMovement(FVector(5500, 0, 200), false, FVector(0, 0, -600));
	C->UpdateMovement(FVector(5550, 0, 100), false, FVector(0, 0, -700));
	TestEqual(TEXT("No steps while airborne"), Steps, 2);
	C->UpdateMovement(FVector(5600, 0, 0), true, FVector::ZeroVector);
	C->UpdateMovement(FVector(5610, 0, 0), true, FVector::ZeroVector);
	TestEqual(TEXT("Landing once per transition"), Landings, 1);

	const FDocSurfaceFeedbackResult Notify = C->TriggerTraced(DocSurfaceTags::Event_Footstep, FVector::ZeroVector, 1.f, TEXT("FootL"), /*bFromAnimNotify*/ true);
	TestEqual(TEXT("Notify ignored while distance producer owns footsteps"), Notify.Result.Outcome, EDocResultOutcome::NoChange);
	C->bDistanceFootsteps = false;
	C->TriggerTraced(DocSurfaceTags::Event_Footstep, FVector::ZeroVector, 1.f, TEXT("FootL"), true);
	TestEqual(TEXT("Notify fires when it owns footsteps"), Steps, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceContinuousTest, "Doc.Surface.ImpactAndContinuous", Flags)
bool FDocSurfaceContinuousTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	FDocSurfaceResponseRule Slide = MakeRule(TEXT("Slide"), DocSurfaceTags::Event_Slide);
	Slide.Responses[0].bContinuous = true;
	F.Profile->Rules = { MakeRule(TEXT("Step"), DocSurfaceTags::Event_Footstep), MakeRule(TEXT("Hit"), DocSurfaceTags::Event_Impact), Slide };
	TestEqual(TEXT("Impact rule"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Melee), F.Profile).SelectedRule, FName(TEXT("Hit")));
	TestEqual(TEXT("Footstep rule differs"), F.S->ResolveFeedback(MakeRequest(DocSurfaceTags::Event_Footstep), F.Profile).SelectedRule, FName(TEXT("Step")));

	AActor* Sled = F.TW.Spawn<AActor>();
	FDocSurfaceFeedbackRequest R = MakeRequest(DocSurfaceTags::Event_Slide);
	R.Instigator = Sled;
	const FDocSurfaceFeedbackResult Started = F.S->SubmitFeedback(R, F.Profile);
	const FDocRequestHandle Handle = Started.Responses[0].Handle;
	TestTrue(TEXT("Continuous handle"), Handle.IsSet());
	TestTrue(TEXT("Update"), F.S->UpdateContinuous(Handle, FVector(10, 0, 0), 0.5f).IsSuccess());
	TestTrue(TEXT("Stop"), F.S->StopContinuous(Handle).IsSuccess());
	TestEqual(TEXT("Stop twice is NoChange"), F.S->StopContinuous(Handle).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Executor instance stopped"), F.Sound->Stopped.Num(), 1);

	const FDocSurfaceFeedbackResult Again = F.S->SubmitFeedback(R, F.Profile);
	Sled->Destroy();
	F.S->AdvanceForTesting(0.1f);
	TestEqual(TEXT("Source teardown stops continuous response"), F.S->UpdateContinuous(Again.Responses[0].Handle, FVector::ZeroVector, 1.f).Outcome, EDocResultOutcome::NotFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceBudgetTest, "Doc.Surface.BudgetAndPooling", Flags)
bool FDocSurfaceBudgetTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	UDocSurfaceFeedbackSettings* Settings = GetMutableDefault<UDocSurfaceFeedbackSettings>();
	const int32 SavedSounds = Settings->MaxActiveSounds;
	Settings->MaxActiveSounds = 2;
	F.Profile->Rules = { MakeRule(TEXT("Step"), DocSurfaceTags::Event_Footstep) };

	auto Submit = [&F](FName Source)
	{
		FDocSurfaceFeedbackRequest R = MakeRequest(DocSurfaceTags::Event_Footstep);
		R.SourceId = Source;
		return F.S->SubmitFeedback(R, F.Profile).Responses[0].Admission;
	};
	TestEqual(TEXT("1st"), Submit(TEXT("A")), EDocSurfaceAdmission::Executed);
	TestEqual(TEXT("2nd"), Submit(TEXT("B")), EDocSurfaceAdmission::Executed);
	TestEqual(TEXT("Cap reached"), Submit(TEXT("C")), EDocSurfaceAdmission::BudgetSuppressed);
	F.Sound->FinishAll();
	F.S->AdvanceForTesting(0.1f);
	TestEqual(TEXT("Admitted after completions"), Submit(TEXT("D")), EDocSurfaceAdmission::Executed);
	Settings->MaxActiveSounds = SavedSounds;

	FDocSurfaceResponseRule Cooled = MakeRule(TEXT("Cooled"), DocSurfaceTags::Event_Jump);
	Cooled.CooldownSeconds = 1.f;
	F.Profile->Rules.Add(Cooled);
	FDocSurfaceFeedbackRequest Jump = MakeRequest(DocSurfaceTags::Event_Jump);
	TestEqual(TEXT("First jump"), F.S->SubmitFeedback(Jump, F.Profile).Responses[0].Admission, EDocSurfaceAdmission::Executed);
	TestEqual(TEXT("Cooldown"), F.S->SubmitFeedback(Jump, F.Profile).Responses[0].Admission, EDocSurfaceAdmission::BudgetSuppressed);
	F.S->AdvanceForTesting(1.1f);
	TestEqual(TEXT("After cooldown"), F.S->SubmitFeedback(Jump, F.Profile).Responses[0].Admission, EDocSurfaceAdmission::Executed);

	F.S->SetViewLocationsOverride({ FVector(1.0e6, 0, 0) });
	TestEqual(TEXT("Distance cull"), Submit(TEXT("Far")), EDocSurfaceAdmission::BudgetSuppressed);
	F.S->SetViewLocationsOverride({ FVector::ZeroVector });

	// Stale queued request (TTL).
	TArray<TFunction<void()>> Loads;
	F.S->SetLoaderForTesting([&Loads](const TArray<FSoftObjectPath>&, TFunction<void()> Done) { Loads.Add(MoveTemp(Done)); });
	FDocSurfaceResponseRule Late = MakeRule(TEXT("Late"), DocSurfaceTags::Event_Break);
	Late.Responses[0].Variations = { TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/DocSurfaceTest/Slow.Slow"))) };
	F.Profile->Rules.Add(Late);
	EDocSurfaceAdmission LastAdmission = EDocSurfaceAdmission::NotDispatched;
	F.S->OnFeedbackDispatchedNative.AddLambda([&LastAdmission](const FDocSurfaceFeedbackResult& R) { if (R.Responses.Num()) { LastAdmission = R.Responses[0].Admission; } });
	TestEqual(TEXT("Pending while loading"), F.S->SubmitFeedback(MakeRequest(DocSurfaceTags::Event_Break), F.Profile).Responses[0].Admission, EDocSurfaceAdmission::Pending);
	F.S->AdvanceForTesting(1.f);
	TestEqual(TEXT("Stale request cancelled, not played late"), LastAdmission, EDocSurfaceAdmission::Cancelled);
	if (Loads.Num()) { Loads[0](); }
	TestEqual(TEXT("Queue empty"), F.S->GetQueuedCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceOwnerLossTest, "Doc.Surface.AsyncOwnerLoss", Flags)
bool FDocSurfaceOwnerLossTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	TArray<TFunction<void()>> Loads;
	F.S->SetLoaderForTesting([&Loads](const TArray<FSoftObjectPath>&, TFunction<void()> Done) { Loads.Add(MoveTemp(Done)); });
	FDocSurfaceResponseRule Late = MakeRule(TEXT("Late"), DocSurfaceTags::Event_Impact);
	Late.Responses[0].Variations = { TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/DocSurfaceTest/Slow.Slow"))) };
	F.Profile->Rules = { Late };
	AActor* Shooter = F.TW.Spawn<AActor>();
	FDocSurfaceFeedbackRequest R = MakeRequest(DocSurfaceTags::Event_Impact);
	R.Instigator = Shooter;
	TestEqual(TEXT("Pending"), F.S->SubmitFeedback(R, F.Profile).Responses[0].Admission, EDocSurfaceAdmission::Pending);
	Shooter->Destroy();
	const int32 Before = F.Sound->Executed.Num();
	if (Loads.Num()) { Loads[0](); }
	TestEqual(TEXT("Late asset callback spawns nothing after owner loss"), F.Sound->Executed.Num(), Before);
	TestEqual(TEXT("Queue cleared"), F.S->GetQueuedCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceNetworkTest, "Doc.Surface.NetworkDedupe", Flags)
bool FDocSurfaceNetworkTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	FDocSurfaceResponseRule Hit = MakeRule(TEXT("Hit"), DocSurfaceTags::Event_Impact);
	FDocSurfaceResponse Damage;
	Damage.Kind = EDocSurfaceResponseKind::GameplayEvent;
	Damage.PayloadTag = TAG_Damage;
	Hit.Responses.Add(Damage);
	F.Profile->Rules = { Hit };

	FDocSurfaceFeedbackRequest Predicted = MakeRequest(DocSurfaceTags::Event_Impact);
	Predicted.CorrelationId = 42;
	TestTrue(TEXT("Predicted plays"), F.S->SubmitFeedback(Predicted, F.Profile).Result.IsChanged());
	TestEqual(TEXT("Confirmed copy deduplicated"), F.S->SubmitFeedback(Predicted, F.Profile).Result.Outcome, EDocResultOutcome::NoChange);

	FDocSurfaceFeedbackRequest Forged = MakeRequest(DocSurfaceTags::Event_Impact);
	TestEqual(TEXT("Cosmetic request cannot apply gameplay"), F.S->SubmitFeedback(Forged, F.Profile).Responses[1].Admission, EDocSurfaceAdmission::PermissionDenied);
	Forged.bAuthoritativeIntent = true;
	TestEqual(TEXT("No provider: unsupported"), F.S->SubmitFeedback(Forged, F.Profile).Responses[1].Admission, EDocSurfaceAdmission::Unsupported);
	TSharedPtr<FGameplayProvider> Provider = MakeShared<FGameplayProvider>();
	F.S->SetGameplayProvider(Provider);
	TestEqual(TEXT("Authorized path executes"), F.S->SubmitFeedback(Forged, F.Profile).Responses[1].Admission, EDocSurfaceAdmission::Executed);
	TestEqual(TEXT("Provider applied once"), Provider->Applied, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocSurfaceServerTest, "Doc.Surface.DedicatedServer", Flags)
bool FDocSurfaceServerTest::RunTest(const FString& Parameters)
{
	FFixture F;
	if (!TestNotNull(TEXT("Subsystem"), F.S)) { return false; }
	FDocSurfaceResponseRule Hit = MakeRule(TEXT("Hit"), DocSurfaceTags::Event_Impact);
	FDocSurfaceResponse Damage;
	Damage.Kind = EDocSurfaceResponseKind::GameplayTag;
	Hit.Responses.Add(Damage);
	F.Profile->Rules = { Hit };
	TSharedPtr<FGameplayProvider> Provider = MakeShared<FGameplayProvider>();
	F.S->SetGameplayProvider(Provider);
	F.S->SetCosmeticsDisabledForTesting(true); // same path as NM_DedicatedServer
	FDocSurfaceFeedbackRequest R = MakeRequest(DocSurfaceTags::Event_Impact);
	R.bAuthoritativeIntent = true;
	const FDocSurfaceFeedbackResult Result = F.S->SubmitFeedback(R, F.Profile);
	TestEqual(TEXT("No cosmetic on server"), Result.Responses[0].Admission, EDocSurfaceAdmission::Unsupported);
	TestEqual(TEXT("Nothing played"), F.Sound->Executed.Num(), 0);
	TestEqual(TEXT("Authoritative response still applied"), Result.Responses[1].Admission, EDocSurfaceAdmission::Executed);
	TestTrue(TEXT("Resolver usable"), F.S->ResolveFeedback(R, F.Profile).Result.IsSuccess());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
