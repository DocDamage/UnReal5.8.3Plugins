#include "DocPaintDefinitions.h"

FDocSystemResult UDocPaintSurfaceDefinition::ValidateDefinition() const
{
	if (SurfaceId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("SurfaceId is empty"));
	}
	if (Width <= 0 || Width > MaxMaskDimension || Height <= 0 || Height > MaxMaskDimension
		|| static_cast<int64>(Width) * static_cast<int64>(Height) > MaxTotalTexels)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Dimensions must be in [1, 1024] and at most 1024*1024 texels"));
	}
	if (EligibleTexelMask.Num() != 0 && EligibleTexelMask.Num() != Width * Height)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("EligibleTexelMask must be empty or Width*Height bytes"));
	}
	if (!FMath::IsFinite(RequiredCoverageRatio) || RequiredCoverageRatio < 0.0f || RequiredCoverageRatio > 1.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("RequiredCoverageRatio must be in [0, 1]"));
	}
	if (!FMath::IsFinite(MaxBridgeGapUV) || MaxBridgeGapUV < 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("MaxBridgeGapUV must be finite and >= 0"));
	}
	if (MaxStrokePoints <= 0 || MaxResampledDabs <= 0 || MaxJournalStrokes <= 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Limits must be positive"));
	}
	return FDocSystemResult::MakeSuccess();
}

EDocPaintMappingStatus UDocPaintSurfaceDefinition::GetMappingStatus() const
{
	if (bHasOverlappingUVs && !bAcceptSharedOverlappingUVs)
	{
		return EDocPaintMappingStatus::OverlappingUVs;
	}
	return EDocPaintMappingStatus::Valid;
}

FDocSystemResult UDocPaintBrushDefinition::ValidateDefinition() const
{
	if (BrushId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("BrushId is empty"));
	}
	if (DefaultRadiusUV <= 0.0f || DefaultRadiusUV > 1.0f || !FMath::IsFinite(DefaultRadiusUV))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("DefaultRadiusUV must be in (0, 1]"));
	}
	if (DefaultStrength < 0.0f || DefaultStrength > 1.0f || !FMath::IsFinite(DefaultStrength))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("DefaultStrength must be in [0, 1]"));
	}
	return FDocSystemResult::MakeSuccess();
}
