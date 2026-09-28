#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocInteractionTypes.h"
#include "DocInteractionSubsystem.generated.h"

class UDocInteractableComponent;
class UDocInteractorComponent;

/**
 * Coordinates interaction sessions for one world (handoff 5.4).
 *
 * Session lifecycle:
 *   Request → Validate (definition, enabled, authority, range, conditions)
 *           → Reserve (concurrency policy, one session per interactor)
 *           → Instant: Commit | Hold/Continuous/Repeated: Running (ticked on the
 *             gameplay clock; pauses with the world)
 *           → Commit: revalidate, execute actions fail-fast, optional compensation
 *           → exactly one terminal state: Completed / Cancelled / Failed / Rejected.
 *
 * The subsystem owns no "global focused actor": focus is per interactor component.
 * Cooldown and one-time-use records are runtime state here, never on shared definitions.
 */
UCLASS()
class DOCINTERACTIONRUNTIME_API UDocInteractionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDocInteractionSubsystem* Get(const UObject* WorldContextObject);

	// ---- Registry ----
	void RegisterInteractable(UDocInteractableComponent* Interactable);
	void UnregisterInteractable(UDocInteractableComponent* Interactable);
	void GetInteractablesInRadius(const FVector& Center, float Radius, TArray<UDocInteractableComponent*>& Out) const;

	// ---- Queries (side-effect free) ----

	/** Evaluate one definition for an interactor without starting anything. */
	FDocInteractionOption EvaluateOption(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target, const FDocInteractionDefinition& Definition) const;

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	TArray<FDocInteractionOption> GetAvailableInteractions(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Interaction")
	bool GetSessionInfo(const FDocRequestHandle& Session, FDocInteractionSessionInfo& OutInfo) const;

	// ---- Commands ----

	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	FDocInteractionSessionInfo RequestInteraction(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target, FName DefinitionId);

	/** Idempotent. Cancelling a finished session returns NoChange. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	FDocSystemResult CancelInteraction(const FDocRequestHandle& Session);

	/**
	 * Finish a running Continuous/Repeated session as Completed (e.g. input released).
	 * For HoldToComplete this cancels (the hold was not completed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	FDocSystemResult CompleteInteraction(const FDocRequestHandle& Session);

	// ---- Runtime use records (read by built-in conditions) ----
	double GetLastCompletionTime(const UDocInteractableComponent* Target, FName DefinitionId, const AActor* InteractorOrNull) const;
	bool HasCompleted(const UDocInteractableComponent* Target, FName DefinitionId, const AActor* InteractorOrNull) const;

	/** Clear use records (e.g. after a project resets a puzzle). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Interaction")
	void ResetUseRecords(UDocInteractableComponent* Target);

	/** World gameplay seconds. */
	double GetNow() const;

	int32 GetActiveSessionCount() const { return Sessions.Num(); }

	/** Fires for every terminal session (after the interactor component's delegates). */
	FDocInteractionSessionNativeEvent OnSessionEndedNative;

	/** Advance running sessions by DeltaSeconds of gameplay time (Tick calls this; tests call it directly). */
	void AdvanceSessions(float DeltaSeconds);

	//~ UWorldSubsystem / FTickableGameObject
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FSession
	{
		FDocInteractionSessionInfo Info;
		TWeakObjectPtr<UDocInteractorComponent> Interactor;
		TWeakObjectPtr<UDocInteractableComponent> Target;
		float TimeSinceRevalidate = 0.f;
		float TimeSinceRepeat = 0.f;
	};

	struct FUseKey
	{
		TWeakObjectPtr<const UDocInteractableComponent> Target;
		FName DefinitionId;
		TWeakObjectPtr<const AActor> Interactor; // null for global scope

		friend bool operator==(const FUseKey& A, const FUseKey& B)
		{
			return A.Target == B.Target && A.DefinitionId == B.DefinitionId && A.Interactor == B.Interactor;
		}
		friend uint32 GetTypeHash(const FUseKey& K)
		{
			return HashCombine(HashCombine(GetTypeHash(K.Target), GetTypeHash(K.DefinitionId)), GetTypeHash(K.Interactor));
		}
	};

	FDocInteractionContext MakeContext(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target, const FDocInteractionDefinition& Definition, const FDocRequestHandle& Session) const;

	/** Range + conditions. bRunningCheck limits conditions to those with bRevalidateWhileRunning. */
	FDocConditionResult Validate(UDocInteractorComponent* Interactor, UDocInteractableComponent* Target, const FDocInteractionDefinition& Definition, bool bRunningCheck) const;

	/** Returns the conflicting session if Definition's concurrency blocks a new session. */
	FDocRequestHandle FindReservationConflict(const UDocInteractorComponent* Interactor, const UDocInteractableComponent* Target, const FDocInteractionDefinition& Definition) const;

	/** Execute actions; fills ExecutedActions/CompensatedActions. Returns the action outcome. */
	FDocSystemResult ExecuteActions(FSession& Session, const FDocInteractionDefinition& Definition);

	void FinishSession(const FDocRequestHandle& Handle, EDocInteractionSessionState State, const FDocSystemResult& Result);
	void RecordUse(const FSession& Session);
	void NotifyReceiver(AActor* Actor, const FDocInteractionContext& Context, FGameplayTag Phase) const;

	TArray<TWeakObjectPtr<UDocInteractableComponent>> Interactables;
	TDocHandleTable<FSession> Sessions;
	TMap<FUseKey, double> LastCompletion;
	bool bShuttingDown = false;
};
