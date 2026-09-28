#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocPuzzleTypes.h"
#include "DocPuzzleComponent.h"
#include "DocPuzzleInputComponent.generated.h"

/** Adapter component for actors acting as physical or logical puzzle input sources (e.g. pressure plates, buttons, levers). */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocPuzzleInputComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FName InputId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid SourceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TWeakObjectPtr<UDocPuzzleComponent> TargetPuzzleComponent;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SendTrigger();

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SetBoolean(bool bActive);

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SetScalar(float Value);

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SetSymbol(FName Symbol);

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult ClearContributor();
};
