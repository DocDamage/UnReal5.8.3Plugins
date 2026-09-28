#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocOpticalBeamTypes.h"
#include "DocBeamPresentationComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * Generic, authority-free beam presentation. Rebuilds a pooled set of segment transforms only when the emitter's
 * committed path changes (never per frame, never a component per segment). With BeamMesh set, one owned instanced
 * mesh component shows one instance per segment; without it, SegmentTransforms is the data for any renderer.
 */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCOPTICALBEAMSRUNTIME_API UDocBeamPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocBeamPresentationComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** Emitter to follow. None = the first beam emitter on the owner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation")
	FName EmitterId = NAME_None;

	/** Optional mesh (unit length along X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation")
	TObjectPtr<UStaticMesh> BeamMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presentation", meta = (ClampMin = "0.01"))
	float BeamThickness = 0.05f;

	/** One transform per segment: located at the start, rotated along the segment, X scaled to its length. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Presentation")
	TArray<FTransform> SegmentTransforms;

	/** Channel per segment, for labels/shapes (color alone is not the gameplay identity). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Presentation")
	TArray<FGameplayTag> SegmentChannels;

	/** Number of rebuilds; stays constant while the path does not change. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Presentation")
	int32 RebuildCount = 0;

	void ApplyPath(const FDocBeamPath& Path);

private:
	void HandlePathUpdated(FName InEmitterId, const FDocBeamPath& Path);

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> InstanceComponent;

	FDelegateHandle PathHandle;
};
