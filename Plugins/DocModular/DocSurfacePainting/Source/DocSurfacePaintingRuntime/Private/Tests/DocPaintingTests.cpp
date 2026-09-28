#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocSurfacePaintingSubsystem.h"
#include "DocPaintableSurfaceComponent.h"
#include "DocPaintDefinitions.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "Misc/Compression.h"
#include <limits>

namespace DocPaintTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UDocPaintSurfaceDefinition* CreateTestDefinition(FName Id = TEXT("Surface_Test"), int32 Dim = 64, const FString& Fingerprint = TEXT("Mesh_v1"))
	{
		UDocPaintSurfaceDefinition* Def = NewObject<UDocPaintSurfaceDefinition>(GetTransientPackage());
		Def->SurfaceId = Id;
		Def->Width = Dim;
		Def->Height = Dim;
		Def->InitialFillValue = 0;
		Def->CoverageThreshold = 128;
		Def->MeshFingerprint = Fingerprint;
		return Def;
	}

	UDocPaintableSurfaceComponent* SpawnSurface(FDocScopedTestWorld& TW, UDocPaintSurfaceDefinition* Def, FName SurfaceId = NAME_None)
	{
		AActor* Actor = TW.Spawn<AActor>();
		UDocPaintableSurfaceComponent* Comp = NewObject<UDocPaintableSurfaceComponent>(Actor);
		Comp->SurfaceId = SurfaceId;
		Comp->SurfaceDefinition = Def;
		Comp->RegisterComponent();
		return Comp;
	}

	FDocPaintStroke MakeStroke(EDocPaintOperation Op, const TArray<FVector2D>& Points, float Radius = 0.1f, float Strength = 1.0f, FName Owner = NAME_None)
	{
		FDocPaintStroke S;
		S.StrokeId = FGuid::NewGuid();
		S.Operation = Op;
		S.Points = Points;
		S.RadiusUV = Radius;
		S.Strength = Strength;
		S.OwnerId = Owner;
		return S;
	}

	/** Reference implementation of the declared dab contract, written independently of the rasterizer loop. */
	uint8 ExpectedAlpha(int32 X, int32 Y, int32 W, int32 H, const FVector2D& P, float Radius, float Strength)
	{
		const FVector2D C((X + 0.5) / W, (Y + 0.5) / H);
		const double D = FVector2D::Distance(C, P);
		if (D > Radius)
		{
			return 0;
		}
		const float A = static_cast<float>((1.0 - D / Radius) * Strength);
		return static_cast<uint8>(FMath::Clamp(FMath::FloorToInt32(A * 255.0f + 0.5f), 0, 255));
	}

	TArray<uint8> Snapshot(const UDocPaintableSurfaceComponent* C) { return C->MaskState.RawMaskBytes; }
}

