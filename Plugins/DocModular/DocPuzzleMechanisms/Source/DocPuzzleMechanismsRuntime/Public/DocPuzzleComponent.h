#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DocPuzzleTypes.h"
#include "DocPuzzleDefinition.h"
#include "DocPuzzleSubsystem.h"
#include "DocPuzzleComponent.generated.h"

/** Component hosting a puzzle instance on an actor. */
UCLASS(ClassGroup = (DocModular), meta = (BlueprintSpawnableComponent))
class DOCPUZZLEMECHANISMSRUNTIME_API UDocPuzzleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDocPuzzleComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FGuid InstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	TObjectPtr<UDocPuzzleDefinition> Definition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	FDocOwnerScope Scope;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	bool bAutoRegisterOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Puzzle")
	bool bAutoStartAttempt = true;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult StartAttempt(FGuid& OutAttemptId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SubmitInput(FName InputId, const FDocPuzzleInputValue& Value, const FGuid& SourceId = FGuid());

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SubmitTrigger(FName InputId, const FGuid& SourceId = FGuid());

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SubmitBoolean(FName InputId, bool bValue, const FGuid& SourceId = FGuid());

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SubmitScalar(FName InputId, float Value, const FGuid& SourceId = FGuid());

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SubmitSymbol(FName InputId, FName Symbol, const FGuid& SourceId = FGuid());

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult SetContributor(FName InputId, const FDocPuzzleInputValue& Value, const FGuid& SourceId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult RemoveContributor(FName InputId, const FGuid& SourceId);

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult RequestReset();

	UFUNCTION(BlueprintCallable, Category = "Doc|Puzzle")
	FDocSystemResult GetEvaluation(FDocPuzzleEvaluation& OutEvaluation) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Puzzle")
	EDocPuzzleState GetState() const;

	UFUNCTION(BlueprintPure, Category = "Doc|Puzzle")
	bool IsSolved() const;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleInputAcceptedSignature OnInputAccepted;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleProgressChangedSignature OnProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleAttemptFailedSignature OnAttemptFailed;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleSolvedSignature OnPuzzleSolved;

	UPROPERTY(BlueprintAssignable, Category = "Doc|Puzzle|Events")
	FDocPuzzleResetCommittedSignature OnResetCommitted;

	FDocPuzzleSolvedNative OnPuzzleSolvedNative;
	FDocPuzzleProgressChangedNative OnProgressChangedNative;

protected:
	UDocPuzzleSubsystem* GetSubsystem() const;

private:
	UFUNCTION()
	void HandleSubsystemInputAccepted(const FGuid& InInstanceId, const FDocPuzzleInputEvent& Event);

	UFUNCTION()
	void HandleSubsystemProgressChanged(const FGuid& InInstanceId, const FDocPuzzleEvaluation& Evaluation);

	UFUNCTION()
	void HandleSubsystemAttemptFailed(const FGuid& InInstanceId, const FGuid& AttemptId);

	UFUNCTION()
	void HandleSubsystemPuzzleSolved(const FGuid& InInstanceId, const FGuid& AttemptId);

	UFUNCTION()
	void HandleSubsystemResetCommitted(const FGuid& InInstanceId, int32 NewResetEpoch);

	int64 LocalSequenceCounter = 0;
};
