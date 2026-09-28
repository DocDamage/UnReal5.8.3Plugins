#include "DocGestureTypes.h"

float FDocGestureStroke::CalculatePathLength() const
{
	double TotalLength = 0.0;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		TotalLength += FVector2D::Distance(Points[Index - 1], Points[Index]);
	}
	return static_cast<float>(TotalLength);
}

bool FDocGestureStroke::HasNonFinitePoints() const
{
	for (const FVector2D& Pt : Points)
	{
		if (!FMath::IsFinite(Pt.X) || !FMath::IsFinite(Pt.Y))
		{
			return true;
		}
	}
	return false;
}

bool FDocGestureStroke::IsDegenerate(float MinLength) const
{
	if (Points.Num() < 2 || HasNonFinitePoints())
	{
		return true;
	}
	const float Length = CalculatePathLength();
	return !FMath::IsFinite(Length) || Length < FMath::Max(MinLength, KINDA_SMALL_NUMBER);
}
