#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "DocEventsSettings.generated.h"

class UScriptStruct;

/** Project Settings → Plugins → Doc Events. All limits are per world. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Events"))
class DOCEVENTSRUNTIME_API UDocEventsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Maximum events dispatched in one drain. Remaining events are deferred to the next tick (counted as Deferred). */
	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "1"))
	int32 MaxEventsPerDrain = 1000;

	/** Maximum queued events. Broadcasts beyond this are dropped with a warning (counted as Dropped). */
	UPROPERTY(Config, EditAnywhere, Category = "Limits", meta = (ClampMin = "1"))
	int32 MaxQueuedEvents = 4096;

	/** Maximum retained (tag + scope) values. Oldest-updated entries are evicted. */
	UPROPERTY(Config, EditAnywhere, Category = "Retention", meta = (ClampMin = "0"))
	int32 MaxRetainedValues = 512;

	/** Retained values older than this (world seconds) are discarded. 0 = no TTL. */
	UPROPERTY(Config, EditAnywhere, Category = "Retention", meta = (ClampMin = "0.0"))
	float RetainedTimeToLiveSeconds = 0.f;

	/** Broadcasts of these tags (or children) also update the retained value for their exact tag + scope. */
	UPROPERTY(Config, EditAnywhere, Category = "Retention")
	FGameplayTagContainer AutoRetainTags;

	/** Size of the occurrence history ring used by HasEventOccurred. 0 disables history (always Unknown). */
	UPROPERTY(Config, EditAnywhere, Category = "History", meta = (ClampMin = "0"))
	int32 HistoryCapacity = 1024;

	/** Only these tags (or children) are recorded in history. Empty = record every tag. */
	UPROPERTY(Config, EditAnywhere, Category = "History")
	FGameplayTagContainer HistoryTrackedTags;

	/** Expected payload struct per exact event tag. A broadcast with a different payload type is rejected (InvalidInput). */
	UPROPERTY(Config, EditAnywhere, Category = "Schemas")
	TMap<FGameplayTag, TSoftObjectPtr<UScriptStruct>> PayloadSchemas;

	/** Capture a bounded debug ring of dispatched events (no payload contents). */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bCaptureDebugHistory = false;

	UPROPERTY(Config, EditAnywhere, Category = "Debug", meta = (ClampMin = "1"))
	int32 DebugHistoryCapacity = 256;
};