// PNT-01: Doc.Paint.MappingValidation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintMappingValidationTest, FAutomationTestBase, "Doc.Paint.MappingValidation", DocPaintTests::Flags)
bool FDocPaintMappingValidationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Valid definition validates"), DocPaintTests::CreateTestDefinition()->ValidateDefinition().IsSuccess());
	TestEqual(TEXT("Empty ID fails"), DocPaintTests::CreateTestDefinition(NAME_None)->ValidateDefinition().Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Oversized dimension fails"), DocPaintTests::CreateTestDefinition(TEXT("Over"), 2048)->ValidateDefinition().Outcome, EDocResultOutcome::InvalidConfiguration);
	TestEqual(TEXT("Zero dimension fails"), DocPaintTests::CreateTestDefinition(TEXT("Zero"), 0)->ValidateDefinition().Outcome, EDocResultOutcome::InvalidConfiguration);
	UDocPaintSurfaceDefinition* BadMask = DocPaintTests::CreateTestDefinition(TEXT("BadMask"), 16);
	BadMask->EligibleTexelMask.Init(1, 10);
	TestFalse(TEXT("Eligible mask of the wrong size fails"), BadMask->ValidateDefinition().IsSuccess());

	FDocScopedTestWorld TW;

	// Overlapping UVs without explicit acceptance: painting refused, nothing written.
	UDocPaintSurfaceDefinition* Mirrored = DocPaintTests::CreateTestDefinition(TEXT("Mirrored"), 16);
	Mirrored->bHasOverlappingUVs = true;
	UDocPaintableSurfaceComponent* MirroredSurface = DocPaintTests::SpawnSurface(TW, Mirrored);
	TestEqual(TEXT("Mapping status reports overlap"), MirroredSurface->GetMappingStatus(), EDocPaintMappingStatus::OverlappingUVs);
	TestEqual(TEXT("Overlapping UVs refuse painting"),
		MirroredSurface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) })).Outcome, EDocResultOutcome::Unsupported);
	TestEqual(TEXT("Nothing written"), MirroredSurface->QueryCoverage().CoveredTexelCount, 0);
	Mirrored->bAcceptSharedOverlappingUVs = true;
	TestTrue(TEXT("Explicit acceptance allows shared paint"),
		MirroredSurface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) })).IsSuccess());

	// Out-of-range and non-finite UVs never paint (0,0).
	UDocPaintableSurfaceComponent* Surface = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Plain"), 16));
	TestEqual(TEXT("UV outside [0,1] refused"),
		Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(-0.2, 0.5) })).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("NaN UV refused"),
		Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(std::numeric_limits<double>::quiet_NaN(), 0.5) })).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Texel (0,0) untouched"), Surface->GetTexelValue(0, 0), static_cast<uint8>(0));
	TestEqual(TEXT("Nothing painted at all"), Surface->QueryCoverage().CoveredTexelCount, 0);

	// A missing collision UV (no hit) is refused through the coordinate provider.
	UDocCollisionUVCoordinateProvider* Provider = NewObject<UDocCollisionUVCoordinateProvider>();
	FDocPaintStroke Built;
	const FDocSystemResult HitResult = Surface->PaintAtHit(FHitResult(), *Provider, EDocPaintOperation::Paint, 0.1f, 1.0f, NAME_None, Built);
	TestTrue(TEXT("Missing UV is Unsupported or InvalidMapping, never success"),
		HitResult.Outcome == EDocResultOutcome::Unsupported || HitResult.Outcome == EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Texel (0,0) still untouched"), Surface->GetTexelValue(0, 0), static_cast<uint8>(0));

	// Wrap address mode wraps points instead of refusing them.
	UDocPaintSurfaceDefinition* Tiling = DocPaintTests::CreateTestDefinition(TEXT("Tiling"), 16);
	Tiling->AddressMode = EDocPaintUVAddressMode::Wrap;
	UDocPaintableSurfaceComponent* Tiled = DocPaintTests::SpawnSurface(TW, Tiling);
	TestTrue(TEXT("Wrapped UV accepted"), Tiled->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(1.25, 0.5) }, 0.05f)).IsSuccess());
	TestTrue(TEXT("Paint landed at u=0.25"), Tiled->GetTexelValue(3, 7) > 0 || Tiled->GetTexelValue(4, 8) > 0);

	return true;
}

