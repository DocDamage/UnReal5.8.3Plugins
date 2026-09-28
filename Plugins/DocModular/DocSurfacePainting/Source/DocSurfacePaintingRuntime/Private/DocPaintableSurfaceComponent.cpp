#include "DocPaintableSurfaceComponent.h"
#include "DocSurfacePaintingSubsystem.h"
#include "IDocSurfaceCoordinateProvider.h"
#include "DocSurfacePaintingLog.h"
#include "Components/MeshComponent.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/Compression.h"
#include "Misc/Crc.h"

namespace DocPaintPrivate
{
	static void GrowRect(FIntRect& Rect, bool& bHas, int32 MinX, int32 MinY, int32 MaxX, int32 MaxY)
	{
		if (!bHas)
		{
			Rect = FIntRect(MinX, MinY, MaxX, MaxY);
			bHas = true;
			return;
		}
		Rect.Min.X = FMath::Min(Rect.Min.X, MinX);
		Rect.Min.Y = FMath::Min(Rect.Min.Y, MinY);
		Rect.Max.X = FMath::Max(Rect.Max.X, MaxX);
		Rect.Max.Y = FMath::Max(Rect.Max.Y, MaxY);
	}

	/** Point validation + bounded resampling. Pure: depends only on the stroke and the definition. */
	static FDocSystemResult BuildDabs(const FDocPaintStroke& Stroke, const UDocPaintSurfaceDefinition& Def, TArray<FVector2D>& OutDabs)
	{
		OutDabs.Reset();
		if (Stroke.Points.Num() == 0 || Stroke.Points.Num() > Def.MaxStrokePoints)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
				FString::Printf(TEXT("Stroke needs 1..%d points (got %d)"), Def.MaxStrokePoints, Stroke.Points.Num()));
		}
		if (!FMath::IsFinite(Stroke.RadiusUV) || Stroke.RadiusUV <= 0.0f || Stroke.RadiusUV > 1.0f)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("RadiusUV must be in (0, 1]"));
		}
		if (!FMath::IsFinite(Stroke.Strength) || Stroke.Strength < 0.0f || Stroke.Strength > 1.0f)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Strength must be in [0, 1]"));
		}

		TArray<FVector2D> Points;
		Points.Reserve(Stroke.Points.Num());
		for (const FVector2D& P : Stroke.Points)
		{
			if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y))
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("InvalidMapping: non-finite UV"));
			}
			if (Def.AddressMode == EDocPaintUVAddressMode::Wrap)
			{
				Points.Add(FVector2D(P.X - FMath::FloorToDouble(P.X), P.Y - FMath::FloorToDouble(P.Y)));
			}
			else
			{
				if (P.X < 0.0 || P.X > 1.0 || P.Y < 0.0 || P.Y > 1.0)
				{
					return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("InvalidMapping: UV outside [0,1] with Reject address mode"));
				}
				Points.Add(P);
			}
		}

		// Count first, refuse oversize work before building anything.
		const double Step = FMath::Max(static_cast<double>(Stroke.RadiusUV) * 0.5, 1.0 / FMath::Max(Def.Width, Def.Height));
		int64 Count = 1;
		for (int32 i = 1; i < Points.Num(); ++i)
		{
			const double Gap = FVector2D::Distance(Points[i - 1], Points[i]);
			Count += (Gap > 0.0 && Gap <= Def.MaxBridgeGapUV) ? static_cast<int64>(FMath::CeilToDouble(Gap / Step)) : 1;
			if (Count > Def.MaxResampledDabs)
			{
				return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
					FString::Printf(TEXT("Stroke resamples to more than %d dabs"), Def.MaxResampledDabs));
			}
		}

		OutDabs.Reserve(static_cast<int32>(Count));
		OutDabs.Add(Points[0]);
		for (int32 i = 1; i < Points.Num(); ++i)
		{
			const double Gap = FVector2D::Distance(Points[i - 1], Points[i]);
			if (Gap > 0.0 && Gap <= Def.MaxBridgeGapUV)
			{
				const int32 N = static_cast<int32>(FMath::CeilToDouble(Gap / Step));
				for (int32 k = 1; k <= N; ++k)
				{
					OutDabs.Add(FMath::Lerp(Points[i - 1], Points[i], static_cast<double>(k) / N));
				}
			}
			else
			{
				OutDabs.Add(Points[i]); // no bridge across a seam or a jump between UV islands
			}
		}
		return FDocSystemResult::MakeSuccess();
	}
}

