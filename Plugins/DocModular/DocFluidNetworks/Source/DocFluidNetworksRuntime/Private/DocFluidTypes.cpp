#include "DocFluidTypes.h"

bool FDocFluidLedger::VerifyConservation(double InitialTotal, double FinalTotal, double Tolerance) const
{
	const double Expected = InitialTotal + TotalExternalInflow - TotalExternalOutflow - TotalLeaks - TotalDrains - TotalDiscarded - NetQuarantined;
	return FMath::IsNearlyEqual(FinalTotal, Expected, Tolerance);
}
