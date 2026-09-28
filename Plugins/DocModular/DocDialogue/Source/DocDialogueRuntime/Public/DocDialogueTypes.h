#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "UObject/SoftObjectPtr.h"
#include "DocEffectKey.h"
#include "DocOwnerScope.h"
#include "DocRequestHandle.h"
#include "DocSharedTypes.h"
#include "DocSystemResult.h"
#include "DocDialogueTypes.generated.h"

class USoundBase;
class UTexture2D;

/**
 * DocDialogue data model (Modules 11-20 handoff, Section 6).
 *
 * Name mapping to the handoff's semantic contracts:
 *   UDialogueSubsystem          -> UDocDialogueSubsystem
 *   UDialogueGraph              -> UDocDialogueGraph
 *   UDialogueParticipantComponent / IDialogueParticipant -> UDocDialogueParticipantComponent / IDocDialogueParticipant
 *   UDialogueCondition / UDialogueAction -> FDocDialogueCondition / FDocDialogueAction (plain data, never code strings)
 *   FDialogueSession            -> FDocDialogueSessionSnapshot (the mutable session lives inside the subsystem)
 *   FDialogueNodeHandle         -> FDocDialogueNodeRef
 *   FDialogueChoice             -> FDocDialogueChoice
 */
namespace DocDialogueTags
{
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_StaleRevision);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_ChoiceNotFound);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_ChoiceHidden);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_ChoiceDisabled);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_Unauthorized);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_WrongState);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_NotSkippable);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_RunawayLoop);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_MissingNode);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_NoChoices);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_ReservationConflict);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_ProviderMissing);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_VariableMissing);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_VariableType);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_ParticipantMissing);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_ParticipantLost);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_ActionFailed);
	DOCDIALOGUERUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_Dialogue_UnresolvedAction);
}

// ---------------------------------------------------------------------------
// Typed variables
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocDialogueValueType : uint8
{
	None,
	Bool,
	Int,
	Float,
	Name
};

/**
 * Typed dialogue value. Numeric coercion rules (used by comparisons, Add and writes):
 *   Bool  -> number: false = 0, true = 1
 *   Int   -> Float: exact (within double precision)
 *   Float -> Int: rounded half away from zero
 *   number -> Bool: non-zero is true
 *   Name is never numeric and only converts to Name.
 */
USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	EDocDialogueValueType Type = EDocDialogueValueType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue", meta = (EditCondition = "Type == EDocDialogueValueType::Bool", EditConditionHides))
	bool bBool = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue", meta = (EditCondition = "Type == EDocDialogueValueType::Int", EditConditionHides))
	int64 Int = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue", meta = (EditCondition = "Type == EDocDialogueValueType::Float", EditConditionHides))
	double Float = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue", meta = (EditCondition = "Type == EDocDialogueValueType::Name", EditConditionHides))
	FName Name;

	static FDocDialogueValue MakeBool(bool bValue) { FDocDialogueValue V; V.Type = EDocDialogueValueType::Bool; V.bBool = bValue; return V; }
	static FDocDialogueValue MakeInt(int64 Value) { FDocDialogueValue V; V.Type = EDocDialogueValueType::Int; V.Int = Value; return V; }
	static FDocDialogueValue MakeFloat(double Value) { FDocDialogueValue V; V.Type = EDocDialogueValueType::Float; V.Float = Value; return V; }
	static FDocDialogueValue MakeName(FName Value) { FDocDialogueValue V; V.Type = EDocDialogueValueType::Name; V.Name = Value; return V; }

	bool IsSet() const { return Type != EDocDialogueValueType::None; }
	bool IsNumeric() const { return Type == EDocDialogueValueType::Bool || Type == EDocDialogueValueType::Int || Type == EDocDialogueValueType::Float; }
	/** False for None and Name. */
	bool ToNumber(double& Out) const;
	/** Convert to Target using the coercion rules. False when not convertible (value unchanged). */
	bool CoerceTo(EDocDialogueValueType Target, FDocDialogueValue& Out) const;
	FString ToString() const;

	friend bool operator==(const FDocDialogueValue& A, const FDocDialogueValue& B)
	{
		if (A.Type != B.Type) { return false; }
		switch (A.Type)
		{
		case EDocDialogueValueType::Bool: return A.bBool == B.bBool;
		case EDocDialogueValueType::Int: return A.Int == B.Int;
		case EDocDialogueValueType::Float: return A.Float == B.Float;
		case EDocDialogueValueType::Name: return A.Name == B.Name;
		default: return true;
		}
	}
	friend bool operator!=(const FDocDialogueValue& A, const FDocDialogueValue& B) { return !(A == B); }
};

