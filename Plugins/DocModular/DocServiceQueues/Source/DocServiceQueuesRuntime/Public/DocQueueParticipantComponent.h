#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocServiceQueueTypes.h"
#include "DocQueueParticipantComponent.generated.h"

class UDocServiceQueueSubsystem;

/** Component on an actor participating in service queues. */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCSERVICEQUEUESRUNTIME_API UDocQueueParticipantComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocQueueParticipantComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Queue")
	FGuid ParticipantId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Doc|Queue")
	FDocQueueTicket CurrentTicket;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult JoinQueue(FName QueueId, int32 PartySize = 1, int32 PriorityClass = 0);

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult LeaveQueue();

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult AcceptCurrentOffer();

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult DeclineCurrentOffer();

	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	FDocSystemResult ConfirmArrival();

	/** True while the subsystem's copy of the ticket is not terminal. */
	UFUNCTION(BlueprintPure, Category = "Doc|Queue")
	bool HasActiveTicket() const;

	/** Copies the authoritative ticket (state, offer, station) from the subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Queue")
	bool RefreshTicket();

protected:
	UDocServiceQueueSubsystem* GetSubsystem() const;
};
