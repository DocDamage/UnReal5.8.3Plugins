#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/StrongObjectPtr.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Math/RandomStream.h"
#include "Algo/Reverse.h"
#include "DocGestureTypes.h"
#include "DocGestureTemplate.h"
#include "DocGestureSubsystem.h"
#include <limits>

namespace DocGestureTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	TArray<FVector2D> Check() { return { FVector2D(0, 50), FVector2D(30, 100), FVector2D(100, 0) }; }
	TArray<FVector2D> Zed() { return { FVector2D(0, 0), FVector2D(100, 0), FVector2D(0, 100), FVector2D(100, 100) }; }
	TArray<FVector2D> ArrowRight() { return { FVector2D(0, 50), FVector2D(100, 50), FVector2D(75, 25) }; }
	TArray<FVector2D> BoxPath(double W, double H) { return { FVector2D(0, 0), FVector2D(W, 0), FVector2D(W, H), FVector2D(0, H), FVector2D(0, 0) }; }

	TArray<FVector2D> Circle(double StartRadians = 0.0, bool bClockwise = false)
	{
		TArray<FVector2D> Out;
		for (int32 i = 0; i <= 32; ++i)
		{
			const double A = StartRadians + (bClockwise ? -1.0 : 1.0) * 2.0 * UE_DOUBLE_PI * i / 32.0;
			Out.Add(FVector2D(50.0 + 50.0 * FMath::Cos(A), 50.0 + 50.0 * FMath::Sin(A)));
		}
		return Out;
	}

	TArray<FVector2D> Reversed(TArray<FVector2D> P)
	{
		Algo::Reverse(P);
		return P;
	}

	/** Rotates about (50, 50) by 90 degrees. */
	TArray<FVector2D> Rotated90(const TArray<FVector2D>& P)
	{
		TArray<FVector2D> Out;
		for (const FVector2D& Q : P)
		{
			Out.Add(FVector2D(50.0 + (Q.Y - 50.0), 50.0 - (Q.X - 50.0)));
		}
		return Out;
	}

	UDocGestureTemplate* Template(FName Id, const TArray<FVector2D>& Points, bool bRotation = false, bool bDirection = false,
		EDocGestureScalePolicy Scale = EDocGestureScalePolicy::UniformPreserveAspect, float Threshold = 0.70f)
	{
		UDocGestureTemplate* T = NewObject<UDocGestureTemplate>(GetTransientPackage());
		T->GestureId = Id;
		T->DisplayName = FText::FromName(Id);
		T->CanonicalPoints = Points;
		T->bRotationInvariant = bRotation;
		T->bDirectionInvariant = bDirection;
		T->ScalePolicy = Scale;
		T->MinSimilarityThreshold = Threshold;
		T->MinRunnerUpMargin = 0.10f;
		T->MinPathLength = 10.0f;
		return T;
	}

	UDocGestureTemplateSet* Set(FName Id, const TArray<UDocGestureTemplate*>& Templates)
	{
		UDocGestureTemplateSet* S = NewObject<UDocGestureTemplateSet>(GetTransientPackage());
		S->SetId = Id;
		for (UDocGestureTemplate* T : Templates)
		{
			S->AddTemplate(T);
		}
		return S;
	}

	UDocGestureTemplateSet* BaseSet()
	{
		return Set(TEXT("Set.Base"), { Template(TEXT("Check"), Check()), Template(TEXT("Circle"), Circle(), true, true), Template(TEXT("Z"), Zed()) });
	}

	struct FFixture
	{
		TStrongObjectPtr<ULocalPlayer> Player;
		TStrongObjectPtr<UDocGestureSubsystem> Subsystem;
		FDocOwnerScope Owner;

		explicit FFixture(UDocGestureTemplateSet* InSet = nullptr)
			: Owner(EDocOwnerScopeKind::PlayerProfile, FGuid::NewGuid())
		{
			Player.Reset(NewObject<ULocalPlayer>(GEngine));
			Subsystem.Reset(NewObject<UDocGestureSubsystem>(Player.Get()));
			if (InSet)
			{
				Subsystem->RegisterTemplateSet(InSet);
			}
		}

		UDocGestureSubsystem* operator->() const { return Subsystem.Get(); }

		FDocGestureStroke Draw(const TArray<FVector2D>& Points, EDocGestureInputSource Source = EDocGestureInputSource::Mouse)
		{
			Subsystem->BeginStroke(Owner, Source);
			Subsystem->AppendPoints(Points, 0.1f);
			FDocGestureStroke Stroke;
			Subsystem->EndStroke(Stroke);
			return Stroke;
		}

		FDocGestureRecognitionResult Recognize(const TArray<FVector2D>& Points)
		{
			return Subsystem->RecognizeStroke(Draw(Points));
		}

		/** A stroke not captured through BeginStroke, stamped with the current generation. */
		FDocGestureStroke Raw(const TArray<FVector2D>& Points) const
		{
			FDocGestureStroke S;
			S.Owner = Owner;
			S.Generation = Subsystem->GetCurrentGeneration();
			S.Points = Points;
			return S;
		}
	};
}