UDocPaintableSurfaceComponent::UDocPaintableSurfaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocPaintableSurfaceComponent::OnRegister()
{
	Super::OnRegister();
	if (SurfaceId.IsNone() && GetOwner())
	{
		SurfaceId = GetOwner()->GetFName();
	}
	if (SurfaceDefinition && MaskState.RawMaskBytes.IsEmpty())
	{
		InitializeCanvas(SurfaceDefinition);
	}
	if (UWorld* World = GetWorld())
	{
		if (UDocSurfacePaintingSubsystem* Subsystem = World->GetSubsystem<UDocSurfacePaintingSubsystem>())
		{
			const FDocSystemResult Result = Subsystem->RegisterSurfaceComponent(this);
			LastRegistrationError = Result.IsSuccess() ? FString() : Result.ToString();
		}
	}
}

void UDocPaintableSurfaceComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocSurfacePaintingSubsystem* Subsystem = World->GetSubsystem<UDocSurfacePaintingSubsystem>())
		{
			Subsystem->UnregisterSurfaceComponent(this);
		}
	}
	Super::OnUnregister();
}

int32 UDocPaintableSurfaceComponent::JournalLimit() const
{
	return SurfaceDefinition ? SurfaceDefinition->MaxJournalStrokes : FMath::Max(1, MaxHistoryStrokes);
}

FDocSystemResult UDocPaintableSurfaceComponent::InitializeCanvas(const UDocPaintSurfaceDefinition* InDef)
{
	if (!InDef)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Definition is null"));
	}
	const FDocSystemResult Val = InDef->ValidateDefinition();
	if (!Val.IsSuccess())
	{
		return Val;
	}

	SurfaceDefinition = const_cast<UDocPaintSurfaceDefinition*>(InDef);
	if (SurfaceId.IsNone())
	{
		SurfaceId = GetOwner() ? GetOwner()->GetFName() : InDef->SurfaceId;
	}

	const int32 Total = InDef->Width * InDef->Height;
	const int64 PriorRevision = MaskState.CanonicalRevision;
	MaskState = FDocSurfaceMaskState();
	MaskState.SurfaceId = SurfaceId;
	MaskState.Width = InDef->Width;
	MaskState.Height = InDef->Height;
	MaskState.CanonicalRevision = PriorRevision + 1;
	MaskState.MeshFingerprint = InDef->MeshFingerprint;
	MaskState.RawMaskBytes.Init(InDef->InitialFillValue, Total);
	MaskState.EligibleTexels.SetNum(Total);
	for (int32 i = 0; i < Total; ++i)
	{
		MaskState.EligibleTexels[i] = InDef->EligibleTexelMask.Num() == Total ? InDef->EligibleTexelMask[i] != 0 : true;
	}

	CheckpointMask = MaskState.RawMaskBytes;
	CheckpointSequence = 0;
	CheckpointStrokeIds.Reset();
	AppliedStrokeIds.Reset();
	StrokeJournal.Reset();
	bHasDirty = false;
	DocPaintPrivate::GrowRect(DirtySinceUpload, bHasDirty, 0, 0, InDef->Width, InDef->Height);
	bLastGoalMet = QueryCoverage().bMeetsThreshold;
	return FDocSystemResult::MakeSuccess();
}

EDocPaintMappingStatus UDocPaintableSurfaceComponent::GetMappingStatus() const
{
	return SurfaceDefinition ? SurfaceDefinition->GetMappingStatus() : EDocPaintMappingStatus::Unsupported;
}

FDocSystemResult UDocPaintableSurfaceComponent::ValidateStroke(const FDocPaintStroke& Stroke, TArray<FVector2D>& OutDabs, int64& OutSequence) const
{
	if (!SurfaceDefinition || MaskState.RawMaskBytes.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Canvas is not initialized"));
	}
	const EDocPaintMappingStatus Mapping = GetMappingStatus();
	if (Mapping != EDocPaintMappingStatus::Valid)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported,
			FString::Printf(TEXT("Mapping status %s: painting refused"), *UEnum::GetValueAsString(Mapping)));
	}
	if (!Stroke.StrokeId.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("StrokeId is required"));
	}
	if (AppliedStrokeIds.Contains(Stroke.StrokeId))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Stroke was already applied (deduplicated)"));
	}
	if (Stroke.SequenceNumber != 0 && Stroke.SequenceNumber <= MaskState.LastSequence)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict,
			FString::Printf(TEXT("Stroke sequence %lld is not after %lld"), Stroke.SequenceNumber, MaskState.LastSequence));
	}
	OutSequence = Stroke.SequenceNumber != 0 ? Stroke.SequenceNumber : MaskState.LastSequence + 1;
	return DocPaintPrivate::BuildDabs(Stroke, *SurfaceDefinition, OutDabs);
}

