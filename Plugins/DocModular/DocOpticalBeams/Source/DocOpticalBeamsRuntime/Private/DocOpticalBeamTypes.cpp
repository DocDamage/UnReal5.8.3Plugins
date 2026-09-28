#include "DocOpticalBeamTypes.h"

namespace DocOpticalMath
{
	FVector ComputeReflection(const FVector& InDirection, const FVector& InNormal)
	{
		if (InDirection.ContainsNaN() || InNormal.ContainsNaN() || InDirection.IsNearlyZero() || InNormal.IsNearlyZero())
		{
			return FVector::ZeroVector;
		}

		const FVector NormD = InDirection.GetSafeNormal();
		const FVector NormN = InNormal.GetSafeNormal();

		// r = d - 2 * (d . n) * n  (symmetric in the sign of n)
		const double Dot = FVector::DotProduct(NormD, NormN);
		return (NormD - 2.0 * Dot * NormN).GetSafeNormal();
	}
}