/** Typed variable declaration. Undeclared variables are never created implicitly. */
USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueVariableDecl
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueValueType Type = EDocDialogueValueType::Int;

	/** Default; coerced to Type. Also used when a saved value is missing or has an incompatible type. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FDocDialogueValue Default;

	/** Persistent values survive the session in the (graph, owner) memory; others reset per session. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bPersistent = false;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueVariableEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName Name;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocDialogueValue Value;
};

// ---------------------------------------------------------------------------
// Conditions (pure data; evaluated by the subsystem or explicit providers)
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocDialogueConditionType : uint8
{
	/** Tag present on Role's participant (or, when Role is None, on the session context tags + initiator). */
	TagPresent,
	/** Tag absent (same source as TagPresent). */
	TagMissing,
	/** Tag present on Role's participant. Role is required. */
	ParticipantTag,
	/** Variable (VariableName) compared with Value. */
	NumericComparison,
	/** Event history through a provider (ProviderId, default Settings.EventProviderId). Bridge-backed. */
	EventState,
	/** Registered provider by ProviderId (the "custom Blueprint" route: a provider object hosts the logic). */
	CustomProvider,
	/** Registered provider by ProviderId querying an interface on participants. */
	InterfaceQuery,
	/** Node QueryId visited in this graph/owner memory. */
	NodeVisited,
	/** ChoiceId QueryId taken in this graph/owner memory. */
	ChoiceTaken
};

UENUM(BlueprintType)
enum class EDocDialogueCompare : uint8
{
	Equal,
	NotEqual,
	Less,
	LessOrEqual,
	Greater,
	GreaterOrEqual
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueConditionType Type = EDocDialogueConditionType::TagPresent;

	/** Participant role for tag conditions and interface queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Role;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName VariableName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueCompare Compare = EDocDialogueCompare::GreaterOrEqual;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	double Value = 0.0;

	/** Provider for EventState/CustomProvider/InterfaceQuery. Missing provider = Unavailable, never false. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName ProviderId;

	/** Provider-defined key (event id, custom query id) or the NodeId/ChoiceId for history conditions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName QueryId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FGameplayTagContainer QueryTags;

	/** Inverts Satisfied/Unsatisfied. Unavailable stays Unavailable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bNegate = false;
};

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocDialogueActionType : uint8
{
	/** Internal: set a declared variable (coerced). Staged, committed with the transition. */
	SetVariable,
	/** Internal: add Value to a numeric variable. Staged, committed with the transition. */
	AddVariable,
	/** External: through the event provider (ProviderId, default Settings.EventProviderId). No Events dependency. */
	BroadcastEvent,
	/** External: IDocMutableGameplayTagProvider on Role's participant (or its actor). No GAS dependency. */
	GrantTag,
	RemoveTag,
	/** External: registered action provider by ProviderId (bridges implement quest/inventory/codex/unlock/sequence actions). */
	Custom
};

UENUM(BlueprintType)
enum class EDocDialogueActionTiming : uint8
{
	/** Resolved before the transition commits; a failed required action rejects the transition. */
	BeforeTransition,
	/** Cosmetic; runs after the transition commits. Failures are logged only. */
	AfterTransition
};