// GES-01
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureResampleDegenerateTest, FAutomationTestBase, "Doc.Gesture.ResampleDegenerate", DocGestureTests::Flags)
bool FDocGestureResampleDegenerateTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	TArray<FVector2D> Out;
	TestFalse(TEXT("Single point"), UDocGestureTemplate::ResamplePoints({ FVector2D(0, 0) }, 32, Out));
	TestFalse(TEXT("Only duplicates"), UDocGestureTemplate::ResamplePoints({ FVector2D(5, 5), FVector2D(5, 5), FVector2D(5, 5) }, 32, Out));

	TArray<FVector2D> Stationary;
	for (int32 i = 0; i < 50; ++i)
	{
		Stationary.Add(FVector2D(0, 0));
	}
	Stationary.Add(FVector2D(100, 0));
	TestTrue(TEXT("Stationary repeats resample safely"), UDocGestureTemplate::ResamplePoints(Stationary, 32, Out));
	TestEqual(TEXT("Exact count"), Out.Num(), 32);
	bool bFinite = true;
	for (const FVector2D& P : Out)
	{
		bFinite &= FMath::IsFinite(P.X) && FMath::IsFinite(P.Y);
	}
	TestTrue(TEXT("All finite"), bFinite);
	TestTrue(TEXT("Ends at the last point"), Out.Last().Equals(FVector2D(100, 0), 1e-6));
	TestTrue(TEXT("Non-finite points are dropped by the resampler"), UDocGestureTemplate::ResamplePoints({ FVector2D(0, 0), FVector2D(NaN, 1), FVector2D(100, 0) }, 16, Out));

	FFixture F(BaseSet());
	TestEqual(TEXT("Zero-length stroke"), F->RecognizeStroke(F.Raw({ FVector2D(50, 50), FVector2D(50, 50) })).Status, EDocGestureRecognitionStatus::InvalidStroke);
	TestEqual(TEXT("NaN stroke"), F->RecognizeStroke(F.Raw({ FVector2D(0, 0), FVector2D(NaN, 100) })).Status, EDocGestureRecognitionStatus::InvalidStroke);
	TestTrue(TEXT("Begin"), F->BeginStroke(F.Owner).IsSuccess());
	TestEqual(TEXT("Capture refuses NaN points"), F->AppendPoints({ FVector2D(NaN, 0) }).Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Stroke survives the refused point"), F->IsStrokeActive());
	TestEqual(TEXT("Negative time refused"), F->AppendPoints({ FVector2D(1, 1) }, -1.0f).Outcome, EDocResultOutcome::InvalidInput);

	UDocGestureTemplate* Flat = Template(TEXT("Flat"), { FVector2D(3, 3), FVector2D(3, 3) });
	TestEqual(TEXT("Zero-length template rejected"), Flat->ValidateTemplate().Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Set with it rejected"), F->RegisterTemplateSet(Set(TEXT("Set.Flat"), { Flat })).Outcome, EDocResultOutcome::InvalidConfiguration);
	return true;
}

// GES-02
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureScaleAndAspectTest, FAutomationTestBase, "Doc.Gesture.ScaleAndAspect", DocGestureTests::Flags)
bool FDocGestureScaleAndAspectTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	FFixture Uniform(Set(TEXT("Set.Uniform"), { Template(TEXT("Square"), BoxPath(100, 100)), Template(TEXT("Wide"), BoxPath(300, 60)) }));
	FDocGestureRecognitionResult R = Uniform.Recognize(BoxPath(150, 30));
	TestEqual(TEXT("Smaller wide box is Wide"), R.MatchedGestureId, FName(TEXT("Wide")));
	R = Uniform.Recognize(BoxPath(300, 300));
	TestEqual(TEXT("Larger square is Square (scale invariant)"), R.MatchedGestureId, FName(TEXT("Square")));

	// A 1.5:1 box: uniform keeps it distinct from a square; opt-in non-uniform stretching accepts it.
	FFixture Strict(Set(TEXT("Set.Strict"), { Template(TEXT("Square"), BoxPath(100, 100), false, false, EDocGestureScalePolicy::UniformPreserveAspect, 0.75f) }));
	TestEqual(TEXT("Uniform rejects a stretched square"), Strict.Recognize(BoxPath(150, 100)).Status, EDocGestureRecognitionStatus::NotRecognized);
	FFixture Loose(Set(TEXT("Set.Loose"), { Template(TEXT("Box"), BoxPath(100, 100), false, false, EDocGestureScalePolicy::NonUniformFitBox, 0.75f) }));
	const FDocGestureRecognitionResult Stretched = Loose.Recognize(BoxPath(150, 100));
	TestEqual(TEXT("Non-uniform accepts it"), Stretched.Status, EDocGestureRecognitionStatus::Recognized);

	TArray<FVector2D> Line = { FVector2D(0, 0), FVector2D(100, 1) };
	TArray<FVector2D> Normalized;
	FDocGestureCompiledTemplate Settings;
	Settings.ResampleCount = 32;
	Settings.ScalePolicy = EDocGestureScalePolicy::NonUniformFitBox;
	TestTrue(TEXT("Near-1D stroke normalizes"), FDocGestureRecognizer::Normalize(Line, Settings, false, Normalized));
	double MaxAbs = 0.0;
	for (const FVector2D& P : Normalized)
	{
		MaxAbs = FMath::Max(MaxAbs, FMath::Max(FMath::Abs(P.X), FMath::Abs(P.Y)));
	}
	TestTrue(TEXT("Near-1D stroke is not blown up by non-uniform scaling"), MaxAbs <= 0.51);
	return true;
}

