#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "DocSystemResult.h"
#include "DocEvidenceTypes.h"
#include "DocEvidenceDefinition.h"
#include "DocEvidenceSubsystem.generated.h"

/**
 * Owner-scoped evidence and deduction. Definitions are global; every investigation (observations, links, conclusions,
 * receipts) is partitioned by OwnerId (NAME_None = the default single-player scope).
 *
 * Audience rule: ordinary queries and conclusion commits evaluate only evidence the player audience can see
 * (not bIsHiddenFromPlayer, not a secret definition). Hidden-solution hypotheses are never returned by ordinary queries.
 */
UCLASS()
class DOCEVIDENCEDEDUCTIONRUNTIME_API UDocEvidenceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static constexpr int32 MaxInferenceDepth = 16;
	static constexpr int32 MaxDependenciesPerHypothesis = 32;
	static constexpr int32 MaxObservationHistory = 16;

	// Definitions ---------------------------------------------------------------------------------

	FDocSystemResult RegisterEvidenceDefinition(UDocEvidenceDefinition* Def);

	/** Refuses cycles and oversized/too-deep dependency graphs at registration, with a path diagnostic. */
	FDocSystemResult RegisterHypothesisDefinition(UDocHypothesisDefinition* Def);

	// Observations ---------------------------------------------------------------------------------

	/** Duplicate ObservationId: NoChange if identical provenance, else Conflict. Same ProvenanceKey for the same evidence: NoChange. */
	FDocSystemResult AddObservation(const FDocEvidenceObservation& Observation, FName OwnerId = NAME_None);

	/** New revision of the content/time; status is unchanged (a claim stays a claim). History is kept. */
	FDocSystemResult ReviseObservation(FName ObservationId, const FString& NewClaim, EDocEvidenceStatus NewStatus, double NewTimestamp = -1.0, FName OwnerId = NAME_None);

	/** Explicit promotion of a claim to an established fact, recorded with the corroborating source. */
	FDocSystemResult EstablishClaim(FName ObservationId, FName CorroboratingSourceId, FName OwnerId = NAME_None);

	FDocSystemResult RetractObservation(FName ObservationId, FName OwnerId = NAME_None);
	bool HasObservation(FName ObservationId, FName OwnerId = NAME_None) const;
	bool GetObservation(FName ObservationId, FDocEvidenceObservation& OutObs, FName OwnerId = NAME_None) const;

	// Interpretation links -----------------------------------------------------------------------------

	FDocSystemResult AddInterpretationLink(const FDocEvidenceLink& Link, FName OwnerId = NAME_None);
	FDocSystemResult RemoveInterpretationLink(FName LinkId, FName OwnerId = NAME_None);

	// Queries (audience-filtered) --------------------------------------------------------------------------

	/** Hidden solutions: PermissionDenied with an empty payload. Evaluates over visible evidence only. */
	FDocSystemResult EvaluateHypothesis(FName HypothesisId, FDocDeductionResult& OutResult, FName OwnerId = NAME_None) const;

	/** Same audience rules. bIncludeSecretSolutions is honored only when debug explanations are authorized. */
	FDocSystemResult QueryExplanation(FName HypothesisId, bool bIncludeSecretSolutions, FDocDeductionResult& OutResult, FName OwnerId = NAME_None) const;

	/** Non-hidden hypotheses, sorted, with their audience evaluation. */
	TArray<FDocDeductionResult> QueryAvailableHypotheses(FName OwnerId = NAME_None) const;

	/** Development-only full explanation (all evidence, missing ids). Unsupported in Shipping or when not authorized. */
	FDocSystemResult QueryDebugExplanation(FName HypothesisId, FDocDeductionResult& OutResult, FName OwnerId = NAME_None) const;

	/** Must be set explicitly by a development tool; never on by default. */
	bool bAuthorizeDebugExplanations = false;

	/** Typed contradictions among visible established observations. */
	TArray<FDocContradiction> QueryContradictions(FName OwnerId = NAME_None) const;

	// Conclusions ----------------------------------------------------------------------------------

	/**
	 * Separate from evaluation. Validates the owner's revision (Conflict when stale), id/receipt idempotency,
	 * the exclusive group, and audience support. The record commits before the event fires.
	 */
	FDocSystemResult CommitConclusion(FName ConclusionId, FName HypothesisId, int64 ExpectedRevision, FName ReceiptKey, FName OwnerId = NAME_None);
	bool IsConclusionCommitted(FName ConclusionId, FName OwnerId = NAME_None) const;
	bool IsConclusionChallenged(FName ConclusionId, FName OwnerId = NAME_None) const;
	bool GetConclusion(FName ConclusionId, FDocConclusionRecord& OutRecord, FName OwnerId = NAME_None) const;
	bool HasReceipt(FName ReceiptKey, FName OwnerId = NAME_None) const;

	// Persistence ------------------------------------------------------------------------------------

	int64 GetEvidenceRevision(FName OwnerId = NAME_None) const;
	FDocEvidenceSnapshot CaptureState(FName OwnerId = NAME_None) const;

	/** Validates first; replaces the snapshot owner's scope; quarantines conclusions for removed definitions; no events. */
	FDocSystemResult RestoreState(const FDocEvidenceSnapshot& Snapshot);

	// Events ---------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Evidence|Events")
	FDocOnObservationAdded OnObservationAdded;
	FDocOnObservationAddedNative OnObservationAddedNative;

	UPROPERTY(BlueprintAssignable, Category = "Evidence|Events")
	FDocOnObservationRevised OnObservationRevised;
	FDocOnObservationRevisedNative OnObservationRevisedNative;

	UPROPERTY(BlueprintAssignable, Category = "Evidence|Events")
	FDocOnObservationRetracted OnObservationRetracted;
	FDocOnObservationRetractedNative OnObservationRetractedNative;

	UPROPERTY(BlueprintAssignable, Category = "Evidence|Events")
	FDocOnConclusionCommitted OnConclusionCommitted;
	FDocOnConclusionCommittedNative OnConclusionCommittedNative;

	UPROPERTY(BlueprintAssignable, Category = "Evidence|Events")
	FDocOnConclusionChallenged OnConclusionChallenged;
	FDocOnConclusionChallengedNative OnConclusionChallengedNative;

