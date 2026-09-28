// DocWeather automation tests (WEA-01..08, 10, 11 base logic). Numerical state only:
// no sky/cloud rendering, audio or network transport is exercised.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocWeatherSubsystem.h"
#include "Curves/CurveFloat.h"
#include "UObject/Package.h"

namespace DocWeatherTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UDocWeatherProfile* MakeProfile(FName Id, float Clouds, float Rain, EDocPrecipitationType Type = EDocPrecipitationType::None, float WindDeg = 0.f)
	{
		UDocWeatherProfile* P = NewObject<UDocWeatherProfile>(GetTransientPackage());
		P->ProfileId = Id;
		P->WeatherTag = DocWeatherTags::Clear;
		P->State.CloudCoverage = Clouds;
		P->State.Precipitation = Rain;
		P->State.PrecipitationType = Rain > 0.f && Type == EDocPrecipitationType::None ? EDocPrecipitationType::Rain : Type;
		P->State.WindSpeed = 5.f;
		P->State.WindDirection = FVector2D(FMath::Cos(FMath::DegreesToRadians(WindDeg)), FMath::Sin(FMath::DegreesToRadians(WindDeg)));
		P->State.Wetness = Rain > 0.f ? 1.f : 0.f;
		P->MinDuration = P->MaxDuration = 10.f;
		return P;
	}

	UDocWeatherTransitionDefinition* MakeTransition(FName Id, float Duration, EDocWeatherCurve Curve = EDocWeatherCurve::Linear)
	{
		UDocWeatherTransitionDefinition* T = NewObject<UDocWeatherTransitionDefinition>(GetTransientPackage());
		T->TransitionId = Id;
		T->Duration = Duration;
		T->Curve = Curve;
		return T;
	}

	UDocWeatherSchedule* MakeSchedule(const TArray<UDocWeatherProfile*>& Profiles, const TArray<float>& Weights, float Hold = 1.f)
	{
		UDocWeatherSchedule* S = NewObject<UDocWeatherSchedule>(GetTransientPackage());
		S->ScheduleId = TEXT("Test");
		S->Mode = EDocWeatherScheduleMode::WeightedRandom;
		for (int32 i = 0; i < Profiles.Num(); ++i)
		{
			FDocWeatherScheduleEntry E;
			E.Profile = Profiles[i];
			E.Weight = Weights[i];
			E.Duration = Hold;
			S->Entries.Add(E);
		}
		return S;
	}

	float WindDegrees(const FDocWeatherState& S) { return FMath::RadiansToDegrees(FMath::Atan2(S.WindDirection.Y, S.WindDirection.X)); }

	class FFakeAdapter final : public IDocWeatherRenderAdapter
	{
	public:
		explicit FFakeAdapter(FName InName) : Name(InName) {}
		virtual FName GetAdapterName() const override { return Name; }
		virtual void ApplyGlobalState(const FDocWeatherState&) override { ++Applies; }
		virtual void ReleaseControls() override { ++Releases; }
		FName Name;
		int32 Applies = 0;
		int32 Releases = 0;
	};
}