// GES-03
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureRotationPolicyTest, FAutomationTestBase, "Doc.Gesture.RotationPolicy", DocGestureTests::Flags)
bool FDocGestureRotationPolicyTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	FFixture F(Set(TEXT("Set.Dir"), { Template(TEXT("ArrowRight"), ArrowRight()), Template(TEXT("Circle"), Circle(), true, true) }));
	TestEqual(TEXT("Arrow right recognized"), F.Recognize(ArrowRight()).MatchedGestureId, FName(TEXT("ArrowRight")));
	const FDocGestureRecognitionResult Up = F.Recognize(Rotated90(ArrowRight()));
	TestTrue(TEXT("Directional template rejects the rotated arrow"), Up.MatchedGestureId != FName(TEXT("ArrowRight")));
	TestTrue(TEXT("Nothing accepted"), Up.Status != EDocGestureRecognitionStatus::Recognized);
	TestEqual(TEXT("Rotation-invariant circle accepts any start angle"), F.Recognize(Circle(UE_DOUBLE_PI / 2.0)).MatchedGestureId, FName(TEXT("Circle")));

	FFixture Any(Set(TEXT("Set.Any"), { Template(TEXT("ArrowAny"), ArrowRight(), /*bRotation*/ true) }));
	TestEqual(TEXT("Opt-in rotation invariance accepts it"), Any.Recognize(Rotated90(ArrowRight())).MatchedGestureId, FName(TEXT("ArrowAny")));
	return true;
}

