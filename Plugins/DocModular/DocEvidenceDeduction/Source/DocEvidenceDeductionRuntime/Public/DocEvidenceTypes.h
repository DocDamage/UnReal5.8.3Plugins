#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocEvidenceTypes.generated.h"

/** A Claim is a reported assertion; only an EstablishedFact can support or disqualify a hypothesis. */
UENUM(BlueprintType)
enum class EDocEvidenceStatus : uint8
{
	Claim,
	EstablishedFact
};

UENUM(BlueprintType)
enum class EDocHypothesisEvaluation : uint8
{
	/** Not enough established, visible evidence. Missing evidence is never proof of absence. */
	Inconclusive,
	Supported,
	Contradicted,
	/** Not evaluable for this audience (hidden, unknown definition, invalid graph). */
	Unavailable
};

UENUM(BlueprintType)
enum class EDocEvidenceLinkRelation : uint8
{
	Supports,
	Contradicts,
	Corroborates,
	AlternativeTo
};

/** Typed contradiction between two established observations. */
UENUM(BlueprintType)
enum class EDocContradictionKind : uint8
{
	/** Same subject, different claimed value. */
	MutuallyExclusiveStatement,
	/** Same subject and event frame, event times further apart than their declared precision. */
	IncompatibleTimestamp
};

USTRUCT(BlueprintType)
struct DOCEVIDENCEDEDUCTIONRUNTIME_API FDocObservationRevision
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	int32 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	EDocEvidenceStatus Status = EDocEvidenceStatus::Claim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FString ClaimContent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName ClaimValue = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	double EventTimestampSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	bool bRetracted = false;

	/** Why this revision exists (Revised, Retracted, EstablishedBy:<source>). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName Reason = NAME_None;
};

USTRUCT(BlueprintType)
struct DOCEVIDENCEDEDUCTIONRUNTIME_API FDocEvidenceObservation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName ObservationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName EvidenceId = NAME_None;

	/** Who or what supplied it (witness, sensor, document). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName SourceEntityId = NAME_None;

	/** Dedup key for the same report delivered twice. Two witnesses never share one merely because their text matches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName ProvenanceKey = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	EDocEvidenceStatus Status = EDocEvidenceStatus::Claim;

	/** Localized/presentational text. Never compared by the rules. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FString ClaimContent;

	/** Subject the observation is about (for typed contradictions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName SubjectId = NAME_None;

	/** Stable value claimed about the subject (for typed contradictions). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName ClaimValue = NAME_None;

	/** Event frame (for timestamp contradictions). None = not time-bound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName EventFrameId = NAME_None;

	/** When the event happened (story clock seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	double EventTimestampSeconds = 0.0;

	/** Precision of EventTimestampSeconds; negative = unknown (never used to contradict). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	double EventTimestampPrecisionSeconds = -1.0;

	/** When it was observed/recorded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	double ObservationTimestampSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	int32 Revision = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	bool bIsRetracted = false;

	/** Discovered by the system but not visible to the player audience. Excluded from ordinary queries and evaluation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	bool bIsHiddenFromPlayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FGameplayTagContainer Tags;

	/** Prior revisions, newest last (bounded). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FDocObservationRevision> History;
};

USTRUCT(BlueprintType)
struct DOCEVIDENCEDEDUCTIONRUNTIME_API FDocEvidenceLink
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName LinkId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName SourceEvidenceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	FName TargetEvidenceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	EDocEvidenceLinkRelation Relation = EDocEvidenceLinkRelation::Supports;

	/** Player interpretation. It is stored and shown, but never changes the authored evaluation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Evidence")
	bool bPlayerAuthored = false;
};

USTRUCT(BlueprintType)
struct DOCEVIDENCEDEDUCTIONRUNTIME_API FDocContradiction
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	EDocContradictionKind Kind = EDocContradictionKind::MutuallyExclusiveStatement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName SubjectId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName ObservationA = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName ObservationB = NAME_None;
};

USTRUCT(BlueprintType)
struct DOCEVIDENCEDEDUCTIONRUNTIME_API FDocDeductionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName HypothesisId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	EDocHypothesisEvaluation Evaluation = EDocHypothesisEvaluation::Inconclusive;

	/** Authored gameplay score, not a calibrated probability. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	float ConfidenceScore = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FString Summary;

	/** Visible evidence ids only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FName> SupportingEvidenceIds;

	/** Visible evidence ids only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FName> ContradictingEvidenceIds;

	/** Only filled by the authorized debug explanation. Ordinary queries never list undiscovered evidence. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FName> MissingEvidenceIds;

	/** How many required items are still undiscovered for the closest evidence set (no identities). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	int32 MissingEvidenceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FDocContradiction> Contradictions;

	/** Index of the satisfied alternative evidence set, or INDEX_NONE. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	int32 SatisfiedSetIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	bool bIsChallenged = false;

	/** True when fields were withheld for this audience. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	bool bRedacted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	int64 SnapshotRevision = 0;
};

USTRUCT(BlueprintType)
struct DOCEVIDENCEDEDUCTIONRUNTIME_API FDocConclusionRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName ConclusionId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName OwnerId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName HypothesisId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	int64 CommittedSnapshotRevision = 0;

	/** "ObservationId@Revision" of each supporting observation at commit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FString> SupportingObservationRevisions;

	/** Durable effect key; a receipt is issued once per owner. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName ReceiptKey = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	bool bIsChallenged = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FString ChallengeReason;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	double Timestamp = 0.0;
};

/** One owner's investigation (observations, links, conclusion history, receipts). Definitions are not saved. */
USTRUCT(BlueprintType)
struct DOCEVIDENCEDEDUCTIONRUNTIME_API FDocEvidenceSnapshot
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	FName OwnerId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	int64 Revision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TMap<FName, FDocEvidenceObservation> Observations;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FDocEvidenceLink> Links;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TMap<FName, FDocConclusionRecord> Conclusions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FName> Receipts;

	/** Conclusions whose hypothesis definition no longer exists; kept, inactive. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Evidence")
	TArray<FDocConclusionRecord> QuarantinedConclusions;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocOnObservationAdded, const FDocEvidenceObservation&, Observation);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocOnObservationAddedNative, const FDocEvidenceObservation& /*Observation*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocOnObservationRevised, const FDocEvidenceObservation&, Observation);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocOnObservationRevisedNative, const FDocEvidenceObservation& /*Observation*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocOnObservationRetracted, FName, ObservationId);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocOnObservationRetractedNative, FName /*ObservationId*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocOnHypothesisEvaluated, const FDocDeductionResult&, Result);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocOnHypothesisEvaluatedNative, const FDocDeductionResult& /*Result*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocOnConclusionCommitted, const FDocConclusionRecord&, Conclusion);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocOnConclusionCommittedNative, const FDocConclusionRecord& /*Conclusion*/);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocOnConclusionChallenged, FName, ConclusionId, FString, Reason);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocOnConclusionChallengedNative, FName /*ConclusionId*/, const FString& /*Reason*/);