// PNT-02: Doc.Paint.StrokeRasterization
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintStrokeRasterizationTest, FAutomationTestBase, "Doc.Paint.StrokeRasterization", DocPaintTests::Flags)
bool FDocPaintStrokeRasterizationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocPaintableSurfaceComponent* Surface = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Raster"), 16));

	const FVector2D P(0.5, 0.5);
	const float R = 0.125f;
	TestTrue(TEXT("Paint dab"), Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { P }, R, 1.0f)).IsSuccess());
	int32 Mismatches = 0;
	for (int32 Y = 0; Y < 16; ++Y)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			Mismatches += Surface->GetTexelValue(X, Y) != DocPaintTests::ExpectedAlpha(X, Y, 16, 16, P, R, 1.0f) ? 1 : 0;
		}
	}
	TestEqual(TEXT("Paint reproduces the reference mask exactly"), Mismatches, 0);
	TestTrue(TEXT("Centre texel painted"), Surface->GetTexelValue(7, 7) > 0);

	// Clean with half strength: m = min(m, 255 - a).
	TArray<uint8> Before = DocPaintTests::Snapshot(Surface);
	TestTrue(TEXT("Clean dab"), Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Clean, { P }, R, 0.5f)).IsSuccess());
	Mismatches = 0;
	for (int32 Y = 0; Y < 16; ++Y)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			const uint8 Expected = FMath::Min(Before[Y * 16 + X], static_cast<uint8>(255 - DocPaintTests::ExpectedAlpha(X, Y, 16, 16, P, R, 0.5f)));
			Mismatches += Surface->GetTexelValue(X, Y) != Expected ? 1 : 0;
		}
	}
	TestEqual(TEXT("Clean reproduces the reference mask exactly"), Mismatches, 0);

	// Paint is monotonic: repainting a weaker dab never lowers the mask.
	Before = DocPaintTests::Snapshot(Surface);
	Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { P }, R, 0.1f));
	bool bMonotonic = true;
	for (int32 i = 0; i < Before.Num(); ++i)
	{
		bMonotonic &= Surface->MaskState.RawMaskBytes[i] >= Before[i];
	}
	TestTrue(TEXT("Paint never lowers a texel"), bMonotonic);

	// Bounded resampling bridges nearby points but not jumps across a seam.
	UDocPaintSurfaceDefinition* BridgeDef = DocPaintTests::CreateTestDefinition(TEXT("Bridge"), 64);
	BridgeDef->MaxBridgeGapUV = 0.3f;
	UDocPaintableSurfaceComponent* Bridged = DocPaintTests::SpawnSurface(TW, BridgeDef);
	Bridged->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.2, 0.5), FVector2D(0.4, 0.5) }, 0.03f));
	TestTrue(TEXT("Midpoint of a short drag is painted"), Bridged->GetTexelValue(19, 31) > 0 || Bridged->GetTexelValue(19, 32) > 0);

	UDocPaintableSurfaceComponent* Seam = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Seam"), 64)); // MaxBridgeGapUV 0.1
	Seam->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.2, 0.5), FVector2D(0.4, 0.5) }, 0.03f));
	TestEqual(TEXT("Jump larger than the bridge gap is not bridged"), Seam->GetTexelValue(19, 31) + Seam->GetTexelValue(19, 32), 0);
	TestTrue(TEXT("Both endpoints still painted"), Seam->GetTexelValue(12, 31) + Seam->GetTexelValue(12, 32) > 0 && Seam->GetTexelValue(25, 31) + Seam->GetTexelValue(25, 32) > 0);

	return true;
}

// PNT-03: Doc.Paint.OrderAndDeduplication
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintOrderAndDeduplicationTest, FAutomationTestBase, "Doc.Paint.OrderAndDeduplication", DocPaintTests::Flags)
bool FDocPaintOrderAndDeduplicationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocPaintableSurfaceComponent* Surface = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Order"), 16));

	FDocPaintStroke Missing = DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) });
	Missing.StrokeId.Invalidate();
	TestEqual(TEXT("Stroke id is required"), Surface->ApplyStroke(Missing).Outcome, EDocResultOutcome::InvalidInput);

	const FDocPaintStroke S1 = DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 0.2f);
	TestTrue(TEXT("First application"), Surface->ApplyStroke(S1).IsChanged());
	const int64 Revision = Surface->MaskState.CanonicalRevision;
	const TArray<uint8> Mask = DocPaintTests::Snapshot(Surface);
	const FDocSystemResult Dup = Surface->ApplyStroke(S1);
	TestTrue(TEXT("Duplicate is NoChange"), Dup.IsSuccess() && !Dup.IsChanged());
	TestEqual(TEXT("Duplicate does not bump revision"), Surface->MaskState.CanonicalRevision, Revision);
	TestTrue(TEXT("Duplicate does not change pixels"), DocPaintTests::Snapshot(Surface) == Mask);
	TestEqual(TEXT("Auto sequence assigned"), Surface->MaskState.LastSequence, static_cast<int64>(1));

	FDocPaintStroke Seq5 = DocPaintTests::MakeStroke(EDocPaintOperation::Clean, { FVector2D(0.5, 0.5) }, 0.1f);
	Seq5.SequenceNumber = 5;
	TestTrue(TEXT("Explicit later sequence accepted"), Surface->ApplyStroke(Seq5).IsSuccess());
	FDocPaintStroke Seq3 = DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 0.1f);
	Seq3.SequenceNumber = 3;
	TestEqual(TEXT("Out-of-order sequence is a Conflict"), Surface->ApplyStroke(Seq3).Outcome, EDocResultOutcome::Conflict);

	// Paint/clean order matters and is preserved by the authoritative sequence.
	UDocPaintableSurfaceComponent* A = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("A"), 16));
	UDocPaintableSurfaceComponent* B = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("B"), 16));
	const FDocPaintStroke Paint = DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 0.2f);
	const FDocPaintStroke Clean = DocPaintTests::MakeStroke(EDocPaintOperation::Clean, { FVector2D(0.5, 0.5) }, 0.2f, 0.5f);
	A->ApplyStroke(Paint);
	A->ApplyStroke(Clean);
	B->ApplyStroke(Clean);
	B->ApplyStroke(Paint);
	TestNotEqual(TEXT("Paint-then-clean differs from clean-then-paint"), A->GetTexelValue(7, 7), B->GetTexelValue(7, 7));
	TestTrue(TEXT("Journal keeps the applied order"), A->StrokeJournal.Num() == 2 && A->StrokeJournal[0].StrokeId == Paint.StrokeId && A->StrokeJournal[1].StrokeId == Clean.StrokeId);
	TestTrue(TEXT("Journal sequences increase"), A->StrokeJournal.Num() == 2 && A->StrokeJournal[0].SequenceNumber < A->StrokeJournal[1].SequenceNumber);

	// Dedup survives save/restore.
	FDocPaintSurfaceSnapshot Saved;
	A->CaptureSnapshot(Saved);
	UDocPaintableSurfaceComponent* Loaded = DocPaintTests::SpawnSurface(TW, A->SurfaceDefinition);
	TestTrue(TEXT("Restore"), Loaded->RestoreSnapshot(Saved).IsSuccess());
	const FDocSystemResult Redelivered = Loaded->ApplyStroke(Paint);
	TestTrue(TEXT("Re-delivered stroke after load is NoChange"), Redelivered.IsSuccess() && !Redelivered.IsChanged());

	return true;
}

