#include "DocAcousticTypes.h"

FString FDocAcousticPortalLink::Validate() const
{
	if (PortalId.IsNone())
	{
		return TEXT("Portal needs a PortalId.");
	}
	if (SpaceA.IsNone() || SpaceB.IsNone())
	{
		return FString::Printf(TEXT("Portal %s needs two endpoint spaces."), *PortalId.ToString());
	}
	if (SpaceA == SpaceB)
	{
		return FString::Printf(TEXT("Portal %s connects %s to itself."), *PortalId.ToString(), *SpaceA.ToString());
	}
	const float Values[] = { Openness, MinTransmissionGain, MaxTransmissionGain, MinCutoffHz, MaxCutoffHz };
	for (const float V : Values)
	{
		if (!FMath::IsFinite(V))
		{
			return FString::Printf(TEXT("Portal %s has a non-finite value."), *PortalId.ToString());
		}
	}
	if (PortalLocation.ContainsNaN())
	{
		return FString::Printf(TEXT("Portal %s has a non-finite location."), *PortalId.ToString());
	}
	if (MinTransmissionGain < 0.0f || MaxTransmissionGain > 1.0f || MinTransmissionGain > MaxTransmissionGain)
	{
		return FString::Printf(TEXT("Portal %s transmission must satisfy 0 <= Min <= Max <= 1."), *PortalId.ToString());
	}
	if (MinCutoffHz < 20.0f || MaxCutoffHz > 20000.0f || MinCutoffHz > MaxCutoffHz)
	{
		return FString::Printf(TEXT("Portal %s cutoff must satisfy 20 <= Min <= Max <= 20000 Hz."), *PortalId.ToString());
	}
	return FString();
}