// GES-04
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureStrokeDirectionTest, FAutomationTestBase, "Doc.Gesture.StrokeDirection", DocGestureTests::Flags)
bool FDocGestureStrokeDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	FFixture F(BaseSet());
	TestEqual(TEXT("Check forward"), F.Recognize(Check()).MatchedGestureId, FName(TEXT("Check")));
	const FDocGestureRecognitionResult Back = F.Recognize(Reversed(Check()));
	TestTrue(TEXT("Reversed check refused by a directional template"), Back.MatchedGestureId != FName(TEXT("Check")));
	const FDocGestureRecognitionResult Clockwise = F.Recognize(Circle(0.0, /*bClockwise*/ true));
	TestEqual(TEXT("Direction-invariant circle accepts clockwise"), Clockwise.MatchedGestureId, FName(TEXT("Circle")));
	TestTrue(TEXT("Reported as matched reversed"), Clockwise.Candidates.Num() > 0 && Clockwise.Candidates[0].bMatchedReversed);

	FFixture Either(Set(TEXT("Set.Either"), { Template(TEXT("CheckAnyDir"), Check(), false, /*bDirection*/ true) }));
	TestEqual(TEXT("Opt-in reversal"), Either.Recognize(Reversed(Check())).MatchedGestureId, FName(TEXT("CheckAnyDir")));
	return true;
}

// GES-05
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureRejectionMarginTest, FAutomationTestBase, "Doc.Gesture.RejectionMargin", DocGestureTests::Flags)
bool FDocGestureRejectionMarginTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	FFixture F(BaseSet());
	FRandomStream Rng(7);
	TArray<FVector2D> Scribble;
	for (int32 i = 0; i < 40; ++i)
	{
		Scribble.Add(FVector2D(Rng.FRandRange(0.0f, 100.0f), Rng.FRandRange(0.0f, 100.0f)));
	}
	const FDocGestureRecognitionResult Noise = F.Recognize(Scribble);
	TestEqual(TEXT("Scribble rejected"), Noise.Status, EDocGestureRecognitionStatus::NotRecognized);
	TestTrue(TEXT("A nearest candidate still exists"), Noise.Candidates.Num() == 3);

	TArray<FVector2D> Dense;
	UDocGestureTemplate::ResamplePoints(Check(), 20, Dense);
	FRandomStream Jitter(3);
	for (FVector2D& P : Dense)
	{
		P = P * 1.5 + FVector2D(Jitter.FRandRange(-3.0f, 3.0f), Jitter.FRandRange(-3.0f, 3.0f));
	}
	const FDocGestureRecognitionResult Hand = F.Recognize(Dense);
	TestEqual(TEXT("Jittered, scaled check recognized"), Hand.MatchedGestureId, FName(TEXT("Check")));
	TestTrue(TEXT("Similarity is bounded"), Hand.Similarity > 0.0f && Hand.Similarity <= 1.0f);

	FFixture Close(Set(TEXT("Set.Close"), {
		Template(TEXT("CheckA"), { FVector2D(0, 50), FVector2D(30, 100), FVector2D(100, 0) }),
		Template(TEXT("CheckB"), { FVector2D(0, 55), FVector2D(35, 100), FVector2D(100, 0) }) }));
	const FDocGestureRecognitionResult Near = Close.Recognize({ FVector2D(0, 52), FVector2D(32, 100), FVector2D(100, 0) });
	TestEqual(TEXT("Near-match between two templates is Ambiguous"), Near.Status, EDocGestureRecognitionStatus::Ambiguous);
	TestTrue(TEXT("Margin reported"), Near.RunnerUpMargin < 0.10f);
	TestTrue(TEXT("Authoring collision check flags the pair"), Set(TEXT("Set.Close2"), {
		Template(TEXT("CheckA"), { FVector2D(0, 50), FVector2D(30, 100), FVector2D(100, 0) }),
		Template(TEXT("CheckB"), { FVector2D(0, 55), FVector2D(35, 100), FVector2D(100, 0) }) })->FindCollisions().Num() > 0);
	return true;
}

