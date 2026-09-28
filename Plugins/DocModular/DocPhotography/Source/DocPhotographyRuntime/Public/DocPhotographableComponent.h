#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DocOwnerScope.h"
#include "DocPhotoTypes.h"
#include "DocPhotographableComponent.generated.h"

/** A photographable subject. Registers with its world's subject registry (never a process-global list). */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotographableComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDocPhotographableComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FName SubjectId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FGameplayTag SubjectTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	int32 SubjectVersion = 1;

	/** Authored half extents around the component (component space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FVector BoundsExtent = FVector(50.0f, 50.0f, 50.0f);

	/** Visibility sample points (component space). Capped by the gallery quota. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	TArray<FVector> RelativeSamplePoints;

	/** Private subjects are only observed in photos taken by OwnerScope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	bool bIsPrivate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photography")
	FDocOwnerScope OwnerScope;

	UFUNCTION(BlueprintCallable, Category = "Photography")
	TArray<FVector> GetWorldSamplePoints() const;

	/** The 8 corners of the authored bounds in world space. */
	TArray<FVector> GetWorldBoundsCorners() const;
};

/** Per-world subject registry. */
UCLASS()
class DOCPHOTOGRAPHYRUNTIME_API UDocPhotoSubjectRegistry : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterSubject(UDocPhotographableComponent* Subject);
	void UnregisterSubject(UDocPhotographableComponent* Subject);

	/** Live subjects sorted by SubjectId (then object name). */
	TArray<UDocPhotographableComponent*> GetSubjectsSorted() const;

	int32 GetSubjectCount() const;
	int64 GetRevision() const { return Revision; }

private:
	TArray<TWeakObjectPtr<UDocPhotographableComponent>> Subjects;
	int64 Revision = 1;
};