// PNT-04: Doc.Paint.CoverageDenominator
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintCoverageDenominatorTest, FAutomationTestBase, "Doc.Paint.CoverageDenominator", DocPaintTests::Flags)
bool FDocPaintCoverageDenominatorTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;

	// Eligible region = left half; painting the left half completes regardless of the right half.
	UDocPaintSurfaceDefinition* Def = DocPaintTests::CreateTestDefinition(TEXT("Half"), 16);
	Def->EligibleTexelMask.Init(0, 256);
	for (int32 Y = 0; Y < 16; ++Y)
	{
		for (int32 X = 0; X < 8; ++X)
		{
			Def->EligibleTexelMask[Y * 16 + X] = 1;
		}
	}
	UDocPaintableSurfaceComponent* Surface = DocPaintTests::SpawnSurface(TW, Def);
	FDocPaintCoverageResult Cov = Surface->QueryCoverage();
	TestEqual(TEXT("Denominator is the eligible texels"), Cov.EligibleTexelCount, 128);
	TestTrue(TEXT("Precision declared"), FMath::IsNearlyEqual(Cov.Precision, 1.0f / 128.0f, 1e-6f));

	int32 GoalEvents = 0;
	Surface->OnCoverageGoalChangedNative.AddLambda([&GoalEvents](FName, const FDocPaintCoverageResult& C) { GoalEvents += C.bMeetsThreshold ? 1 : 0; });

	TArray<FVector2D> Column;
	for (int32 i = 0; i <= 16; ++i)
	{
		Column.Add(FVector2D(0.25, i / 16.0));
	}
	Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, Column, 0.5f));
	Cov = Surface->QueryCoverage();
	TestEqual(TEXT("All eligible texels covered"), Cov.CoveredTexelCount, 128);
	TestTrue(TEXT("Goal met"), Cov.bMeetsThreshold);
	Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, Column, 0.5f));
	TestEqual(TEXT("Goal event fired once, not per stroke"), GoalEvents, 1);

	// Zero eligible texels can never report success, even with a 0% requirement.
	UDocPaintSurfaceDefinition* Empty = DocPaintTests::CreateTestDefinition(TEXT("Empty"), 16);
	Empty->EligibleTexelMask.Init(0, 256);
	Empty->RequiredCoverageRatio = 0.0f;
	UDocPaintableSurfaceComponent* EmptySurface = DocPaintTests::SpawnSurface(TW, Empty);
	Cov = EmptySurface->QueryCoverage();
	TestEqual(TEXT("Zero denominator"), Cov.EligibleTexelCount, 0);
	TestEqual(TEXT("Ratio is 0"), Cov.CoverageRatio, 0.0f);
	TestFalse(TEXT("Empty mask never meets the goal"), Cov.bMeetsThreshold);

	// Cleaning goal: BelowThreshold polarity with a dirty start.
	UDocPaintSurfaceDefinition* Dirty = DocPaintTests::CreateTestDefinition(TEXT("Dirty"), 16);
	Dirty->InitialFillValue = 255;
	Dirty->CoverageMode = EDocPaintCoverageMode::BelowThreshold;
	Dirty->RequiredCoverageRatio = 0.5f;
	UDocPaintableSurfaceComponent* Window = DocPaintTests::SpawnSurface(TW, Dirty);
	TestEqual(TEXT("Dirty start: nothing clean"), Window->QueryCoverage().CoveredTexelCount, 0);
	Window->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Clean, Column, 0.5f));
	Cov = Window->QueryCoverage();
	TestTrue(TEXT("Left half cleaned"), Cov.CoverageRatio >= 0.5f);
	TestTrue(TEXT("Cleaning goal met at 50%"), Cov.bMeetsThreshold);

	return true;
}

