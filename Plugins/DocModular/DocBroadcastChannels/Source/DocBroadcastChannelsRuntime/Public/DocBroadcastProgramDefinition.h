#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.h"
#include "DocBroadcastProgramDefinition.generated.h"

/** A finite, seekable program. Unknown durations or unseekable sources fail validation (spec 14.2). */
UCLASS(BlueprintType)
class DOCBROADCASTCHANNELSRUNTIME_API UDocBroadcastProgramDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	FName ProgramId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	double DurationSeconds = 60.0;

	/** False when the author cannot vouch for the duration (procedural/streamed sounds). Such programs are rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	bool bKnownDuration = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	bool bCanSeek = true;

	/** Soft reference to the bundled media, resolved by the playback provider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	FSoftObjectPath MediaSource;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	float AuthoredVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Broadcast")
	FGameplayTagContainer ProgramTags;

	/** Checks the authored program. bRequireSeek is true for scheduled programs (late join needs seeking). */
	FDocSystemResult ValidateProgram(bool bRequireSeek) const;
};