// GES-06
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureTemplateVersionTest, FAutomationTestBase, "Doc.Gesture.TemplateVersion", DocGestureTests::Flags)
bool FDocGestureTemplateVersionTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	UDocGestureTemplate* Editable = Template(TEXT("Check"), Check());
	UDocGestureTemplateSet* S = Set(TEXT("Set.Versioned"), { Editable, Template(TEXT("Z"), Zed()) });
	FFixture F(S);
	const FString HashV1 = F->GetActiveModelHash();

	TestTrue(TEXT("Begin under v1"), F->BeginStroke(F.Owner).IsSuccess());
	// The author edits the asset mid-capture and re-registers it.
	Editable->CanonicalPoints = Circle();
	S->IncrementVersion();
	TestTrue(TEXT("Register v2"), F->RegisterTemplateSet(S).IsChanged());
	TestTrue(TEXT("Active model changed"), F->GetActiveModelHash() != HashV1);
	F->AppendPoints(Check(), 0.1f);
	FDocGestureStroke Stroke;
	F->EndStroke(Stroke);
	const FDocGestureRecognitionResult R = F->RecognizeStroke(Stroke);
	TestEqual(TEXT("In-flight stroke keeps the model it began with"), R.ModelHash, HashV1);
	TestEqual(TEXT("Recorded set version"), R.TemplateVersion, 1);
	TestEqual(TEXT("Interpreted with the original check"), R.MatchedGestureId, FName(TEXT("Check")));
	TestEqual(TEXT("A result from a replaced model is not dispatched"), F->DispatchResult(R).Outcome, EDocResultOutcome::Conflict);

	const FDocGestureRecognitionResult After = F.Recognize(Check());
	TestEqual(TEXT("New strokes use v2"), After.TemplateVersion, 2);
	TestTrue(TEXT("The old check no longer matches the edited template"), After.MatchedGestureId != FName(TEXT("Check")));
	TestEqual(TEXT("Re-registering identical content is NoChange"), F->RegisterTemplateSet(S).Outcome, EDocResultOutcome::NoChange);
	return true;
}

// GES-07
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureCancelGenerationTest, FAutomationTestBase, "Doc.Gesture.CancelGeneration", DocGestureTests::Flags)
bool FDocGestureCancelGenerationTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	FFixture F(BaseSet());
	int32 Invocations = 0;
	F->OnGestureDispatched.AddLambda([&Invocations](const FDocGestureRecognitionResult&) { ++Invocations; });

	const FDocGestureStroke A = F.Draw(Check());
	const FDocGestureRecognitionResult RA = F->RecognizeStroke(A);
	TestEqual(TEXT("A recognized"), RA.Status, EDocGestureRecognitionStatus::Recognized);
	TestTrue(TEXT("Player starts another gesture"), F->BeginStroke(F.Owner).IsSuccess());
	TestEqual(TEXT("Old result cannot invoke the consumer"), F->DispatchResult(RA).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Re-recognizing the old stroke is Cancelled"), F->RecognizeStroke(A).Status, EDocGestureRecognitionStatus::Cancelled);
	TestTrue(TEXT("Cancel"), F->CancelStroke().IsSuccess());
	FDocGestureStroke Nothing;
	TestEqual(TEXT("Nothing to end after cancel"), F->EndStroke(Nothing).Outcome, EDocResultOutcome::NotReady);
	TestEqual(TEXT("Consumer untouched"), Invocations, 0);

	const FDocGestureStroke C = F.Draw(Check());
	const FDocGestureRecognitionResult RC = F->RecognizeStroke(C);
	TestTrue(TEXT("Current result dispatches"), F->DispatchResult(RC).IsChanged());
	TestEqual(TEXT("Consumer invoked once"), Invocations, 1);
	TestEqual(TEXT("Repeat dispatch ignored"), F->DispatchResult(RC).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("A second recognition of the consumed stroke is stale"), F->RecognizeStroke(C).Status, EDocGestureRecognitionStatus::Cancelled);
	TestEqual(TEXT("Still once"), Invocations, 1);

	F->BeginStroke(F.Owner);
	F->AppendPoints({ FVector2D(0, 0), FVector2D(10, 10) });
	F->NotifyInputDeviceChanged();
	TestFalse(TEXT("Device change cancels capture"), F->IsStrokeActive());
	return true;
}