// PNT-05: Doc.Paint.InstanceIsolation
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintInstanceIsolationTest, FAutomationTestBase, "Doc.Paint.InstanceIsolation", DocPaintTests::Flags)
bool FDocPaintInstanceIsolationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocSurfacePaintingSubsystem* Subsystem = TW.World->GetSubsystem<UDocSurfacePaintingSubsystem>();
	UDocPaintSurfaceDefinition* Shared = DocPaintTests::CreateTestDefinition(TEXT("SharedMesh"), 16);

	UDocPaintableSurfaceComponent* First = DocPaintTests::SpawnSurface(TW, Shared);
	UDocPaintableSurfaceComponent* Second = DocPaintTests::SpawnSurface(TW, Shared);
	TestNotEqual(TEXT("Instances get distinct ids"), First->SurfaceId, Second->SurfaceId);
	TestTrue(TEXT("Both registered"), Subsystem->GetSurfaceComponent(First->SurfaceId) == First && Subsystem->GetSurfaceComponent(Second->SurfaceId) == Second);

	TestTrue(TEXT("Paint first through the subsystem"),
		Subsystem->ApplyStrokeToSurface(First->SurfaceId, DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 0.3f)).IsSuccess());
	TestTrue(TEXT("First painted"), First->QueryCoverage().CoveredTexelCount > 0);
	TestEqual(TEXT("Second untouched"), Second->QueryCoverage().CoveredTexelCount, 0);
	TestTrue(TEXT("Separate mask buffers"), First->MaskState.RawMaskBytes.GetData() != Second->MaskState.RawMaskBytes.GetData());

	// The same stroke id may be applied to another instance (dedup is per surface).
	const FDocPaintStroke Shared1 = DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.2, 0.2) }, 0.1f);
	TestTrue(TEXT("Stroke on first"), First->ApplyStroke(Shared1).IsChanged());
	TestTrue(TEXT("Same stroke id on second is independent"), Second->ApplyStroke(Shared1).IsChanged());

	// A stroke addressed to a different surface id is refused.
	FDocPaintStroke Misrouted = DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) });
	Misrouted.SurfaceId = Second->SurfaceId;
	TestEqual(TEXT("Misrouted stroke refused"), Subsystem->ApplyStrokeToSurface(First->SurfaceId, Misrouted).Outcome, EDocResultOutcome::InvalidInput);

	// Explicit duplicate instance ids conflict.
	UDocPaintableSurfaceComponent* Clash = DocPaintTests::SpawnSurface(TW, Shared, First->SurfaceId);
	TestFalse(TEXT("Duplicate instance id refused"), Clash->LastRegistrationError.IsEmpty());
	TestTrue(TEXT("Registry still points at the first"), Subsystem->GetSurfaceComponent(First->SurfaceId) == First);

	return true;
}