UENUM(BlueprintType)
enum class EDocDialogueActionState : uint8
{
	Pending,
	Committed,
	Failed,
	/** Undone by a provider that advertises a validated compensation. */
	Compensated,
	/** Receipt already existed: not executed again. */
	Duplicate
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueAction
{
	GENERATED_BODY()

	/** Stable within its node/choice; part of the effect key. Never an array offset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName ActionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueActionType Type = EDocDialogueActionType::SetVariable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueActionTiming Timing = EDocDialogueActionTiming::BeforeTransition;

	/** A failed required BeforeTransition action rejects the transition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bRequired = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName VariableName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FDocDialogueValue Value;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Role;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FGameplayTag Tag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName ProviderId;

	/** Opaque, provider-interpreted data. Never a class path to load. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FString Payload;

	/** If the session is cancelled after this committed, ask the provider to compensate (only if it advertises support). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bCompensateOnCancel = false;
};

// ---------------------------------------------------------------------------
// Nodes and graph
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocDialogueNodeType : uint8
{
	Line,
	Choice,
	/** Ordered cases; first satisfied case wins, else Next. */
	Branch,
	/** Gate: Conditions satisfied -> Next, unsatisfied -> FailDestination. */
	Condition,
	/** Runs Actions (as a transition) then Next. */
	Event,
	Jump,
	Delay,
	End,
	/** Registered custom node handler by CustomType. */
	Custom
};

UENUM(BlueprintType)
enum class EDocDialogueLineTiming : uint8
{
	/** Auto-advance after Duration (or the text estimate when Duration <= 0). */
	FixedDuration,
	/** Auto-advance after the voice duration; missing voice uses MissingVoicePolicy. */
	VoiceDuration,
	/** Wait for AcknowledgeLinePresented, then hold Duration. A safety timeout prevents headless deadlock. */
	PresentationAck
};

UENUM(BlueprintType)
enum class EDocDialogueMissingVoicePolicy : uint8
{
	/** Use Duration, or the text estimate when Duration <= 0. */
	UseDuration,
	/** Always use the text-length estimate. */
	UseTextEstimate
};

UENUM(BlueprintType)
enum class EDocDialogueEndKind : uint8
{
	Completed,
	Failed
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueSubtitleTiming
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	float LeadInSeconds = 0.f;

