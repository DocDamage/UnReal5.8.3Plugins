#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocAcousticTypes.generated.h"

UENUM(BlueprintType)
enum class EDocAcousticPathStatus : uint8
{
	SameSpace UMETA(DisplayName = "Same Space"),
	Connected UMETA(DisplayName = "Connected"),
	/** Both spaces resolved but no admissible route (or the route exceeds MaxHops). Uses the floor policy. */
	Blocked UMETA(DisplayName = "Blocked"),
	/** Reserved: no graph node for a resolved space. */
	NoPath UMETA(DisplayName = "No Path"),
	/** Search stopped at MaxVisitedNodes before the listener space was settled. */
	BudgetExceeded UMETA(DisplayName = "Budget Exceeded"),
	/** Emitter or listener is outside every space and the unresolved policy does not map it. Uses the floor policy. */
	Unresolved UMETA(DisplayName = "Unresolved")
};

UENUM(BlueprintType)
enum class EDocAcousticPortalState : uint8
{
	Open UMETA(DisplayName = "Open"),
	Closed UMETA(DisplayName = "Closed"),
	Obstructed UMETA(DisplayName = "Obstructed")
};

/** What happens when a location is outside every space. */
UENUM(BlueprintType)
enum class EDocAcousticUnresolvedPolicy : uint8
{
	/** Result is Unresolved with the disconnected floor gain and cutoff. */
	UseFloor UMETA(DisplayName = "Use Floor"),
	/** Treat the location as being in ExteriorSpaceId (if that space exists). */
	TreatAsExterior UMETA(DisplayName = "Treat As Exterior")
};

USTRUCT(BlueprintType)
struct DOCACOUSTICSPACESRUNTIME_API FDocAcousticPortalLink
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FName PortalId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FName SpaceA = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FName SpaceB = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Openness = 1.0f;

	/** Transmission when fully closed (Openness 0). 0 = a closed door is a blocked edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinTransmissionGain = 0.05f;

	/** Transmission when fully open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxTransmissionGain = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "20.0", ClampMax = "20000.0"))
	float MinCutoffHz = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics", meta = (ClampMin = "20.0", ClampMax = "20000.0"))
	float MaxCutoffHz = 20000.0f;

	/** A disabled portal is a blocked edge (gain 0), not its closed transmission. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bIsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FVector PortalLocation = FVector::ZeroVector;

	float GetEffectiveGain() const
	{
		if (!bIsEnabled)
		{
			return 0.0f;
		}
		return FMath::Clamp(FMath::Lerp(MinTransmissionGain, MaxTransmissionGain, FMath::Clamp(Openness, 0.0f, 1.0f)), 0.0f, 1.0f);
	}

	float GetEffectiveCutoffHz() const
	{
		return FMath::Lerp(MinCutoffHz, MaxCutoffHz, FMath::Clamp(bIsEnabled ? Openness : 0.0f, 0.0f, 1.0f));
	}

	/** Empty when valid. */
	FString Validate() const;
};

USTRUCT(BlueprintType)
struct DOCACOUSTICSPACESRUNTIME_API FDocAcousticPathQuery
{
	GENERATED_BODY()

	/** Optional. When set, the listener's tracked membership (with hysteresis) is used and updated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FName ListenerId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FVector ListenerLocation = FVector::ZeroVector;

	/** Optional. When set, the emitter's tracked membership (with hysteresis) is used and updated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FName EmitterId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	FVector EmitterLocation = FVector::ZeroVector;

	/** Maximum portals on a path. Longer routes are not admissible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	int32 MaxHops = 16;

	/** Maximum settled (space, hops) states before the search stops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	int32 MaxVisitedNodes = 64;

	/** When the budget is hit: true returns the best tentative route flagged approximate; false fails with BudgetExceeded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acoustics")
	bool bAllowApproximation = false;
};

USTRUCT(BlueprintType)
struct DOCACOUSTICSPACESRUNTIME_API FDocAcousticPathResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	EDocAcousticPathStatus Status = EDocAcousticPathStatus::NoPath;

	/** Portal transmission only (product along the path). Distance attenuation is not included; see README "Composition". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float TransmissionGain = 0.0f;

	/** Most restrictive portal cutoff along the path (authored approximation). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float CutoffFrequencyHz = 20000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TArray<FName> PathPortals;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TArray<FName> PathSpaces;

	/** Per-portal gain stages, same order as PathPortals. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TArray<float> PortalGains;

	/** Per-portal cutoff stages, same order as PathPortals. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	TArray<float> PortalCutoffsHz;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName EmitterId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName ListenerId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName EmitterSpace = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName ListenerSpace = NAME_None;

	/** Route length through authored portal positions (emitter, portals..., listener). Straight line for SameSpace. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float TotalDistance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float PathCost = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	int64 TopologyRevision = 0;

	/** World time the result was computed (or served from cache). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	double QueryTimeSeconds = 0.0;

	/** World time of the listener's last UpdateListener, if tracked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	double ListenerTimeSeconds = 0.0;

	/** Budget-limited: not proven to be the strongest path. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	bool bIsApproximation = false;

	/** Served from the path cache for the same topology revision and memberships. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	bool bFromCache = false;

	/** Stable reason code: SameSpace, Connected, NoRoute, HopLimit, Budget, BudgetApproximate, Unresolved, Exterior. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName Reason = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FString DiagnosticMessage;
};

USTRUCT(BlueprintType)
struct DOCACOUSTICSPACESRUNTIME_API FDocAcousticClaim
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FGuid ClaimId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	FName EmitterId = NAME_None;

	/** Emitter registration generation the claim belongs to. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	int32 EmitterGeneration = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float AppliedGainMultiplier = 1.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	float AppliedCutoffHz = 20000.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Acoustics")
	bool bIsActive = false;
};