// PNT-06: Doc.Paint.RenderRevision
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintRenderRevisionTest, FAutomationTestBase, "Doc.Paint.RenderRevision", DocPaintTests::Flags)
bool FDocPaintRenderRevisionTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocPaintableSurfaceComponent* Surface = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Render"), 16));

	// A fresh surface needs one full upload before rectangles become local.
	FIntRect Dirty;
	const int64 InitialTicket = Surface->CreateRenderUploadTicket(Dirty);
	TestTrue(TEXT("Initial upload covers the whole mask"), Dirty == FIntRect(0, 0, 16, 16));
	TestTrue(TEXT("Initial full upload commits"), Surface->CommitRenderUpload(InitialTicket).IsChanged());
	Surface->CreateRenderUploadTicket(Dirty);
	TestEqual(TEXT("Nothing pending after the initial upload"), Dirty.Area(), 0);

	Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.25, 0.25) }, 0.1f));
	const int64 OldTicket = Surface->CreateRenderUploadTicket(Dirty);
	TestTrue(TEXT("Dirty rectangle reported"), Dirty.Width() > 0 && Dirty.Height() > 0);
	TestTrue(TEXT("Dirty rectangle is local, not the whole mask"), Dirty.Width() < 16 || Dirty.Height() < 16);

	Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.75, 0.75) }, 0.1f));
	const int64 NewTicket = Surface->CreateRenderUploadTicket(Dirty);
	TestTrue(TEXT("Newer ticket"), NewTicket > OldTicket);

	TestTrue(TEXT("Newest upload commits"), Surface->CommitRenderUpload(NewTicket).IsSuccess());
	TestEqual(TEXT("Late upload of an older revision is rejected"), Surface->CommitRenderUpload(OldTicket).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Render revision stays at the newest"), Surface->MaskState.RenderRevision, NewTicket);
	TestEqual(TEXT("Upload ahead of canonical is refused"), Surface->CommitRenderUpload(NewTicket + 5).Outcome, EDocResultOutcome::InvalidInput);
	Surface->CreateRenderUploadTicket(Dirty);
	TestEqual(TEXT("Nothing dirty after presenting the newest revision"), Dirty.Width(), 0);

	// Coverage is canonical: render lag does not produce extra goal events.
	UDocPaintSurfaceDefinition* GoalDef = DocPaintTests::CreateTestDefinition(TEXT("Goal"), 8);
	GoalDef->RequiredCoverageRatio = 0.1f;
	UDocPaintableSurfaceComponent* Goal = DocPaintTests::SpawnSurface(TW, GoalDef);
	int32 Events = 0;
	Goal->OnCoverageGoalChangedNative.AddLambda([&Events](FName, const FDocPaintCoverageResult&) { ++Events; });
	Goal->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 0.4f));
	const int64 T1 = Goal->CreateRenderUploadTicket(Dirty);
	Goal->CommitRenderUpload(T1);
	Goal->CommitRenderUpload(T1);
	TestEqual(TEXT("Exactly one goal event regardless of uploads"), Events, 1);
	TestTrue(TEXT("Render revision never ahead of canonical"), Goal->MaskState.RenderRevision <= Goal->MaskState.CanonicalRevision);

	return true;
}

// PNT-07: Doc.Paint.CheckpointRestore
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintCheckpointRestoreTest, FAutomationTestBase, "Doc.Paint.CheckpointRestore", DocPaintTests::Flags)
bool FDocPaintCheckpointRestoreTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocPaintSurfaceDefinition* Def = DocPaintTests::CreateTestDefinition(TEXT("Journal"), 32);
	Def->MaxJournalStrokes = 3;
	Def->bAllowLocalUndo = true;
	UDocPaintableSurfaceComponent* Source = DocPaintTests::SpawnSurface(TW, Def);

	TArray<FDocPaintStroke> Applied;
	for (int32 i = 0; i < 5; ++i)
	{
		FDocPaintStroke S = DocPaintTests::MakeStroke(i % 2 ? EDocPaintOperation::Clean : EDocPaintOperation::Paint,
			{ FVector2D(0.1 + 0.15 * i, 0.5), FVector2D(0.15 + 0.15 * i, 0.55) }, 0.08f, 0.9f);
		TestTrue(TEXT("Stroke applied"), Source->ApplyStroke(S).IsSuccess());
		Applied.Add(S);
	}
	TestTrue(TEXT("Journal compacted under its limit"), Source->StrokeJournal.Num() <= 3);
	TestTrue(TEXT("Folded strokes remembered"), Source->GetCheckpointStrokeIdCount() > 0);

	FDocPaintSurfaceSnapshot Saved;
	TestTrue(TEXT("Capture"), Source->CaptureSnapshot(Saved).IsSuccess());
	TestTrue(TEXT("Checkpoint is compressed"), Saved.CompressedCheckpoint.Num() < Saved.UncompressedSize);

	UDocPaintableSurfaceComponent* Loaded = DocPaintTests::SpawnSurface(TW, Def);
	TestTrue(TEXT("Restore"), Loaded->RestoreSnapshot(Saved).IsSuccess());
	TestTrue(TEXT("Checkpoint + journal reproduces the mask byte for byte"), Loaded->MaskState.RawMaskBytes == Source->MaskState.RawMaskBytes);
	TestEqual(TEXT("Journal restored"), Loaded->StrokeJournal.Num(), Source->StrokeJournal.Num());
	const FDocSystemResult OldStroke = Loaded->ApplyStroke(Applied[0]);
	TestTrue(TEXT("A stroke folded into the checkpoint is not re-applied"), OldStroke.IsSuccess() && !OldStroke.IsChanged());

	// Corrupted journal: replay does not match the saved hash; state unchanged.
	FDocPaintSurfaceSnapshot Corrupt = Saved;
	if (Corrupt.Journal.Num() > 0)
	{
		Corrupt.Journal[0].Strength *= 0.5f;
	}
	const TArray<uint8> BeforeCorrupt = DocPaintTests::Snapshot(Loaded);
	TestFalse(TEXT("Corrupted journal refused"), Loaded->RestoreSnapshot(Corrupt).IsSuccess());
	TestTrue(TEXT("State unchanged after refused restore"), DocPaintTests::Snapshot(Loaded) == BeforeCorrupt);

	// Local undo profile: cannot undo another owner's newer stroke.
	UDocPaintableSurfaceComponent* Undo = DocPaintTests::SpawnSurface(TW, Def);
	Undo->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.3, 0.3) }, 0.1f, 1.0f, TEXT("Alice")));
	const TArray<uint8> AfterAlice = DocPaintTests::Snapshot(Undo);
	Undo->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.7, 0.7) }, 0.1f, 1.0f, TEXT("Bob")));
	TestEqual(TEXT("Alice cannot undo Bob's newer stroke"), Undo->UndoLastStroke(TEXT("Alice")).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Bob undoes his own stroke"), Undo->UndoLastStroke(TEXT("Bob")).IsSuccess());
	TestTrue(TEXT("Mask equals the state after Alice"), DocPaintTests::Snapshot(Undo) == AfterAlice);

	// Raw checkpoint restore validates against the definition.
	FDocSurfaceMaskState Raw;
	Source->CreateCheckpoint(Raw);
	Raw.Width = 16;
	TestFalse(TEXT("Raw checkpoint with other dimensions refused"), Loaded->RestoreCheckpoint(Raw).IsSuccess());

	return true;
}

