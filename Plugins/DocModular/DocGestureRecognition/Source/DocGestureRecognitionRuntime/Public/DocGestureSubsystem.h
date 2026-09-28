#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "DocSystemResult.h"
#include "DocGestureTypes.h"
#include "DocGestureTemplate.h"
#include "DocGestureSubsystem.generated.h"

/** The single validated consumer path. Gameplay code decides (with its own permissions) what the intent does. */
DECLARE_MULTICAST_DELEGATE_OneParam(FDocGestureDispatchedEvent, const FDocGestureRecognitionResult&);

/**
 * Per-player single-stroke gesture capture and recognition. Each player has its own compiled model, generation
 * counter and capture state. Recognition returns data only; DispatchResult is the only way a result reaches
 * the consumer, and it refuses stale generations, swapped models and repeats.
 */
UCLASS(BlueprintType)
class DOCGESTURERECOGNITIONRUNTIME_API UDocGestureSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 HardMaxPointsPerStroke = 5000;

	virtual void Deinitialize() override;

	// Templates

	/** Validates and compiles an immutable snapshot of the set. Strokes already in capture keep the model they began with. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocSystemResult RegisterTemplateSet(UDocGestureTemplateSet* InTemplateSet);

	UFUNCTION(BlueprintPure, Category = "Doc|Gesture")
	UDocGestureTemplateSet* GetActiveTemplateSet() const { return ActiveTemplateSet; }

	UFUNCTION(BlueprintPure, Category = "Doc|Gesture")
	FString GetActiveModelHash() const;

	// Capture

	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocSystemResult SetCaptureSettings(const FDocGestureCaptureSettings& InSettings);

	/** Starts a new gesture (new generation): any older result can no longer be dispatched. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocSystemResult BeginStroke(const FDocOwnerScope& InOwner, EDocGestureInputSource InputSource = EDocGestureInputSource::Mouse);

	/** Appends plane-space points. Non-finite points or negative time are refused; exceeding bounds cancels the stroke. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocSystemResult AppendPoints(const TArray<FVector2D>& NewPoints, float DeltaSeconds = 0.0f);

	/** Integrates an analog-stick deflection (dead zone + speed) into a coarse sample. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocSystemResult AppendAnalogSample(FVector2D StickDeflection, float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocSystemResult EndStroke(FDocGestureStroke& OutStroke);

	/** Cancels capture and invalidates pending results (new generation). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocSystemResult CancelStroke();

	/** Device change during capture cancels the stroke. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	void NotifyInputDeviceChanged();

	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	void InvalidateCurrentSession();

	UFUNCTION(BlueprintPure, Category = "Doc|Gesture")
	const FDocGestureStroke& GetActiveStroke() const { return ActiveStroke; }

	UFUNCTION(BlueprintPure, Category = "Doc|Gesture")
	bool IsStrokeActive() const { return bStrokeActive; }

	UFUNCTION(BlueprintPure, Category = "Doc|Gesture")
	int32 GetCurrentGeneration() const { return CurrentGeneration; }

	// Recognition

	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocGestureRecognitionResult RecognizeStroke(const FDocGestureStroke& Stroke);

	/** Scores every template (no acceptance gating). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	TArray<FDocGestureCandidate> QueryCandidates(const FDocGestureStroke& Stroke);

	/** Button/menu route producing the same semantic intent, labelled AccessibleAlternative. Starts a new generation. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocGestureRecognitionResult SelectAccessibleAlternative(FName GestureId, const FDocOwnerScope& InOwner);

	/** Delivers a recognized result to OnGestureDispatched once, if it is still current. Consumes the generation. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Gesture")
	FDocSystemResult DispatchResult(const FDocGestureRecognitionResult& Result);

	FDocGestureDispatchedEvent OnGestureDispatched;

	// Limits (the effective point limit never exceeds HardMaxPointsPerStroke)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Gesture")
	int32 MaxPointsPerStroke = 500;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Gesture")
	float MaxStrokeDurationSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Gesture")
	float MinStrokePathLength = 10.0f;

	int32 GetEffectiveMaxPoints() const { return FMath::Clamp(MaxPointsPerStroke, 2, HardMaxPointsPerStroke); }

private:
	struct FModel
	{
		FName SetId;
		int32 SetVersion = 0;
		FString ModelHash;
		TArray<FDocGestureCompiledTemplate> Templates;

		const FDocGestureCompiledTemplate* Find(FName GestureId) const
		{
			return Templates.FindByPredicate([GestureId](const FDocGestureCompiledTemplate& T) { return T.GestureId == GestureId; });
		}
	};

	TSharedPtr<const FModel> FindModelForSession(const FGuid& SessionId) const;
	void FinishStroke(bool bCancelled);
	void FillModelFields(const FModel& Model, FDocGestureRecognitionResult& Result) const;
	TArray<FDocGestureCandidate> ScoreAll(const FModel& Model, const FDocGestureStroke& Stroke, int32& OutWork) const;

	UPROPERTY()
	TObjectPtr<UDocGestureTemplateSet> ActiveTemplateSet;

	UPROPERTY()
	FDocGestureStroke ActiveStroke;

	UPROPERTY()
	FDocGestureStroke EndedStroke;

	FDocGestureCaptureSettings CaptureSettings;
	TSharedPtr<const FModel> ActiveModel;
	TSharedPtr<const FModel> ActiveStrokeModel;
	TMap<FString, FDocGestureCompiledTemplate> CompiledCache;
	TArray<TPair<FGuid, TSharedPtr<const FModel>>> RecentSessions;
	TArray<FGuid> DispatchedRequests;
	FVector2D AnalogCursor = FVector2D::ZeroVector;
	int32 CurrentGeneration = 1;
	bool bStrokeActive = false;
	bool bHasEndedStroke = false;
};