// GES-08
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureInputBoundsTest, FAutomationTestBase, "Doc.Gesture.InputBounds", DocGestureTests::Flags)
bool FDocGestureInputBoundsTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	FFixture F(BaseSet());
	F->MaxPointsPerStroke = 50;
	TArray<FVector2D> Many;
	for (int32 i = 0; i < 60; ++i)
	{
		Many.Add(FVector2D(i, i % 7));
	}
	F->BeginStroke(F.Owner);
	TestEqual(TEXT("Point limit"), F->AppendPoints(Many).Outcome, EDocResultOutcome::InvalidInput);
	TestFalse(TEXT("Over-long stroke cancelled"), F->IsStrokeActive());
	F->BeginStroke(F.Owner);
	TestEqual(TEXT("Duration limit"), F->AppendPoints({ FVector2D(0, 0), FVector2D(50, 50) }, 11.0f).Outcome, EDocResultOutcome::TimedOut);
	TestFalse(TEXT("Over-long capture cancelled"), F->IsStrokeActive());

	F->MaxPointsPerStroke = 1000000;
	TestEqual(TEXT("Hard cap"), F->GetEffectiveMaxPoints(), UDocGestureSubsystem::HardMaxPointsPerStroke);
	TArray<FVector2D> Huge;
	for (int32 i = 0; i < UDocGestureSubsystem::HardMaxPointsPerStroke + 1; ++i)
	{
		Huge.Add(FVector2D(i, 0));
	}
	TestEqual(TEXT("Oversized stroke is not processed"), F->RecognizeStroke(F.Raw(Huge)).Status, EDocGestureRecognitionStatus::InvalidStroke);

	const FDocGestureRecognitionResult R = F.Recognize(Circle());
	const int32 Bound = 3 * 2 * (1 + 2 + FDocGestureRecognizer::RotationIterations);
	TestTrue(TEXT("Search work is bounded"), R.WorkUnits > 0 && R.WorkUnits <= Bound);

	UDocGestureTemplate* Heavy = Template(TEXT("Heavy"), Check());
	Heavy->ResampleCount = 1000;
	TestEqual(TEXT("Resample count bounded"), Heavy->ValidateTemplate().Outcome, EDocResultOutcome::InvalidConfiguration);
	TArray<UDocGestureTemplate*> TooMany;
	for (int32 i = 0; i <= UDocGestureTemplateSet::MaxTemplates; ++i)
	{
		TooMany.Add(Template(FName(*FString::Printf(TEXT("T%d"), i)), Check()));
	}
	TestEqual(TEXT("Template count bounded"), Set(TEXT("Set.Big"), TooMany)->ValidateSet().Outcome, EDocResultOutcome::InvalidConfiguration);

	// Surface leave policies.
	FDocGestureCaptureSettings Settings;
	Settings.SurfaceSize = FVector2D(200, 100);
	Settings.LeavePolicy = EDocGesturePointerLeavePolicy::Cancel;
	F->SetCaptureSettings(Settings);
	F->BeginStroke(F.Owner);
	TestEqual(TEXT("Leaving the surface cancels"), F->AppendPoints({ FVector2D(10, 10), FVector2D(250, 10) }).Outcome, EDocResultOutcome::Cancelled);
	Settings.LeavePolicy = EDocGesturePointerLeavePolicy::Clamp;
	F->SetCaptureSettings(Settings);
	F->BeginStroke(F.Owner);
	F->AppendPoints({ FVector2D(10, 10), FVector2D(250, 150) });
	TestTrue(TEXT("Clamped to the surface"), F->GetActiveStroke().Points.Last().Equals(FVector2D(200, 100)));
	return true;
}