private:
	struct FOwnerState
	{
		int64 Revision = 1;
		TMap<FName, FDocEvidenceObservation> Observations;
		TMap<FName, FDocEvidenceLink> Links;
		TMap<FName, FDocConclusionRecord> Conclusions;
		TSet<FName> Receipts;
		TArray<FDocConclusionRecord> Quarantined;
	};

	enum class EAudience : uint8 { Player, Debug };

	FOwnerState& GetOrAddOwner(FName OwnerId);
	const FOwnerState* FindOwner(FName OwnerId) const;

	bool IsVisible(const FDocEvidenceObservation& Obs, EAudience Audience) const;
	bool IsEstablished(const FDocEvidenceObservation& Obs, EAudience Audience) const;
	TArray<FDocContradiction> FindContradictions(const FOwnerState& State, EAudience Audience) const;

	/** Pure evaluation over a coherent owner state. Depth-limited; dependencies are acyclic by registration. */
	void EvaluateInternal(const FOwnerState& State, FName HypothesisId, EAudience Audience, int32 Depth, FDocDeductionResult& OutResult) const;
	void RedactForPlayer(FDocDeductionResult& Result) const;

	bool WouldCreateCycle(const UDocHypothesisDefinition& Def, FString& OutPath) const;
	bool DependsOnEvidence(FName HypothesisId, FName EvidenceId, int32 Depth = 0) const;
	void ReevaluateConclusions(FName OwnerId, FOwnerState& State, FName ChangedEvidenceId, const FString& Reason);
	static void PushHistory(FDocEvidenceObservation& Obs, FName Reason);

	TMap<FName, TStrongObjectPtr<UDocEvidenceDefinition>> EvidenceDefinitions;
	TMap<FName, TStrongObjectPtr<UDocHypothesisDefinition>> HypothesisDefinitions;
	TMap<FName, FOwnerState> Owners;
};