void UDocPaintableSurfaceComponent::Rasterize(TArray<uint8>& Mask, int32 Width, int32 Height, const TArray<FVector2D>& Dabs, float Radius, float Strength, EDocPaintOperation Op, FIntRect& InOutDirty)
{
	bool bHas = InOutDirty.Max.X > InOutDirty.Min.X;
	for (const FVector2D& Point : Dabs)
	{
		const int32 MinX = FMath::Clamp(FMath::FloorToInt32((Point.X - Radius) * Width), 0, Width - 1);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt32((Point.X + Radius) * Width), 0, Width - 1);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt32((Point.Y - Radius) * Height), 0, Height - 1);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt32((Point.Y + Radius) * Height), 0, Height - 1);
		DocPaintPrivate::GrowRect(InOutDirty, bHas, MinX, MinY, MaxX + 1, MaxY + 1);

		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FVector2D TexelUV((X + 0.5) / Width, (Y + 0.5) / Height);
				const double Dist = FVector2D::Distance(TexelUV, Point);
				if (Dist > Radius)
				{
					continue;
				}
				const float Alpha = static_cast<float>((1.0 - Dist / Radius) * Strength);
				const uint8 A = static_cast<uint8>(FMath::Clamp(FMath::FloorToInt32(Alpha * 255.0f + 0.5f), 0, 255));
				uint8& M = Mask[Y * Width + X];
				switch (Op)
				{
				case EDocPaintOperation::Paint:
					M = FMath::Max(M, A);
					break;
				case EDocPaintOperation::Clean:
				case EDocPaintOperation::Reveal:
					M = FMath::Min(M, static_cast<uint8>(255 - A));
					break;
				}
			}
		}
	}
}

bool UDocPaintableSurfaceComponent::ReplayInto(TArray<uint8>& Mask, const TArray<FDocPaintStroke>& Strokes, FIntRect& InOutDirty) const
{
	if (!SurfaceDefinition)
	{
		return false;
	}
	TArray<FVector2D> Dabs;
	for (const FDocPaintStroke& S : Strokes)
	{
		if (!DocPaintPrivate::BuildDabs(S, *SurfaceDefinition, Dabs).IsSuccess())
		{
			return false;
		}
		Rasterize(Mask, SurfaceDefinition->Width, SurfaceDefinition->Height, Dabs, S.RadiusUV, S.Strength, S.Operation, InOutDirty);
	}
	return true;
}

