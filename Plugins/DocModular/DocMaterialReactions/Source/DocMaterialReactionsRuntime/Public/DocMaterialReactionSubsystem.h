#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocSystemResult.h"
#include "DocMaterialReactionTypes.h"
#include "DocReactiveMaterialComponent.h"
#include "DocMaterialReactionSubsystem.generated.h"

/**
 * Steps registered reactive objects on a fixed simulation clock and runs budgeted, stateful propagation.
 * Objects are addressed by ReactiveObjectId; an unloaded (unregistered) object is NotFound, never mutated
 * through a stale reference.
 */
UCLASS()
class DOCMATERIALREACTIONSRUNTIME_API UDocMaterialReactionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UDocMaterialReactionSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** When true, Tick advances the simulation in FixedStepSeconds steps. Tests and custom drivers call StepSimulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bAutoStep = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.01"))
	float FixedStepSeconds = 0.1f;

	/** Steps per frame; time beyond this is dropped and counted (GetDroppedSimulationSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "1"))
	int32 MaxStepsPerTick = 4;

	// Registration --------------------------------------------------------------------------

	/** Registers a component; assigns ReactiveObjectId if None. Conflict if another live component holds the id. */
	UFUNCTION(BlueprintCallable, Category = "Registration")
	FDocSystemResult RegisterReactiveObject(UDocReactiveMaterialComponent* Comp);

	/** Compatibility wrapper for RegisterReactiveObject. */
	UFUNCTION(BlueprintCallable, Category = "Registration")
	void RegisterReactiveComponent(UDocReactiveMaterialComponent* Comp);

	/** Unregisters, removes its contacts, and withdraws the propagation exposure it was providing to others. */
	UFUNCTION(BlueprintCallable, Category = "Registration")
	void UnregisterReactiveComponent(UDocReactiveMaterialComponent* Comp);

	UFUNCTION(BlueprintCallable, Category = "Registration")
	UDocReactiveMaterialComponent* FindReactiveObject(FName ObjectId) const;

	UFUNCTION(BlueprintCallable, Category = "Queries")
	int32 GetRegisteredComponentCount() const;

	// Gameplay API by id ---------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Exposure")
	FDocSystemResult AddExposureSource(FName ObjectId, const FDocExposureSample& Sample);

	UFUNCTION(BlueprintCallable, Category = "Exposure")
	FDocSystemResult UpdateExposureSource(FName ObjectId, FName SourceId, float NewIntensity);

	UFUNCTION(BlueprintCallable, Category = "Exposure")
	FDocSystemResult RemoveExposureSource(FName ObjectId, FName SourceId);

	UFUNCTION(BlueprintCallable, Category = "Reaction")
	FDocSystemResult RequestReaction(FName ObjectId, FName ReactionId, FName Cause);

	UFUNCTION(BlueprintCallable, Category = "Reaction")
	FDocSystemResult SuppressReaction(FName ObjectId, FName ReactionId, FName SourceId, float DurationSeconds = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Queries")
	bool QueryMaterialState(FName ObjectId, FDocMaterialState& OutState) const;

	UFUNCTION(BlueprintCallable, Category = "Queries")
	TArray<FDocReactionInstance> QueryActiveReactions(FName ObjectId) const;

	// Contacts and propagation ----------------------------------------------------------------

	/** Explicit logical contact (symmetric). Contacts are candidates regardless of radius, still within budget. */
	UFUNCTION(BlueprintCallable, Category = "Propagation")
	FDocSystemResult AddContact(FName ObjectA, FName ObjectB);

	UFUNCTION(BlueprintCallable, Category = "Propagation")
	bool RemoveContact(FName ObjectA, FName ObjectB);

	UFUNCTION(BlueprintCallable, Category = "Propagation")
	int32 GetContactCount() const { return Contacts.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Budget")
	void SetPropagationBudget(const FDocPropagationBudget& NewBudget);

	UFUNCTION(BlueprintCallable, Category = "Budget")
	FDocPropagationBudget GetPropagationBudget() const { return PropagationBudget; }

	UFUNCTION(BlueprintCallable, Category = "Budget")
	FDocPropagationStats GetLastPropagationStats() const { return LastStats; }

	// Simulation -------------------------------------------------------------------------------

	/** Advances the simulation clock by DeltaTime, steps every object in id order, then propagates. */
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void StepSimulation(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	double GetSimulationTime() const { return SimTime; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	double GetDroppedSimulationSeconds() const { return DroppedSeconds; }

	// Persistence ------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Persistence")
	bool CaptureMaterialState(const UDocReactiveMaterialComponent* Comp, FDocMaterialSnapshot& OutSnapshot) const;

	/** Validates the whole snapshot before applying; emits only a "Restored" state change, no reaction starts. */
	UFUNCTION(BlueprintCallable, Category = "Persistence")
	FDocSystemResult StageRestore(UDocReactiveMaterialComponent* Comp, const FDocMaterialSnapshot& Snapshot);

private:
	UPROPERTY()
	TMap<FName, TWeakObjectPtr<UDocReactiveMaterialComponent>> Objects;

	UPROPERTY()
	FDocPropagationBudget PropagationBudget;

	UPROPERTY()
	FDocPropagationStats LastStats;

	/** Normalized pairs (lexically smaller id first). */
	TSet<TPair<FName, FName>> Contacts;

	/** "source|target|reaction" -> last refresh time, for cooldown. */
	TMap<FString, double> LastTransferTime;

	double SimTime = 0.0;
	double Accumulator = 0.0;
	double DroppedSeconds = 0.0;
	int32 PropagationCursor = 0;
	int32 StepCounter = 0;

	void CleanupStaleReferences();
	TArray<FName> GetSortedIds() const;
	void ProcessPropagation(float DeltaTime);
	static FString MakePropagationSourcePrefix(FName SourceObjectId);
	static FDocSystemResult FromReject(const UDocReactiveMaterialComponent* Comp);
};