	/** Minimum on-screen time for subtitles, independent of audio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	float MinDisplaySeconds = 0.f;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueChoice
{
	GENERATED_BODY()

	/** Stable ID; selection is by ID, never by list index. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName ChoiceId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FText ChoiceText;

	/** None ends the conversation (Completed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Destination;

	/** All must be satisfied for the choice to be offered at all; unsatisfied = Hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueCondition> Conditions;

	/** All must be satisfied for the choice to be shown; unsatisfied = Hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueCondition> HiddenConditions;

	/** All must be satisfied for a shown choice to be selectable; unsatisfied = VisibleDisabled with FailureReason. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueCondition> DisabledConditions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FText FailureReason;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueAction> Actions;

	/** Hidden after being taken once (per graph/owner memory). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bOnceOnly = false;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueBranchCase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueCondition> Conditions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Destination;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueNode
{
	GENERATED_BODY()

	/** Stable node ID (checkpoints and history refer to it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName NodeId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueNodeType Type = EDocDialogueNodeType::Line;

	/** Line/Event/Jump/Delay/Custom continuation, Condition pass, Branch else. None ends the conversation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName Next;

	// ---- Line ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	FName SpeakerRole;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	TSoftObjectPtr<USoundBase> VoiceAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	FDocDialogueSubtitleTiming SubtitleTiming;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	FGameplayTag AnimationTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	FGameplayTag ExpressionTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	FGameplayTag CameraTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	FGameplayTagContainer GameplayTags;

	/** False: the line completes only through Advance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	bool bAutoAdvance = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	float Duration = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	EDocDialogueLineTiming TimingSource = EDocDialogueLineTiming::FixedDuration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	EDocDialogueMissingVoicePolicy MissingVoicePolicy = EDocDialogueMissingVoicePolicy::UseDuration;

	/** For auto-advancing lines: false rejects a manual Advance before the line's timing completes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Line")
	bool bSkippable = true;

	// ---- Choice ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice")
	TArray<FDocDialogueChoice> Choices;

	/** Used when no choice is VisibleEnabled. None = the session fails with NoChoices. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Choice")
	FName NoChoiceDestination;

	// ---- Branch / Condition ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Branch")
	TArray<FDocDialogueBranchCase> Cases;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Branch")
	TArray<FDocDialogueCondition> Conditions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Branch")
	FName FailDestination;

	/** Where to go when a condition is Unavailable. None = fail the session (never treated as false). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Branch")
	FName UnavailableDestination;

	// ---- Event ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	TArray<FDocDialogueAction> Actions;

	// ---- Delay ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delay")
	float DelaySeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delay")
	EDocClockDomain DelayClock = EDocClockDomain::WorldGameplay;

	// ---- End ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "End")
	EDocDialogueEndKind EndKind = EDocDialogueEndKind::Completed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "End")
	FGameplayTag EndTag;

	// ---- Custom ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom")
	FName CustomType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Custom")
	TMap<FName, FString> CustomParams;

	/** Suppresses the unreachable-node warning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bIntentionallyUnreachable = false;
};

/** Reference to a node inside a specific graph version (FDialogueNodeHandle). */
USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueNodeRef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName GraphId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int32 GraphVersion = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName NodeId;
};

/** What happens to a session when a participant is lost (death/unload/range/travel/takeover). */
UENUM(BlueprintType)
enum class EDocDialogueLossPolicy : uint8
{
	Cancel,
	/** Pause; TimedOut if not resumed within PauseTimeoutSeconds. */
	PauseWithTimeout,
	/** Keep going with the logical participant only (no actor). */
	ContinueWithoutActor,
	/** Pause until a participant with the same ParticipantId registers in this world; TimedOut after PauseTimeoutSeconds. */
	RebindByIdentity
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueRole
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName RoleId;

	/** Fallback speaker name when the participant supplies none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bRequired = true;

	/** Exclusive roles reserve the participant (one conversation at a time) unless the participant allows sharing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	bool bExclusive = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueLossPolicy LossPolicy = EDocDialogueLossPolicy::Cancel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue", meta = (ClampMin = "0"))
	float PauseTimeoutSeconds = 30.f;
};

UENUM(BlueprintType)
enum class EDocDialogueUnavailableChoicePolicy : uint8
{
	/** Shown but not selectable. */
	Disable,
	Hide
};

UENUM(BlueprintType)
enum class EDocDialogueAuthorityMode : uint8
{
	/** Runs locally; the initiator or audience may drive it. Works on clients. */
	LocalOnly,
	/** Authority only; only the initiator may drive it. */
	OwnerAuthoritative,
	/** Authority only; the initiator or any audience member may drive it. No voting is implied. */
	SharedAuthoritative
};

/**
 * Dialogue graph (UDialogueGraph). Immutable runtime data: a running session never
 * mutates the asset. Data-asset authoring is the interim path until a graph editor exists.
 */
UCLASS(BlueprintType)
class DOCDIALOGUERUNTIME_API UDocDialogueGraph : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName GraphId;