FDocSystemResult UDocPaintableSurfaceComponent::ApplyStroke(const FDocPaintStroke& Stroke)
{
	TArray<FVector2D> Dabs;
	int64 Sequence = 0;
	const FDocSystemResult Valid = ValidateStroke(Stroke, Dabs, Sequence);
	if (!Valid.IsChanged())
	{
		return Valid; // failure or NoChange (duplicate): nothing touched
	}

	FIntRect Dirty(0, 0, 0, 0);
	Rasterize(MaskState.RawMaskBytes, MaskState.Width, MaskState.Height, Dabs, Stroke.RadiusUV, Stroke.Strength, Stroke.Operation, Dirty);
	DocPaintPrivate::GrowRect(DirtySinceUpload, bHasDirty, Dirty.Min.X, Dirty.Min.Y, Dirty.Max.X, Dirty.Max.Y);

	FDocPaintStroke Stored = Stroke;
	Stored.SequenceNumber = Sequence;
	Stored.SurfaceId = SurfaceId;
	StrokeJournal.Add(Stored);
	AppliedStrokeIds.Add(Stroke.StrokeId);
	MaskState.LastSequence = Sequence;
	MaskState.CanonicalRevision++;
	CompactJournalIfNeeded();

	OnStrokeApplied.Broadcast(SurfaceId, Stored);
	OnStrokeAppliedNative.Broadcast(SurfaceId, Stored);
	BroadcastCoverage();
	UpdatePresentation();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPaintableSurfaceComponent::PaintAtHit(const FHitResult& Hit, const IDocSurfaceCoordinateProvider& Provider, EDocPaintOperation Operation,
	float RadiusUV, float Strength, FName OwnerId, FDocPaintStroke& OutStroke)
{
	if (!SurfaceDefinition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Canvas is not initialized"));
	}
	FVector2D UV = FVector2D::ZeroVector;
	const EDocPaintMappingStatus Status = Provider.ResolveSurfaceUV(Hit, SurfaceDefinition->UVChannel, UV);
	if (Status != EDocPaintMappingStatus::Valid)
	{
		const EDocResultOutcome Outcome = Status == EDocPaintMappingStatus::Unsupported ? EDocResultOutcome::Unsupported : EDocResultOutcome::InvalidInput;
		return FDocSystemResult::MakeFailure(Outcome, FString::Printf(TEXT("No surface UV: %s"), *UEnum::GetValueAsString(Status)));
	}
	OutStroke = FDocPaintStroke();
	OutStroke.StrokeId = FGuid::NewGuid();
	OutStroke.SurfaceId = SurfaceId;
	OutStroke.OwnerId = OwnerId;
	OutStroke.Operation = Operation;
	OutStroke.Points.Add(UV);
	OutStroke.RadiusUV = RadiusUV;
	OutStroke.Strength = Strength;
	return ApplyStroke(OutStroke);
}

void UDocPaintableSurfaceComponent::CompactJournalIfNeeded()
{
	if (StrokeJournal.Num() <= JournalLimit())
	{
		return;
	}
	CheckpointMask = MaskState.RawMaskBytes;
	CheckpointSequence = MaskState.LastSequence;
	for (const FDocPaintStroke& S : StrokeJournal)
	{
		CheckpointStrokeIds.Add(S.StrokeId);
	}
	StrokeJournal.Reset();
	// Bounded id memory; older duplicates are still rejected by the sequence rule.
	const int32 Excess = CheckpointStrokeIds.Num() - MaxRememberedStrokeIds;
	if (Excess > 0)
	{
		for (int32 i = 0; i < Excess; ++i)
		{
			AppliedStrokeIds.Remove(CheckpointStrokeIds[i]);
		}
		CheckpointStrokeIds.RemoveAt(0, Excess);
	}
}

FDocPaintCoverageResult UDocPaintableSurfaceComponent::QueryCoverage() const
{
	FDocPaintCoverageResult Result;
	Result.CanonicalRevision = MaskState.CanonicalRevision;
	if (SurfaceDefinition)
	{
		Result.Threshold = SurfaceDefinition->CoverageThreshold;
		Result.Mode = SurfaceDefinition->CoverageMode;
		Result.RequiredRatio = SurfaceDefinition->RequiredCoverageRatio;
	}
	if (MaskState.RawMaskBytes.IsEmpty())
	{
		return Result;
	}

	int32 Eligible = 0;
	int32 Covered = 0;
	for (int32 i = 0; i < MaskState.RawMaskBytes.Num(); ++i)
	{
		if (MaskState.EligibleTexels.IsValidIndex(i) && !MaskState.EligibleTexels[i])
		{
			continue;
		}
		++Eligible;
		const uint8 M = MaskState.RawMaskBytes[i];
		const bool bCovered = Result.Mode == EDocPaintCoverageMode::AtOrAboveThreshold ? M >= Result.Threshold : M < Result.Threshold;
		Covered += bCovered ? 1 : 0;
	}
	Result.EligibleTexelCount = Eligible;
	Result.CoveredTexelCount = Covered;
	Result.CoverageRatio = Eligible > 0 ? static_cast<float>(Covered) / static_cast<float>(Eligible) : 0.0f;
	Result.Precision = Eligible > 0 ? 1.0f / static_cast<float>(Eligible) : 0.0f;
	// Compare in integer space to avoid float rounding at the boundary.
	Result.bMeetsThreshold = Eligible > 0
		&& static_cast<double>(Covered) >= FMath::CeilToDouble(static_cast<double>(Result.RequiredRatio) * Eligible - 1e-9);
	return Result;
}

void UDocPaintableSurfaceComponent::BroadcastCoverage()
{
	const FDocPaintCoverageResult Coverage = QueryCoverage();
	OnCoverageUpdated.Broadcast(SurfaceId, Coverage);
	OnCoverageUpdatedNative.Broadcast(SurfaceId, Coverage);
	if (Coverage.bMeetsThreshold != bLastGoalMet)
	{
		bLastGoalMet = Coverage.bMeetsThreshold;
		OnCoverageGoalChanged.Broadcast(SurfaceId, Coverage);
		OnCoverageGoalChangedNative.Broadcast(SurfaceId, Coverage);
	}
}

// ---------------------------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------------------------

int64 UDocPaintableSurfaceComponent::CreateRenderUploadTicket(FIntRect& OutDirtyRect) const
{
	OutDirtyRect = bHasDirty ? DirtySinceUpload : FIntRect(0, 0, 0, 0);
	return MaskState.CanonicalRevision;
}

FDocSystemResult UDocPaintableSurfaceComponent::CommitRenderUpload(int64 UploadedRevision)
{
	if (UploadedRevision > MaskState.CanonicalRevision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Upload revision is ahead of canonical state"));
	}
	if (UploadedRevision < MaskState.RenderRevision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Stale render upload revision rejected"));
	}
	if (UploadedRevision == MaskState.RenderRevision)
	{
		// The initial (or post-restore) full upload shares the current revision; it still clears the pending rectangle.
		if (bHasDirty && UploadedRevision == MaskState.CanonicalRevision)
		{
			bHasDirty = false;
			return FDocSystemResult::MakeSuccess();
		}
		return FDocSystemResult::MakeNoChange(TEXT("Upload revision already presented"));
	}
	MaskState.RenderRevision = UploadedRevision;
	if (UploadedRevision == MaskState.CanonicalRevision)
	{
		bHasDirty = false;
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocPaintableSurfaceComponent::UpdatePresentation()
{
	if (!bCreateRenderTexture || !TargetMesh || !FApp::CanEverRender() || MaskState.RawMaskBytes.IsEmpty()
		|| TargetMesh->GetNumMaterials() <= MaterialSlot || MaterialSlot < 0 || !TargetMesh->GetMaterial(MaterialSlot))
	{
		return; // canonical state stays authoritative without a texture (headless, dedicated server, unloaded presentation)
	}
	if (!RenderTexture)
	{
		RenderTexture = UTexture2D::CreateTransient(MaskState.Width, MaskState.Height, PF_G8);
		if (!RenderTexture)
		{
			return;
		}
		RenderTexture->SRGB = false;
		RenderTexture->Filter = TF_Bilinear;
		RenderMaterial = TargetMesh->CreateAndSetMaterialInstanceDynamic(MaterialSlot); // per-instance, never the shared asset
		if (RenderMaterial)
		{
			RenderMaterial->SetTextureParameterValue(MaskTextureParameter, RenderTexture);
		}
	}

	FIntRect Dirty;
	const int64 Ticket = CreateRenderUploadTicket(Dirty);
	if (FTexturePlatformData* Platform = RenderTexture->GetPlatformData())
	{
		if (Platform->Mips.Num() > 0)
		{
			FTexture2DMipMap& Mip = Platform->Mips[0];
			if (void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE))
			{
				FMemory::Memcpy(Data, MaskState.RawMaskBytes.GetData(), FMath::Min<int64>(Mip.BulkData.GetBulkDataSize(), MaskState.RawMaskBytes.Num()));
			}
			Mip.BulkData.Unlock();
			RenderTexture->UpdateResource();
			CommitRenderUpload(Ticket);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Checkpoints, snapshots, undo
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocPaintableSurfaceComponent::CreateCheckpoint(FDocSurfaceMaskState& OutCheckpoint) const
{
	if (MaskState.RawMaskBytes.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Mask state is empty"));
	}
	OutCheckpoint = MaskState;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPaintableSurfaceComponent::RestoreCheckpoint(const FDocSurfaceMaskState& InCheckpoint)
{
	if (!SurfaceDefinition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Canvas is not initialized"));
	}
	if (InCheckpoint.Width != SurfaceDefinition->Width || InCheckpoint.Height != SurfaceDefinition->Height
		|| InCheckpoint.RawMaskBytes.Num() != InCheckpoint.Width * InCheckpoint.Height)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Checkpoint mask data is corrupted or mismatched dimensions"));
	}
	if (!SurfaceDefinition->MeshFingerprint.IsEmpty() && InCheckpoint.MeshFingerprint != SurfaceDefinition->MeshFingerprint)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("IncompatibleSurface: mesh fingerprint mismatch: expected %s, got %s"),
			*SurfaceDefinition->MeshFingerprint, *InCheckpoint.MeshFingerprint));
	}

	MaskState.RawMaskBytes = InCheckpoint.RawMaskBytes; // eligibility stays authored
	MaskState.LastSequence = FMath::Max(MaskState.LastSequence, InCheckpoint.LastSequence);
	MaskState.CanonicalRevision = FMath::Max(MaskState.CanonicalRevision, InCheckpoint.CanonicalRevision) + 1;
	CheckpointMask = MaskState.RawMaskBytes;
	CheckpointSequence = MaskState.LastSequence;
	StrokeJournal.Reset();
	DocPaintPrivate::GrowRect(DirtySinceUpload, bHasDirty, 0, 0, MaskState.Width, MaskState.Height);
	bLastGoalMet = QueryCoverage().bMeetsThreshold; // restore never fires completion
	BroadcastCoverage();
	UpdatePresentation();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPaintableSurfaceComponent::CaptureSnapshot(FDocPaintSurfaceSnapshot& OutSnapshot) const
{
	if (!SurfaceDefinition || MaskState.RawMaskBytes.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Canvas is not initialized"));
	}
	OutSnapshot = FDocPaintSurfaceSnapshot();
	OutSnapshot.SurfaceId = SurfaceId;
	OutSnapshot.MeshFingerprint = SurfaceDefinition->MeshFingerprint;
	OutSnapshot.Width = MaskState.Width;
	OutSnapshot.Height = MaskState.Height;
	OutSnapshot.RasterizerVersion = UDocPaintSurfaceDefinition::RasterizerVersion;
	OutSnapshot.UncompressedSize = CheckpointMask.Num();

	int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, CheckpointMask.Num());
	OutSnapshot.CompressedCheckpoint.SetNumUninitialized(CompressedSize);
	if (!FCompression::CompressMemory(NAME_Zlib, OutSnapshot.CompressedCheckpoint.GetData(), CompressedSize, CheckpointMask.GetData(), CheckpointMask.Num()))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Checkpoint compression failed; previous save remains valid"));
	}
	OutSnapshot.CompressedCheckpoint.SetNum(CompressedSize);
	OutSnapshot.CheckpointSequence = CheckpointSequence;
	OutSnapshot.Journal = StrokeJournal;
	OutSnapshot.CheckpointStrokeIds = CheckpointStrokeIds;
	OutSnapshot.CanonicalRevision = MaskState.CanonicalRevision;
	OutSnapshot.MaskHash = static_cast<int64>(FCrc::MemCrc32(MaskState.RawMaskBytes.GetData(), MaskState.RawMaskBytes.Num()));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPaintableSurfaceComponent::RestoreSnapshot(const FDocPaintSurfaceSnapshot& Snapshot)
{
	if (!SurfaceDefinition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Canvas is not initialized"));
	}
	const UDocPaintSurfaceDefinition& Def = *SurfaceDefinition;

	// Everything is validated before any allocation or state change.
	if (Snapshot.SchemaVersion != FDocPaintSurfaceSnapshot::CurrentSchemaVersion || Snapshot.RasterizerVersion != UDocPaintSurfaceDefinition::RasterizerVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Unsupported snapshot schema or rasterizer version"));
	}
	if (!Def.MeshFingerprint.IsEmpty() && Snapshot.MeshFingerprint != Def.MeshFingerprint)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("IncompatibleSurface: saved paint belongs to a different mesh/UV fingerprint"));
	}
	if (Snapshot.Width != Def.Width || Snapshot.Height != Def.Height)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Snapshot dimensions do not match the definition"));
	}
	const int64 Expected = static_cast<int64>(Def.Width) * Def.Height;
	if (Snapshot.UncompressedSize != Expected || Expected > UDocPaintSurfaceDefinition::MaxTotalTexels)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Declared decompressed size is invalid"));
	}
	if (Snapshot.CompressedCheckpoint.Num() <= 0 || Snapshot.CompressedCheckpoint.Num() > FCompression::CompressMemoryBound(NAME_Zlib, Snapshot.UncompressedSize))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Compressed checkpoint size is invalid"));
	}
	if (Snapshot.Journal.Num() > JournalLimit() || Snapshot.CheckpointStrokeIds.Num() > MaxRememberedStrokeIds)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Snapshot journal exceeds limits"));
	}
	int64 PrevSeq = Snapshot.CheckpointSequence;
	for (const FDocPaintStroke& S : Snapshot.Journal)
	{
		if (!S.StrokeId.IsValid() || S.SequenceNumber <= PrevSeq || S.Points.Num() > Def.MaxStrokePoints)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Snapshot journal is out of order or invalid"));
		}
		PrevSeq = S.SequenceNumber;
	}

	TArray<uint8> Checkpoint;
	Checkpoint.SetNumUninitialized(Snapshot.UncompressedSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Checkpoint.GetData(), Snapshot.UncompressedSize, Snapshot.CompressedCheckpoint.GetData(), Snapshot.CompressedCheckpoint.Num()))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Checkpoint decompression failed"));
	}
	TArray<uint8> Replayed = Checkpoint;
	FIntRect Dirty(0, 0, 0, 0);
	if (!ReplayInto(Replayed, Snapshot.Journal, Dirty))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Journal stroke failed validation during replay"));
	}
	if (static_cast<int64>(FCrc::MemCrc32(Replayed.GetData(), Replayed.Num())) != Snapshot.MaskHash)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Checkpoint + journal does not reproduce the saved mask; state unchanged"));
	}

	// Commit.
	CheckpointMask = MoveTemp(Checkpoint);
	MaskState.RawMaskBytes = MoveTemp(Replayed);
	CheckpointSequence = Snapshot.CheckpointSequence;
	CheckpointStrokeIds = Snapshot.CheckpointStrokeIds;
	StrokeJournal = Snapshot.Journal;
	AppliedStrokeIds.Reset();
	for (const FGuid& Id : CheckpointStrokeIds) { AppliedStrokeIds.Add(Id); }
	for (const FDocPaintStroke& S : StrokeJournal) { AppliedStrokeIds.Add(S.StrokeId); }
	MaskState.LastSequence = PrevSeq;
	MaskState.CanonicalRevision = FMath::Max(MaskState.CanonicalRevision, Snapshot.CanonicalRevision) + 1;
	DocPaintPrivate::GrowRect(DirtySinceUpload, bHasDirty, 0, 0, MaskState.Width, MaskState.Height);
	bLastGoalMet = QueryCoverage().bMeetsThreshold; // restore never fires completion rewards
	BroadcastCoverage();
	UpdatePresentation();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocPaintableSurfaceComponent::UndoLastStroke(FName OwnerId)
{
	if (!SurfaceDefinition || !SurfaceDefinition->bAllowLocalUndo)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Undo is not enabled for this surface"));
	}
	if (StrokeJournal.Num() == 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Nothing to undo after the last checkpoint"));
	}
	if (StrokeJournal.Last().OwnerId != OwnerId)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("The newest stroke belongs to another owner"));
	}

	TArray<FDocPaintStroke> Remaining = StrokeJournal;
	Remaining.Pop();
	TArray<uint8> Rebuilt = CheckpointMask;
	FIntRect Dirty(0, 0, 0, 0);
	if (!ReplayInto(Rebuilt, Remaining, Dirty))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Replay failed; state unchanged"));
	}
	MaskState.RawMaskBytes = MoveTemp(Rebuilt);
	StrokeJournal = MoveTemp(Remaining); // the undone id stays in AppliedStrokeIds, so a late duplicate cannot resurrect it
	MaskState.CanonicalRevision++;
	DocPaintPrivate::GrowRect(DirtySinceUpload, bHasDirty, 0, 0, MaskState.Width, MaskState.Height);
	BroadcastCoverage();
	UpdatePresentation();
	return FDocSystemResult::MakeSuccess();
}

void UDocPaintableSurfaceComponent::SetEligibleTexelsMask(const TArray<bool>& InEligible)
{
	if (InEligible.Num() == MaskState.RawMaskBytes.Num())
	{
		MaskState.EligibleTexels = InEligible;
		bLastGoalMet = QueryCoverage().bMeetsThreshold;
	}
}

uint8 UDocPaintableSurfaceComponent::GetTexelValue(int32 X, int32 Y) const
{
	if (X >= 0 && X < MaskState.Width && Y >= 0 && Y < MaskState.Height)
	{
		const int32 Index = Y * MaskState.Width + X;
		if (MaskState.RawMaskBytes.IsValidIndex(Index))
		{
			return MaskState.RawMaskBytes[Index];
		}
	}
	return 0;
}
