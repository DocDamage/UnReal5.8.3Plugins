#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocOpticalBeamTypes.h"
#include "DocOpticalProfile.h"
#include "DocBeamSurfaceComponent.generated.h"

UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCOPTICALBEAMSRUNTIME_API UDocBeamSurfaceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocBeamSurfaceComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FName SurfaceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bIsEnabled = true;

	/** Null = plain blocker. Change at runtime through UDocOpticalBeamSubsystem::SetSurfaceProfile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	TObjectPtr<UDocOpticalProfile> OpticalProfile;

	/** Authored front normal in actor space. Zero = use the hit normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	FVector SurfaceNormalOverride = FVector::ZeroVector;

	/** One-sided surfaces (false) block beams arriving from behind the front normal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bTwoSided = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface")
	FString LastRegistrationError;

	/** Front normal in world space. Negative-scale owners flip the authored normal consistently. */
	UFUNCTION(BlueprintCallable, Category = "Surface")
	FVector GetSurfaceNormal(const FVector& HitNormal) const;

	/** Blocker when there is no profile or the profile is invalid/unsupported. */
	UFUNCTION(BlueprintCallable, Category = "Surface")
	EDocBeamSurfaceType GetSurfaceType() const;
};
