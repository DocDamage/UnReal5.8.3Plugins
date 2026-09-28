#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DocGhostSurrogateActor.generated.h"

/**
 * Visual-only ghost proxy. Collision, overlaps, damage, replication, navigation and ticking are disabled by
 * construction and re-enforced on every component whenever visuals are attached (EnforceIsolation).
 */
UCLASS()
class DOCREPLAYGHOSTSRUNTIME_API ADocGhostSurrogateActor : public AActor
{
	GENERATED_BODY()

public:
	ADocGhostSurrogateActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ghost")
	TObjectPtr<USceneComponent> SceneRoot;

	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void UpdateGhostTransform(const FTransform& InTransform);

	/** Strips every live-world capability from all components (collision, overlaps, ticking, animation). */
	UFUNCTION(BlueprintCallable, Category = "Ghost")
	void EnforceIsolation();

	UFUNCTION(BlueprintPure, Category = "Ghost")
	bool HasAnyCollision() const;

	UFUNCTION(BlueprintPure, Category = "Ghost")
	bool HasAnyOverlapEvents() const;

	UFUNCTION(BlueprintPure, Category = "Ghost")
	bool HasAnyTickingComponent() const;

	UFUNCTION(BlueprintPure, Category = "Ghost")
	bool CanAffectLiveWorld() const;

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override { return 0.0f; }
};