// GES-09
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureAccessibleAlternativeTest, FAutomationTestBase, "Doc.Gesture.AccessibleAlternative", DocGestureTests::Flags)
bool FDocGestureAccessibleAlternativeTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	UDocGestureTemplate* NoAlt = Template(TEXT("Z"), Zed());
	NoAlt->bAllowAccessibleAlternative = false;
	FFixture F(Set(TEXT("Set.Alt"), { Template(TEXT("Check"), Check()), Template(TEXT("Circle"), Circle(), true, true), NoAlt }));
	TArray<FDocGestureRecognitionResult> Received;
	F->OnGestureDispatched.AddLambda([&Received](const FDocGestureRecognitionResult& R) { Received.Add(R); });

	TestTrue(TEXT("Drawing in progress"), F->BeginStroke(F.Owner).IsSuccess());
	const FDocGestureRecognitionResult Alt = F->SelectAccessibleAlternative(TEXT("Circle"), F.Owner);
	TestEqual(TEXT("Alternative recognized"), Alt.Status, EDocGestureRecognitionStatus::Recognized);
	TestFalse(TEXT("Alternative supersedes the drawing"), F->IsStrokeActive());
	TestTrue(TEXT("Same consumer path"), F->DispatchResult(Alt).IsChanged());

	const FDocGestureRecognitionResult Drawn = F.Recognize(Circle());
	TestTrue(TEXT("Drawn circle dispatches too"), F->DispatchResult(Drawn).IsChanged());
	TestEqual(TEXT("Both reached the consumer"), Received.Num(), 2);
	if (Received.Num() == 2)
	{
		TestEqual(TEXT("Same semantic intent"), Received[0].MatchedGestureId, Received[1].MatchedGestureId);
		TestEqual(TEXT("Alternative is labelled"), Received[0].InputSource, EDocGestureInputSource::AccessibleAlternative);
		TestEqual(TEXT("Drawing is labelled"), Received[1].InputSource, EDocGestureInputSource::Mouse);
	}

	const FDocGestureRecognitionResult Unknown = F->SelectAccessibleAlternative(TEXT("Nope"), F.Owner);
	TestEqual(TEXT("Unknown gesture refused"), Unknown.Status, EDocGestureRecognitionStatus::NotRecognized);
	TestEqual(TEXT("Refused result cannot be dispatched"), F->DispatchResult(Unknown).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Template can opt out"), F->SelectAccessibleAlternative(TEXT("Z"), F.Owner).Status, EDocGestureRecognitionStatus::NotRecognized);
	TestEqual(TEXT("Owner required"), F->SelectAccessibleAlternative(TEXT("Circle"), FDocOwnerScope()).Status, EDocGestureRecognitionStatus::InvalidStroke);
	TestEqual(TEXT("Drawing cannot claim the alternative label"), F->BeginStroke(F.Owner, EDocGestureInputSource::AccessibleAlternative).Outcome, EDocResultOutcome::InvalidInput);
	return true;
}

// GES-10
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocGestureLocalIsolationTest, FAutomationTestBase, "Doc.Gesture.LocalIsolation", DocGestureTests::Flags)
bool FDocGestureLocalIsolationTest::RunTest(const FString& Parameters)
{
	using namespace DocGestureTests;
	UDocGestureTemplate* Shared = Template(TEXT("Check"), Check());
	FFixture P1(Set(TEXT("Set.P1"), { Shared }));
	FFixture P2(Set(TEXT("Set.P2"), { Template(TEXT("Circle"), Circle(), true, true) }));
	int32 P2Invocations = 0;
	P2->OnGestureDispatched.AddLambda([&P2Invocations](const FDocGestureRecognitionResult&) { ++P2Invocations; });

	const FDocGestureRecognitionResult R1 = P1.Recognize(Check());
	TestEqual(TEXT("Player 1 recognizes with its set"), R1.MatchedGestureId, FName(TEXT("Check")));
	const int32 P2Generation = P2->GetCurrentGeneration();
	TestEqual(TEXT("Player 2 does not know player 1's template"), P2.Recognize(Check()).Status, EDocGestureRecognitionStatus::NotRecognized);
	P1->BeginStroke(P1.Owner);
	P1->CancelStroke();
	TestEqual(TEXT("Player 1 activity does not touch player 2's generation"), P2->GetCurrentGeneration(), P2Generation + 1);
	TestEqual(TEXT("Player 1's result cannot be dispatched through player 2"), P2->DispatchResult(R1).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Player 2's consumer untouched"), P2Invocations, 0);
	TestTrue(TEXT("Results carry their owner"), R1.Owner == P1.Owner);

	// Editing the shared asset does not change player 1's registered model.
	const FString Before = P1->GetActiveModelHash();
	Shared->CanonicalPoints = Zed();
	TestEqual(TEXT("Registered model is a private snapshot"), P1->GetActiveModelHash(), Before);
	TestEqual(TEXT("Still recognizes the original"), P1.Recognize(Check()).MatchedGestureId, FName(TEXT("Check")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