	/** Content/schema version; stored with memories and checkpoints. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	FName StartNode;

	/** Keyed by NodeId (order is irrelevant to execution). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueNode> Nodes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueRole> Roles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueVariableDecl> Variables;

	/** Immediate steps per dispatch before yielding to the next tick (0 = settings default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution", meta = (ClampMin = "0"))
	int32 MaxStepsPerDispatch = 0;

	/** Consecutive steps without waiting before the session fails as a runaway loop (0 = settings default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution", meta = (ClampMin = "0"))
	int32 MaxNonYieldingSteps = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution")
	EDocDialogueUnavailableChoicePolicy UnavailableChoicePolicy = EDocDialogueUnavailableChoicePolicy::Disable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution")
	bool bCheckpointsEnabled = true;

	/** Reject a second concurrent session of this graph for the same owner (keeps checkpoints unambiguous). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution")
	bool bSingleSessionPerOwner = true;

	/** Content migration: removed NodeId -> replacement NodeId, used when restoring checkpoints. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Execution")
	TMap<FName, FName> NodeRedirects;

	const FDocDialogueNode* FindNode(FName NodeId) const;
	const FDocDialogueRole* FindRole(FName RoleId) const;
	const FDocDialogueVariableDecl* FindVariable(FName Name) const;

	/** Errors block Start; warnings are informational. */
	void FindProblems(TArray<FString>& OutErrors, TArray<FString>& OutWarnings) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocDialogueGraph"), GetFName()); }
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

// ---------------------------------------------------------------------------
// Sessions
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocDialogueSessionState : uint8
{
	None,
	PendingBindings,
	Running,
	WaitingForAdvance,
	WaitingForChoice,
	WaitingForDelay,
	WaitingForAction,
	Paused,
	Completed,
	Cancelled,
	Failed,
	TimedOut
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueParticipantBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FName RoleId;

	/** Stable logical identity. Optional when Participant is set (read from it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FName ParticipantId;

	/** Optional object implementing IDocDialogueParticipant. Logical participants (a narrator) need none. */
	UPROPERTY(BlueprintReadWrite, Category = "Dialogue")
	TWeakObjectPtr<UObject> Participant;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueStartRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TObjectPtr<UDocDialogueGraph> Graph = nullptr;

	/** Durable owner of history/variables/receipts. Required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FDocOwnerScope Owner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	TArray<FDocDialogueParticipantBinding> Bindings;

	/** Who started it (player controller, local player, actor...). Drives authorization. */
	UPROPERTY(BlueprintReadWrite, Category = "Dialogue")
	TWeakObjectPtr<UObject> Initiator;

	/** Who may observe and (in shared mode) drive the session. Native-only (weak array). */
	UPROPERTY()
	TArray<TWeakObjectPtr<UObject>> Audience;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	EDocDialogueAuthorityMode AuthorityMode = EDocDialogueAuthorityMode::OwnerAuthoritative;

	/** Source for TagPresent/TagMissing without a role. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialogue")
	FGameplayTagContainer ContextTags;
};

UENUM(BlueprintType)
enum class EDocDialogueChoiceState : uint8
{
	Hidden,
	VisibleDisabled,
	VisibleEnabled
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueChoiceView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName ChoiceId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FText ChoiceText;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueChoiceState State = EDocDialogueChoiceState::Hidden;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FText FailureReason;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGameplayTag ReasonTag;
};

/** Choices computed for one explicit session revision. Hidden choices are never included. */
USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueChoiceList
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName NodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int64 SessionRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueChoiceView> Choices;

	const FDocDialogueChoiceView* Find(FName ChoiceId) const { return Choices.FindByPredicate([ChoiceId](const FDocDialogueChoiceView& V) { return V.ChoiceId == ChoiceId; }); }
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueLineInfo
{
	GENERATED_BODY()

	/** Stable per-session ordinal; shown and completed events of one line share it. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int64 LineOrdinal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName NodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName SpeakerRole;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName SpeakerParticipantId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FText SpeakerName;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TSoftObjectPtr<USoundBase> VoiceAsset;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bVoiceMissing = false;

	/** Seconds the line is timed for (auto lines); subtitles use at least SubtitleTiming.MinDisplaySeconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	float ResolvedDuration = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocDialogueSubtitleTiming SubtitleTiming;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGameplayTag AnimationTag;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGameplayTag ExpressionTag;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGameplayTag CameraTag;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGameplayTagContainer GameplayTags;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bAutoAdvance = false;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bSkippable = true;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueParticipantView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName RoleId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName ParticipantId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FText DisplayName;

	/** False when the participant object is gone (lost or purely logical). */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bPresent = false;
};

/** Read-only view of a session (FDialogueSession). Presentation adapters observe this. */
USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueSessionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocRequestHandle Handle;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGuid SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocDialogueNodeRef Node;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueSessionState State = EDocDialogueSessionState::None;

	/** For Paused: the state resumed into. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueSessionState ResumeState = EDocDialogueSessionState::None;

	/** Monotonic; submit as ExpectedRevision. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int64 TransitionOrdinal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bHasLine = false;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocDialogueLineInfo Line;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocDialogueChoiceList Choices;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueParticipantView> Participants;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FString Diagnostic;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueActionRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocEffectKey Key;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	EDocDialogueActionState State = EDocDialogueActionState::Pending;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocSystemResult Result;
};

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueChoiceRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName NodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName ChoiceId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGuid SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int64 TransitionOrdinal = 0;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueRoleBinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName RoleId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName ParticipantId;
};

/**
 * Safe resumable point. Only waiting boundaries are checkpointed. A transition whose
 * actions were in flight is recorded as an intent (FromNode + ChoiceId + ordinal) so a
 * resume re-runs it with the same effect keys: committed receipts are not replayed.
 */
USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueCheckpoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGuid SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName NodeId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int64 TransitionOrdinal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int64 LineOrdinal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueVariableEntry> Variables;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueRoleBinding> RoleBindings;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	bool bPendingTransition = false;

	/** Choice being committed (None for an Event node transition at NodeId). */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName PendingChoiceId;

	/** Action that was in flight (no receipt yet) at checkpoint time. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName InFlightActionId;
};

/** Durable per (GraphId, Owner) memory. */
USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueMemory
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FName GraphId;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int32 GraphVersion = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocOwnerScope Owner;

	/** Part of every effect key; a new campaign never matches old receipts. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FGuid CampaignEpoch;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FName> VisitedNodes;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueChoiceRecord> ChoiceHistory;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FName> OnceChoicesTaken;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueVariableEntry> PersistentVariables;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocEffectReceipt> Receipts;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	FDocDialogueCheckpoint Checkpoint;
};

USTRUCT(BlueprintType)
struct DOCDIALOGUERUNTIME_API FDocDialogueSaveData
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	int32 SchemaVersion = CurrentSchemaVersion;

	UPROPERTY(BlueprintReadOnly, Category = "Dialogue")
	TArray<FDocDialogueMemory> Memories;
};

UENUM(BlueprintType)
enum class EDocDialogueRestorePolicy : uint8
{
	/** Missing checkpoint node (after redirects) is NotFound. */
	Error,
	/** Explicitly restart from StartNode with a new session id (actions may run again). */
	RestartFromStart
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Dialogue"))
class DOCDIALOGUERUNTIME_API UDocDialogueSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(Config, EditAnywhere, Category = "Execution", meta = (ClampMin = "1"))
	int32 DefaultMaxStepsPerDispatch = 64;

	UPROPERTY(Config, EditAnywhere, Category = "Execution", meta = (ClampMin = "1"))
	int32 DefaultMaxNonYieldingSteps = 1000;

	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0"))
	float TextSecondsPerCharacter = 0.05f;

	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0"))
	float MinLineSeconds = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0"))
	float MaxLineSeconds = 12.f;

	/** PresentationAck lines complete after this long without an acknowledgement (no headless deadlock). */
	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0.1"))
	float PresentationAckTimeoutSeconds = 15.f;

	/** Provider used by EventState conditions and BroadcastEvent actions when ProviderId is None. */
	UPROPERTY(Config, EditAnywhere, Category = "Providers")
	FName EventProviderId = TEXT("Event");

	/** Receipts kept per (graph, owner) memory; oldest evicted first. 0 = unbounded. */
	UPROPERTY(Config, EditAnywhere, Category = "Persistence", meta = (ClampMin = "0"))
	int32 MaxReceiptsPerMemory = 512;
};
