#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DocServiceQueueTypes.h"
#include "DocServiceQueueDefinition.generated.h"

/** Authored definition of a service queue's ordering, fairness, and timeout policies. */
UCLASS(BlueprintType)
class DOCSERVICEQUEUESRUNTIME_API UDocServiceQueueDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue")
	FName QueueId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue")
	EDocQueueOrderingPolicy OrderingPolicy = EDocQueueOrderingPolicy::StrictFIFO;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue", meta = (ClampMin = "0.5"))
	float OfferTimeoutSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue", meta = (ClampMin = "0.5"))
	float ArrivalTimeoutSeconds = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue", meta = (ClampMin = "1"))
	int32 MaxBypassCount = 3;

	/** FirstFit: a ticket waiting at least this long can no longer be bypassed (0 = no age limit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue", meta = (ClampMin = "0.0"))
	float MaxBypassAgeSeconds = 0.0f;

	/** FormBatch: minimum seats for a partial batch (a full station always starts). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue", meta = (ClampMin = "1"))
	int32 MinBatchSize = 1;

	/** FormBatch: once the oldest selected ticket has waited this long, a batch below the minimum may start (0 = never). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue", meta = (ClampMin = "0.0"))
	float BatchWaitSeconds = 0.0f;

	/** Highest priority a participant may request for itself; anything above is clamped. Use SetTicketPriority to grant more. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue", meta = (ClampMin = "0"))
	int32 MaxSelfAssignedPriority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Doc|Queue")
	bool bAutoAdvanceOnArrival = false;
};
