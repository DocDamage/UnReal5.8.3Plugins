#include "DocRaceTimingTypes.h"

bool FDocRaceGateDefinition::TestSweptCrossing(const FVector& P0, const FVector& P1, float& OutFraction, FVector& OutCrossPoint) const
{
	FVector Normal = ForwardDirection.GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		Normal = FVector::ForwardVector;
	}

	if (P0.ContainsNaN() || P1.ContainsNaN() || Location.ContainsNaN())
	{
		return false;
	}

	const double d0 = FVector::DotProduct(P0 - Location, Normal);
	const double d1 = FVector::DotProduct(P1 - Location, Normal);

	// Forward directional crossing: strictly behind the plane, then strictly in front.
	// Starting on the plane (or inside the start volume) is not a crossing (RAC-01).
	if (!(d0 < 0.0) || !(d1 > 0.0))
	{
		return false;
	}

	const double Denom = d1 - d0;
	if (FMath::IsNearlyZero(Denom))
	{
		return false;
	}

	const double Alpha = -d0 / Denom;
	OutFraction = (float)FMath::Clamp(Alpha, 0.0, 1.0);
	OutCrossPoint = FMath::Lerp(P0, P1, OutFraction);

	// Extents check
	FVector Offset = OutCrossPoint - Location;
	FVector Right = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::CrossProduct(FVector::ForwardVector, Normal).GetSafeNormal();
	}
	FVector Up = FVector::CrossProduct(Normal, Right).GetSafeNormal();

	float RightDist = FMath::Abs(FVector::DotProduct(Offset, Right));
	float UpDist = FMath::Abs(FVector::DotProduct(Offset, Up));

	if (Width > 0.0f && RightDist > Width * 0.5f)
	{
		return false;
	}
	if (Height > 0.0f && UpDist > Height * 0.5f)
	{
		return false;
	}

	return true;
}