// PNT-08: Doc.Paint.MappingMigration
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintMappingMigrationTest, FAutomationTestBase, "Doc.Paint.MappingMigration", DocPaintTests::Flags)
bool FDocPaintMappingMigrationTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	UDocPaintableSurfaceComponent* V1 = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Crate"), 16, TEXT("Mesh_v1")));
	V1->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 0.3f));
	FDocPaintSurfaceSnapshot Saved;
	V1->CaptureSnapshot(Saved);
	FDocSurfaceMaskState Raw;
	V1->CreateCheckpoint(Raw);

	// Same size, new UV layout.
	UDocPaintableSurfaceComponent* V2 = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Crate"), 16, TEXT("Mesh_v2")));
	const FDocSystemResult Snap = V2->RestoreSnapshot(Saved);
	TestEqual(TEXT("Changed fingerprint is refused (IncompatibleSurface)"), Snap.Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Reason names the incompatibility"), Snap.ToString().Contains(TEXT("IncompatibleSurface")));
	TestEqual(TEXT("No pixels guessed onto the new layout"), V2->QueryCoverage().CoveredTexelCount, 0);
	TestEqual(TEXT("Raw checkpoint with another fingerprint refused"), V2->RestoreCheckpoint(Raw).Outcome, EDocResultOutcome::Conflict);

	// Different resolution.
	UDocPaintableSurfaceComponent* Hi = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Crate"), 32, TEXT("Mesh_v1")));
	TestEqual(TEXT("Different dimensions refused"), Hi->RestoreSnapshot(Saved).Outcome, EDocResultOutcome::InvalidConfiguration);

	// Same fingerprint restores.
	UDocPaintableSurfaceComponent* Same = DocPaintTests::SpawnSurface(TW, DocPaintTests::CreateTestDefinition(TEXT("Crate"), 16, TEXT("Mesh_v1")));
	TestTrue(TEXT("Matching fingerprint restores"), Same->RestoreSnapshot(Saved).IsSuccess());
	TestEqual(TEXT("Coverage matches"), Same->QueryCoverage().CoveredTexelCount, V1->QueryCoverage().CoveredTexelCount);

	return true;
}