using namespace DocWeatherTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherApplyTest, "Doc.Weather.ProfileApply", Flags)
bool FDocWeatherApplyTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
	if (!TestNotNull(TEXT("Subsystem"), W)) { return false; }
	UDocWeatherProfile* Bad = MakeProfile(TEXT("Bad"), 0.5f, 0.f);
	Bad->State.Precipitation = 2.f;
	const FDocWeatherState Before = W->GetGlobalWeatherState();
	int64 Id = 0;
	TestEqual(TEXT("Out-of-range profile rejected"), W->SetWeather(Bad, nullptr, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id).Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Nothing partially applied"), W->GetGlobalWeatherState().CloudCoverage, Before.CloudCoverage);

	UDocWeatherProfile* Rain = MakeProfile(TEXT("Rain"), 0.9f, 0.7f);
	const int64 Rev = W->GetRevision();
	TestTrue(TEXT("Immediate apply"), W->SetWeather(Rain, nullptr, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id).IsSuccess());
	TestEqual(TEXT("Applied"), W->GetGlobalWeatherState().Precipitation, 0.7f);
	TestEqual(TEXT("Current profile"), W->GetCurrentProfileId(), FName(TEXT("Rain")));
	TestTrue(TEXT("Revision advanced"), W->GetRevision() > Rev);
	TestEqual(TEXT("Wetness is not snapped (relaxes over time)"), W->GetGlobalWeatherState().Wetness, 0.f);
	W->AdvanceForTesting(60.f);
	TestTrue(TEXT("Wetness rises gradually"), W->GetGlobalWeatherState().Wetness > 0.f && W->GetGlobalWeatherState().Wetness < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherTransitionTest, "Doc.Weather.Transition", Flags)
bool FDocWeatherTransitionTest::RunTest(const FString& Parameters)
{
	FDocWeatherState From = MakeProfile(TEXT("A"), 0.f, 0.f, EDocPrecipitationType::None, 350.f)->State;
	FDocWeatherState To = MakeProfile(TEXT("B"), 1.f, 0.f, EDocPrecipitationType::None, 10.f)->State;
	const FDocWeatherState Mid = UDocWeatherSubsystem::Blend(From, To, 0.5f, 0.5f);
	TestTrue(TEXT("Linear field"), FMath::IsNearlyEqual(Mid.CloudCoverage, 0.5f));
	TestTrue(TEXT("Wind takes the short way through 0°, not 180°"), FMath::IsNearlyZero(WindDegrees(Mid), 0.01f));

	FDocWeatherState Rain = MakeProfile(TEXT("R"), 1.f, 0.8f, EDocPrecipitationType::Rain)->State;
	FDocWeatherState Snow = MakeProfile(TEXT("S"), 1.f, 0.8f, EDocPrecipitationType::Snow)->State;
	TestEqual(TEXT("Categorical before switch"), UDocWeatherSubsystem::Blend(Rain, Snow, 0.4f, 0.5f).PrecipitationType, EDocPrecipitationType::Rain);
	TestEqual(TEXT("Categorical after switch"), UDocWeatherSubsystem::Blend(Rain, Snow, 0.6f, 0.5f).PrecipitationType, EDocPrecipitationType::Snow);

	FDocScopedTestWorld TW;
	UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
	UDocWeatherProfile* Clear = MakeProfile(TEXT("Clear"), 0.f, 0.f);
	UDocWeatherProfile* Overcast = MakeProfile(TEXT("Overcast"), 1.f, 0.f);
	int64 Id = 0;
	W->SetWeather(Clear, nullptr, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id);
	W->SetWeather(Overcast, MakeTransition(TEXT("Step"), 10.f, EDocWeatherCurve::Step), EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id);
	W->AdvanceForTesting(4.f);
	TestEqual(TEXT("Step: before"), W->GetGlobalWeatherState().CloudCoverage, 0.f);
	W->AdvanceForTesting(2.f);
	TestEqual(TEXT("Step: after"), W->GetGlobalWeatherState().CloudCoverage, 1.f);

	UDocWeatherTransitionDefinition* Curved = MakeTransition(TEXT("Curve"), 10.f, EDocWeatherCurve::CurveAsset);
	TestEqual(TEXT("Missing curve fails cleanly"), W->SetWeather(Clear, Curved, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id).Outcome, EDocResultOutcome::InvalidConfiguration);
	Curved->CurveAsset = NewObject<UCurveFloat>(GetTransientPackage());
	Curved->CurveAsset->FloatCurve.AddKey(0.f, 0.f);
	Curved->CurveAsset->FloatCurve.AddKey(0.5f, 1.2f); // overshoot
	Curved->CurveAsset->FloatCurve.AddKey(1.f, 1.f);
	W->AdvanceForTesting(10.f);
	TestTrue(TEXT("Curve transition starts"), W->SetWeather(Clear, Curved, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id).IsSuccess());
	W->AdvanceForTesting(5.f);
	TestTrue(TEXT("Overshoot clamped"), W->GetGlobalWeatherState().CloudCoverage >= 0.f && W->GetGlobalWeatherState().CloudCoverage <= 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherCompletionTest, "Doc.Weather.TransitionCompletion", Flags)
bool FDocWeatherCompletionTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
	TMap<int64, TArray<EDocWeatherTransitionOutcome>> Terminal;
	W->OnTransitionCompletedNative.AddLambda([&Terminal](int64 Id, EDocWeatherTransitionOutcome O) { Terminal.FindOrAdd(Id).Add(O); });
	UDocWeatherProfile* Clear = MakeProfile(TEXT("Clear"), 0.f, 0.f);
	UDocWeatherProfile* Cloudy = MakeProfile(TEXT("Cloudy"), 1.f, 0.f);
	UDocWeatherProfile* Rain = MakeProfile(TEXT("Rain"), 0.5f, 0.5f);

	int64 Zero = 0;
	W->SetWeather(Clear, MakeTransition(TEXT("Z"), 0.f), EDocWeatherCommandPolicy::ReplaceKeepScheduler, Zero);
	TestEqual(TEXT("Zero duration: one Completed"), Terminal.FindRef(Zero).Num(), 1);

	int64 A = 0, B = 0;
	W->SetWeather(Cloudy, MakeTransition(TEXT("Slow"), 10.f), EDocWeatherCommandPolicy::ReplaceKeepScheduler, A);
	W->AdvanceForTesting(5.f);
	const float MidCoverage = W->GetGlobalWeatherState().CloudCoverage;
	W->SetWeather(Rain, MakeTransition(TEXT("Slow2"), 10.f), EDocWeatherCommandPolicy::ReplaceKeepScheduler, B);
	TestEqual(TEXT("Replaced exactly once"), Terminal.FindRef(A).Num(), 1);
	TestTrue(TEXT("Outcome Replaced"), Terminal.FindRef(A).Num() == 1 && Terminal.FindRef(A)[0] == EDocWeatherTransitionOutcome::Replaced);
	W->AdvanceForTesting(0.001f);
	TestTrue(TEXT("New transition starts from the evaluated snapshot (no jump)"), FMath::IsNearlyEqual(W->GetGlobalWeatherState().CloudCoverage, MidCoverage, 0.01f));

	int64 Rejected = 0;
	TestEqual(TEXT("Reject policy"), W->SetWeather(Clear, nullptr, EDocWeatherCommandPolicy::RejectIfTransitioning, Rejected).Outcome, EDocResultOutcome::Conflict);
	int64 Queued = 0;
	TestTrue(TEXT("Queue policy"), W->SetWeather(Clear, nullptr, EDocWeatherCommandPolicy::QueueBehindCurrent, Queued).IsSuccess());
	W->AdvanceForTesting(11.f);
	TestEqual(TEXT("B completed once"), Terminal.FindRef(B).Num(), 1);
	TestEqual(TEXT("Queued ran after"), W->GetCurrentProfileId(), FName(TEXT("Clear")));

	int64 C = 0;
	W->SetWeather(Rain, MakeTransition(TEXT("Slow3"), 10.f), EDocWeatherCommandPolicy::ReplaceKeepScheduler, C);
	W->AdvanceForTesting(3.f);
	TestTrue(TEXT("Cancel"), W->CancelTransition().IsSuccess());
	TestTrue(TEXT("Cancelled once"), Terminal.FindRef(C).Num() == 1 && Terminal.FindRef(C)[0] == EDocWeatherTransitionOutcome::Cancelled);
	W->AdvanceForTesting(20.f);
	TestEqual(TEXT("No late completion"), Terminal.FindRef(C).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherRandomTest, "Doc.Weather.RandomSelection", Flags)
bool FDocWeatherRandomTest::RunTest(const FString& Parameters)
{
	UDocWeatherProfile* P0 = MakeProfile(TEXT("P0"), 0.1f, 0.f);
	UDocWeatherProfile* P1 = MakeProfile(TEXT("P1"), 0.5f, 0.f);
	UDocWeatherProfile* P2 = MakeProfile(TEXT("P2"), 0.9f, 0.f);
	TestEqual(TEXT("All-zero weights rejected"), MakeSchedule({ P0, P1 }, { 0.f, 0.f })->ValidateSchedule().Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Negative weight rejected"), MakeSchedule({ P0, P1 }, { 1.f, -1.f })->ValidateSchedule().Outcome, EDocResultOutcome::InvalidConfiguration);

	UDocWeatherSchedule* Schedule = MakeSchedule({ P0, P1, P2 }, { 1.f, 0.f, 3.f });
	FDocScopedTestWorld TW;
	UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
	TestTrue(TEXT("Schedule"), W->SetSchedule(Schedule, 1234).IsSuccess());
	TArray<FName> First;
	FDocWeatherSaveData Mid;
	for (int32 i = 0; i < 12; ++i)
	{
		W->AdvanceForTesting(1.01f);
		First.Add(W->GetCurrentProfileId());
		if (i == 4) { Mid = W->CaptureState(); }
	}
	TestFalse(TEXT("Zero-weight entry never selected"), First.Contains(FName(TEXT("P1"))));

	FDocScopedTestWorld TW2;
	UDocWeatherSubsystem* W2 = TW2.GetSubsystem<UDocWeatherSubsystem>();
	W2->SetSchedule(Schedule, 99); // different seed; restore must replace the stream state
	TestTrue(TEXT("Restore"), W2->RestoreState(Mid).IsSuccess());
	bool bSame = true;
	for (int32 i = 5; i < 12; ++i)
	{
		W2->AdvanceForTesting(1.01f);
		bSame &= W2->GetCurrentProfileId() == First[i];
	}
	TestTrue(TEXT("Restored RNG continues the same sequence"), bSame);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherRegionalTest, "Doc.Weather.RegionalOverride", Flags)
bool FDocWeatherRegionalTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
	int64 Id = 0;
	W->SetWeather(MakeProfile(TEXT("Storm"), 1.f, 1.f), nullptr, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id);

	auto Spawn = [&TW](FName RegionId, const FVector& At, int32 Priority, float Fog, bool bInterior)
	{
		AActor* Actor = TW.Spawn<AActor>(At);
		UDocWeatherRegionOverrideComponent* O = NewObject<UDocWeatherRegionOverrideComponent>(Actor);
		O->RegionId = RegionId;
		O->Priority = Priority;
		O->Extent = FVector(500.f);
		O->BlendDistance = 500.f;
		O->Fields.bFog = true;
		O->Values.FogDensity = Fog;
		O->bInterior = bInterior;
		Actor->SetRootComponent(O);
		O->RegisterComponent();
		Actor->SetActorLocation(At);
		if (!Actor->HasActorBegunPlay())
		{
			Actor->DispatchBeginPlay();
		}
		return O;
	};
	UDocWeatherRegionOverrideComponent* House = Spawn(TEXT("House"), FVector(0, 0, 0), 0, 0.f, true);
	UDocWeatherRegionOverrideComponent* Valley = Spawn(TEXT("Valley"), FVector(5000, 0, 0), 0, 4.f, false);
	UDocWeatherRegionOverrideComponent* Peak = Spawn(TEXT("Peak"), FVector(5000, 0, 0), 5, 8.f, false);

	const FDocWeatherState Inside = W->SampleWeatherAtLocation(FVector(0, 0, 0));
	const FDocWeatherState Outside = W->SampleWeatherAtLocation(FVector(20000, 0, 0));
	TestEqual(TEXT("Interior suppresses local precipitation"), Inside.Precipitation, 0.f);
	TestEqual(TEXT("Outdoors keeps the storm"), Outside.Precipitation, 1.f);
	TestEqual(TEXT("Global state untouched"), W->GetGlobalWeatherState().Precipitation, 1.f);
	TestEqual(TEXT("Highest priority band wins"), W->SampleWeatherAtLocation(FVector(5000, 0, 0)).FogDensity, 8.f);
	const float Edge = W->SampleWeatherAtLocation(FVector(750, 0, 0)).Precipitation;
	TestTrue(TEXT("Blend distance"), Edge > 0.f && Edge < 1.f);

	Peak->GetOwner()->Destroy();
	TestEqual(TEXT("Removal recomputes from remaining sources"), W->SampleWeatherAtLocation(FVector(5000, 0, 0)).FogDensity, 4.f);
	Valley->GetOwner()->Destroy();
	House->GetOwner()->Destroy();
	TestEqual(TEXT("No overrides: global"), W->SampleWeatherAtLocation(FVector(0, 0, 0)).Precipitation, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherSaveTest, "Doc.Weather.SaveRestore", Flags)
bool FDocWeatherSaveTest::RunTest(const FString& Parameters)
{
	UDocWeatherProfile* Clear = MakeProfile(TEXT("Clear"), 0.f, 0.f);
	UDocWeatherProfile* Rain = MakeProfile(TEXT("Rain"), 1.f, 0.8f);
	UDocWeatherTransitionDefinition* Slow = MakeTransition(TEXT("Slow"), 20.f);
	FDocScopedTestWorld TW;
	UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
	int64 Id = 0;
	W->SetWeather(Clear, nullptr, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id);
	W->SetWeather(Rain, Slow, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id);
	W->AdvanceForTesting(6.f);
	const FDocWeatherSaveData Saved = W->CaptureState();

	FDocScopedTestWorld TW2;
	UDocWeatherSubsystem* W2 = TW2.GetSubsystem<UDocWeatherSubsystem>();
	TestEqual(TEXT("Unregistered ids rejected"), W2->RestoreState(Saved).Outcome, EDocResultOutcome::NotFound);
	W2->RegisterProfile(Clear);
	W2->RegisterProfile(Rain);
	W2->RegisterTransition(Slow);
	TestTrue(TEXT("Restore"), W2->RestoreState(Saved).IsSuccess());
	TestTrue(TEXT("Interrupted transition restored"), W2->IsTransitioning());
	W->AdvanceForTesting(4.f);
	W2->AdvanceForTesting(4.f);
	TestTrue(TEXT("Continues identically"), FMath::IsNearlyEqual(W->GetGlobalWeatherState().CloudCoverage, W2->GetGlobalWeatherState().CloudCoverage, 1.e-4f));
	TestTrue(TEXT("Wetness identical"), FMath::IsNearlyEqual(W->GetGlobalWeatherState().Wetness, W2->GetGlobalWeatherState().Wetness, 1.e-4f));
	FDocWeatherSaveData Future = Saved;
	Future.Version = 99;
	TestEqual(TEXT("Newer version rejected"), W2->RestoreState(Future).Outcome, EDocResultOutcome::Unsupported);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherClockTest, "Doc.Weather.ClockDiscontinuity", Flags)
bool FDocWeatherClockTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
	UDocWeatherProfile* Storm = MakeProfile(TEXT("Storm"), 1.f, 1.f);
	Storm->State.LightningPerMinute = 60.f;
	UDocWeatherProfile* Calm = MakeProfile(TEXT("Calm"), 0.f, 0.f);
	W->SetSchedule(MakeSchedule({ Storm, Calm }, { 1.f, 1.f }, 5.f), 7);
	W->AdvanceForTesting(0.1f);
	const double Before = W->GetClock();
	W->AdvanceForTesting(0.f);
	TestEqual(TEXT("Paused: no advance"), W->GetClock(), Before);
	TestEqual(TEXT("Backward jump rejected"), W->AdvanceClock(-5.0).Outcome, EDocResultOutcome::Unsupported);

	int32 Started = 0, Strikes = 0;
	W->OnLightningNative.AddLambda([&Strikes](const FDocLightningEvent&) { ++Strikes; });
	W->OnTransitionCompletedNative.AddLambda([&Started](int64, EDocWeatherTransitionOutcome) { ++Started; });
	const double Start = FPlatformTime::Seconds();
	TestTrue(TEXT("Large jump"), W->AdvanceClock(1.0e7).IsSuccess());
	TestTrue(TEXT("Bounded selections"), Started <= GetDefault<UDocWeatherSettings>()->MaxCatchUpSelections + 1);
	TestEqual(TEXT("No lightning replay"), Strikes, 0);
	TestTrue(TEXT("Bounded work"), FPlatformTime::Seconds() - Start < 1.0);
	TestTrue(TEXT("Clock advanced"), W->GetClock() >= Before + 1.0e7 - 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherRateTest, "Doc.Weather.RateIndependence", Flags)
bool FDocWeatherRateTest::RunTest(const FString& Parameters)
{
	auto Run = [](float Step)
	{
		FDocScopedTestWorld TW;
		UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
		UDocWeatherProfile* Storm = MakeProfile(TEXT("Storm"), 1.f, 1.f);
		Storm->State.LightningPerMinute = 30.f;
		int64 Id = 0;
		W->SetWeather(Storm, nullptr, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id);
		int32 Strikes = 0;
		W->OnLightningNative.AddLambda([&Strikes](const FDocLightningEvent&) { ++Strikes; });
		const int32 Steps = FMath::RoundToInt(600.f / Step);
		for (int32 i = 0; i < Steps; ++i) { W->AdvanceForTesting(Step); }
		return Strikes;
	};
	const int32 Fast = Run(1.f / 60.f);
	const int32 Slow = Run(0.5f);
	TestTrue(TEXT("Expected rate (~300 in 10 min)"), Fast > 220 && Fast < 380);
	TestTrue(TEXT("Cadence-independent (same stream)"), FMath::Abs(Fast - Slow) <= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocWeatherAdapterTest, "Doc.Weather.AdapterLifecycle", Flags)
bool FDocWeatherAdapterTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW; // no players, no renderer: WEA-11 core path
	UDocWeatherSubsystem* W = TW.GetSubsystem<UDocWeatherSubsystem>();
	TSharedPtr<FFakeAdapter> Sky = MakeShared<FFakeAdapter>(TEXT("Sky"));
	TSharedPtr<FFakeAdapter> Fx = MakeShared<FFakeAdapter>(TEXT("Fx"));
	W->RegisterAdapter(Sky);
	W->RegisterAdapter(Fx);
	int64 Id = 0;
	W->SetWeather(MakeProfile(TEXT("Rain"), 1.f, 1.f), nullptr, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id);
	TestTrue(TEXT("Both receive state"), Sky->Applies >= 2 && Fx->Applies >= 2);
	W->UnregisterAdapter(Sky);
	TestEqual(TEXT("Removed adapter released its own controls"), Sky->Releases, 1);
	TestEqual(TEXT("Other adapter untouched"), Fx->Releases, 0);
	const int32 SkyApplies = Sky->Applies;
	W->SetWeather(MakeProfile(TEXT("Clear"), 0.f, 0.f), nullptr, EDocWeatherCommandPolicy::ReplaceKeepScheduler, Id);
	TestEqual(TEXT("Removed adapter no longer driven"), Sky->Applies, SkyApplies);
	TestEqual(TEXT("Provider mode without provider"), W->SetSchedule([]{ UDocWeatherSchedule* S = NewObject<UDocWeatherSchedule>(); S->Mode = EDocWeatherScheduleMode::Provider; return S; }(), 1).Outcome, EDocResultOutcome::Unavailable);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
