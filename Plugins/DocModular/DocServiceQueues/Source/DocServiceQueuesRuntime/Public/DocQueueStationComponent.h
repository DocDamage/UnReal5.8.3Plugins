#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocServiceQueueTypes.h"
#include "DocQueueStationComponent.generated.h"

class UDocServiceQueueSubsystem;

/** Component representing an in-world service station with limited capacity. */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCSERVICEQUEUESRUNTIME_API UDocQueueStationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocQueueStationComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FName StationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue", meta = (ClampMin = "1"))
	int32 Capacity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGameplayTagContainer ServiceTags;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult CreateOffer(FName QueueId, FDocAdmissionOffer& OutOffer);

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult StartService(const FGuid& TicketId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult CompleteService(const FGuid& TicketId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult SetOperationalState(EDocStationOperationalState NewState);

protected:
	UDocServiceQueueSubsystem* GetSubsystem() const;
};