// PNT-09: Doc.Paint.ResourceBounds
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintResourceBoundsTest, FAutomationTestBase, "Doc.Paint.ResourceBounds", DocPaintTests::Flags)
bool FDocPaintResourceBoundsTest::RunTest(const FString& Parameters)
{
	UDocPaintSurfaceDefinition* Huge = DocPaintTests::CreateTestDefinition(TEXT("Huge"), 2048);
	TestFalse(TEXT("Oversized mask refused"), Huge->ValidateDefinition().IsSuccess());

	FDocScopedTestWorld TW;
	UDocPaintSurfaceDefinition* Def = DocPaintTests::CreateTestDefinition(TEXT("Bounds"), 32);
	Def->MaxResampledDabs = 3; // a 0.1 UV drag on 32 texels resamples to 5 dabs
	UDocPaintableSurfaceComponent* Surface = DocPaintTests::SpawnSurface(TW, Def);

	TArray<FVector2D> TooMany;
	TooMany.Init(FVector2D(0.5, 0.5), Def->MaxStrokePoints + 1);
	TestEqual(TEXT("Too many points refused"), Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, TooMany)).Outcome, EDocResultOutcome::InvalidInput);

	TestEqual(TEXT("Stroke that resamples beyond the dab limit refused"),
		Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.1, 0.5), FVector2D(0.2, 0.5) }, 0.001f)).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Radius out of range refused"),
		Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 5.0f)).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Nothing painted by refused strokes"), Surface->QueryCoverage().CoveredTexelCount, 0);

	Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 0.2f));
	FDocPaintSurfaceSnapshot Good;
	Surface->CaptureSnapshot(Good);

	FDocPaintSurfaceSnapshot Bomb = Good;
	Bomb.UncompressedSize = 512 * 1024 * 1024;
	TestEqual(TEXT("Declared decompressed size beyond the mask refused before allocation"), Surface->RestoreSnapshot(Bomb).Outcome, EDocResultOutcome::InvalidInput);
	Bomb = Good;
	Bomb.CompressedCheckpoint.SetNumZeroed(FCompression::CompressMemoryBound(NAME_Zlib, Good.UncompressedSize) + 1);
	TestEqual(TEXT("Compressed payload larger than possible refused"), Surface->RestoreSnapshot(Bomb).Outcome, EDocResultOutcome::InvalidInput);
	Bomb = Good;
	Bomb.Journal.SetNum(Def->MaxJournalStrokes + 1);
	TestEqual(TEXT("Oversized journal refused"), Surface->RestoreSnapshot(Bomb).Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Valid snapshot still restores"), Surface->RestoreSnapshot(Good).IsSuccess());

	return true;
}

// PNT-10: Doc.Paint.CookedMaterial
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocPaintCookedMaterialTest, FAutomationTestBase, "Doc.Paint.CookedMaterial", DocPaintTests::Flags)
bool FDocPaintCookedMaterialTest::RunTest(const FString& Parameters)
{
	// Headless half of the requirement: canonical logic works without any render texture. A real cooked material
	// displaying the mask is a manual gate and is not claimed here.
	FDocScopedTestWorld TW;
	AActor* Actor = TW.Spawn<AActor>();
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
	Mesh->RegisterComponent();
	UDocPaintableSurfaceComponent* Surface = NewObject<UDocPaintableSurfaceComponent>(Actor);
	Surface->SurfaceDefinition = DocPaintTests::CreateTestDefinition(TEXT("Headless"), 16);
	Surface->bCreateRenderTexture = true;
	Surface->TargetMesh = Mesh;
	Surface->RegisterComponent();

	TestTrue(TEXT("Paint works with or without a render device"),
		Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Paint, { FVector2D(0.5, 0.5) }, 0.3f)).IsSuccess());
	TestTrue(TEXT("Canonical coverage available"), Surface->QueryCoverage().CoveredTexelCount > 0);
	TestTrue(TEXT("Presentation never runs ahead of canonical state"), Surface->MaskState.RenderRevision <= Surface->MaskState.CanonicalRevision);
	TestTrue(TEXT("Presentation, when present, shows the newest revision"),
		Surface->MaskState.RenderRevision == 0 || Surface->MaskState.RenderRevision == Surface->MaskState.CanonicalRevision);

	Surface->ApplyStroke(DocPaintTests::MakeStroke(EDocPaintOperation::Clean, { FVector2D(0.5, 0.5) }, 0.3f));
	TestTrue(TEXT("Clean lowers the mask at the dab centre (min(m, 255 - a))"), Surface->GetTexelValue(7, 7) < 60);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
