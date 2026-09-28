#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "NativeGameplayTags.h"
#include "UObject/SoftObjectPtr.h"
#include "DocOwnerScope.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocUITypes.generated.h"

class UTexture2D;
class USoundBase;

/**
 * DocGameFrameworkUI data model (Modules 11-20 handoff, Section 12; Module 20).
 *
 * The base is presentation-neutral: no UMG or CommonUI types, no widget classes. A
 * presenter (the CommonUI bridge, a UMG reference presenter, or a test double) maps a
 * screen definition's PresentationKey to real widgets.
 *
 * Name mapping to the handoff's semantic contracts:
 *   UIManagerSubsystem     -> UDocUIManagerSubsystem (LocalPlayer)
 *   NotificationSubsystem  -> UDocNotificationSubsystem (LocalPlayer)
 *   UIScreenDefinition     -> UDocUIScreenDefinition
 *   NotificationDefinition -> UDocNotificationDefinition
 *   UIScreenRequest        -> FDocUIScreenRequest
 *   NotificationRequest    -> FDocNotificationRequest
 */
namespace DocUITags
{
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Game);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_HUD);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Menu);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Modal);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Popup);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Loading);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Layer_Debug);

	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Dialog);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Notification);

	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_UnknownScreen);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Duplicate);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_LayerFull);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Blocked);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_StaleRevision);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_StaleHandle);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_PresentationFailed);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Capability);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Dialog);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Pause);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Loading);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Notification);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Remap);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_Setting);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_SettingUnavailable);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_SettingConflict);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_SettingNotApplied);
	DOCGAMEFRAMEWORKUIRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Error_UI_RevertFailed);
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocUIInputMode : uint8
{
	/** Gameplay keeps input; the screen claims nothing. */
	Game,
	/** Gameplay and UI both receive input; the screen claims focus only. */
	GameAndUI,
	/** The screen owns input and focus while it is the effective claimant. */
	UIOnly
};

UENUM(BlueprintType)
enum class EDocUIPausePolicy : uint8
{
	None,
	/** Halts the world through the shared global pause lease (authorized worlds only). */
	GlobalPause,
	/** Local UI/input behaviour only; never halts authoritative simulation. */
	UIOnlyPause,
	/** Multiplayer menus: never pauses the server. */
	MultiplayerNonPause
};

UENUM(BlueprintType)
enum class EDocUICursorPolicy : uint8
{
	Unchanged,
	Show
};

UENUM(BlueprintType)
enum class EDocUIDuplicatePolicy : uint8
{
	Reject,
	FocusExisting,
	ReplaceExisting,
	AllowDistinctInstances
};

UENUM(BlueprintType)
enum class EDocUIFocusPolicy : uint8
{
	/** Restore the saved target if it is still focusable, else the default target. */
	RestoreLast,
	/** Always the definition's default target. */
	DefaultTarget,
	/** The screen never takes navigation focus. */
	None
};

UENUM(BlueprintType)
enum class EDocUIScreenState : uint8
{
	Requested,
	LoadingPresentation,
	Activating,
	Active,
	Suspended,
	Deactivating,
	Closed,
	Failed,
	Cancelled
};

/** One semantic layer: stack/exclusivity, relative priority, blocking and capacity. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocUILayerPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (Categories = "UI.Layer")) FGameplayTag LayerTag;
	/** Higher layers are above lower ones for back handling and focus. Not an input-ownership contract. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") int32 Priority = 0;
	/** Only the top screen is Active; screens below are Suspended. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") bool bExclusive = true;
	/** While any screen of this layer is Active, back/navigation input is routed to it before lower layers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI") bool bBlocksLowerLayers = false;
	/** 0 = unlimited live screens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = "0")) int32 Capacity = 0;
};

/** UIScreenDefinition. Widgets are never referenced here: the presenter resolves PresentationKey. */
UCLASS(BlueprintType)
class DOCGAMEFRAMEWORKUIRUNTIME_API UDocUIScreenDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable identity (saved in restoration descriptors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") FName ScreenId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen", meta = (Categories = "UI.Screen")) FGameplayTag ScreenTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen", meta = (Categories = "UI.Layer")) FGameplayTag LayerTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") EDocUIInputMode InputMode = EDocUIInputMode::UIOnly;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") EDocUIPausePolicy PausePolicy = EDocUIPausePolicy::None;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") EDocUICursorPolicy CursorPolicy = EDocUICursorPolicy::Show;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") EDocUIDuplicatePolicy DuplicatePolicy = EDocUIDuplicatePolicy::Reject;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") FName TransitionProfile;
	/** Context tags (host-set on the manager) that must all be present. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") FGameplayTagContainer RequiredTags;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") FGameplayTagContainer BlockedTags;
	/** Bridge-owned presentation asset key (the CommonUI bridge maps it to a widget class). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") FName PresentationKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") FName PayloadSchema;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") int32 PayloadVersion = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") EDocUIFocusPolicy FocusPolicy = EDocUIFocusPolicy::RestoreLast;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") FName DefaultFocusTarget;
	/** May be recreated from a restoration descriptor after travel (never for modals). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") bool bRestorableAfterTravel = false;
	/** Presenter capabilities this screen needs (e.g. UI.Capability.Touch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") FGameplayTagContainer RequiredCapabilities;
	/** Control-claim priority; modals default above menus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen") int32 ControlPriority = 100;

	void FindProblems(TArray<FString>& OutErrors) const;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocUIScreenDefinition"), GetFName()); }
};

/** UIScreenRequest. Payload is plain validated data only (no object references or callbacks). */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocUIScreenRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FName ScreenId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") TMap<FName, FString> Payload;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") int32 PayloadVersion = 1;
	/** -1 = any; otherwise the manager revision the caller observed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") int64 ExpectedRevision = -1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FString DebugReason;
};

/** Read-only view of one screen instance. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocUIScreenInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UI") FDocRequestHandle Handle;
	/** Distinct per instance even when ScreenTag matches. */
	UPROPERTY(BlueprintReadOnly, Category = "UI") FGuid InstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "UI") FName ScreenId;
	UPROPERTY(BlueprintReadOnly, Category = "UI") FGameplayTag ScreenTag;
	UPROPERTY(BlueprintReadOnly, Category = "UI") FGameplayTag LayerTag;
	UPROPERTY(BlueprintReadOnly, Category = "UI") FName PresentationKey;
	UPROPERTY(BlueprintReadOnly, Category = "UI") EDocUIScreenState State = EDocUIScreenState::Requested;
	UPROPERTY(BlueprintReadOnly, Category = "UI") FName FocusTarget;
	UPROPERTY(BlueprintReadOnly, Category = "UI") bool bHasControlClaim = false;
	UPROPERTY(BlueprintReadOnly, Category = "UI") TMap<FName, FString> Payload;
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocUIScreenStateChange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UI") FDocRequestHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "UI") FName ScreenId;
	UPROPERTY(BlueprintReadOnly, Category = "UI") EDocUIScreenState OldState = EDocUIScreenState::Requested;
	UPROPERTY(BlueprintReadOnly, Category = "UI") EDocUIScreenState NewState = EDocUIScreenState::Requested;
};

/** Optional travel/restore descriptor: stable id and plain payload only. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocUIRestorationDescriptor
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "UI") FName ScreenId;
	UPROPERTY(BlueprintReadWrite, Category = "UI") int32 PayloadVersion = 1;
	UPROPERTY(BlueprintReadWrite, Category = "UI") TMap<FName, FString> Payload;
};

// ---------------------------------------------------------------------------
// Dialogs and loading
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocDialogResult : uint8
{
	None,
	Confirmed,
	Cancelled,
	ThirdAction,
	OwnerDestroyed,
	Failed
};

UENUM(BlueprintType)
enum class EDocDialogInput : uint8
{
	Confirm,
	Cancel,
	ThirdAction,
	Back
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocDialogRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FText Body;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FText ConfirmText;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FText CancelText;
	/** Empty = no third action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FText ThirdActionText;
	/** Destructive prompts never resolve affirmatively on close, back or timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") bool bDestructive = false;
	/** Real-time seconds; 0 = no timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0")) float TimeoutSeconds = 0.f;
	/** Result on timeout (forced to Cancelled for destructive prompts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") EDocDialogResult TimeoutResult = EDocDialogResult::Cancelled;
};

UENUM(BlueprintType)
enum class EDocLoadingOutcome : uint8
{
	Completed,
	Failed,
	Cancelled
};

/** Aggregated loading presentation for one local player. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocLoadingView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UI") bool bVisible = false;
	UPROPERTY(BlueprintReadOnly, Category = "UI") int32 ActiveLeases = 0;
	/** True unless every active lease reports a real denominator. */
	UPROPERTY(BlueprintReadOnly, Category = "UI") bool bIndeterminate = true;
	/** Conservative: the minimum of the determinate leases. */
	UPROPERTY(BlueprintReadOnly, Category = "UI") float Progress = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UI") FText Status;
	/** A failed or cancelled operation stays visible until dismissed; distinct from completion. */
	UPROPERTY(BlueprintReadOnly, Category = "UI") bool bHasError = false;
	UPROPERTY(BlueprintReadOnly, Category = "UI") EDocLoadingOutcome ErrorOutcome = EDocLoadingOutcome::Failed;
	UPROPERTY(BlueprintReadOnly, Category = "UI") FText ErrorText;
};

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocNotificationCategory : uint8
{
	Info,
	Success,
	Warning,
	Error,
	Discovery,
	Objective,
	Unlock,
	Custom
};

UENUM(BlueprintType)
enum class EDocNotificationPolicy : uint8
{
	/** Shown alongside others (up to the visible limit), otherwise queued. */
	Stack,
	/** Replaces entries with the same NotificationTag and owner. */
	Replace,
	/** Merges into the entry with the same MergeKey and owner (count increments). */
	Merge,
	/** May preempt a lower-priority visible entry back into the queue. */
	PriorityInterrupt,
	/** Never times out; must be dismissed. Never dropped by overflow. */
	Persistent,
	/** Standard timed entry. */
	Timed
};

/** NotificationRequest. Actions are handler keys resolved at invocation, never serialized callbacks. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocNotificationRequest
{
	GENERATED_BODY()

	/** Duplicate identity; generated when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FGuid RequestId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (Categories = "UI.Notification")) FGameplayTag NotificationTag;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FText Title;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FText Message;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") int32 Priority = 0;
	/** Real-time seconds while visible; <= 0 uses the settings default. Ignored for Persistent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") float Duration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") EDocNotificationCategory Category = EDocNotificationCategory::Info;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") EDocNotificationPolicy Policy = EDocNotificationPolicy::Timed;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FName MergeKey;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") TSoftObjectPtr<USoundBase> Sound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FName ActionHandlerKey;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") TMap<FName, FString> ActionPayload;
	/** Merge-key scope. Unset = the local player's own queue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") FDocOwnerScope Owner;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") int32 PayloadVersion = 1;
	/** Real-time TTL counted from enqueue, including time spent queued/hidden. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = "0")) float ExpireSeconds = 0.f;
};

UCLASS(BlueprintType)
class DOCGAMEFRAMEWORKUIRUNTIME_API UDocNotificationDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Template; RequestId, Owner and ActionPayload are filled per request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notification") FDocNotificationRequest Template;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("DocNotificationDefinition"), GetFName()); }
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocNotificationEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UI") FDocNotificationRequest Request;
	/** Merged occurrences (presentation only; merging never repeats a gameplay action). */
	UPROPERTY(BlueprintReadOnly, Category = "UI") int32 MergeCount = 1;
	UPROPERTY(BlueprintReadOnly, Category = "UI") bool bVisible = false;
	UPROPERTY(BlueprintReadOnly, Category = "UI") float RemainingSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UI") float AgeSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UI") float QueuedSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UI") bool bActionConsumed = false;
	/** Stable FIFO tie-break. */
	UPROPERTY(BlueprintReadOnly, Category = "UI") int64 Sequence = 0;
};

// ---------------------------------------------------------------------------
// Input devices and bindings
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocInputDeviceCategory : uint8
{
	/** No verified metadata. */
	Unknown,
	KeyboardMouse,
	XboxController,
	PlayStationController,
	GenericGamepad,
	Touch,
	Custom
};

/** One observed input, reported by the host input adapter. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocInputSample
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") FKey Key;
	/** Axis magnitude for analog keys (mouse deltas in pixels). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") float AnalogValue = 0.f;
	/** Brand/category from verified platform/device metadata only; never inferred from the key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") EDocInputDeviceCategory VerifiedHardware = EDocInputDeviceCategory::Unknown;
};

/** One alternative binding; several keys = a chord (all held). */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocInputBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TArray<FKey> Keys;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") bool bHold = false;

	friend bool operator==(const FDocInputBinding& A, const FDocInputBinding& B) { return A.Keys == B.Keys && A.bHold == B.bHold; }
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocActionBindings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") FName ActionId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TArray<FDocInputBinding> KeyboardMouse;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") TArray<FDocInputBinding> Gamepad;
	/** Navigation confirm/back: must keep at least one binding in each scope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input") bool bReservedNavigation = false;

	TArray<FDocInputBinding>& Scope(bool bGamepad) { return bGamepad ? Gamepad : KeyboardMouse; }
	const TArray<FDocInputBinding>& Scope(bool bGamepad) const { return bGamepad ? Gamepad : KeyboardMouse; }
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocBindingPresentation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Input") TArray<FKey> Keys;
	/** Parallel to Keys; None = no art (use Labels). */
	UPROPERTY(BlueprintReadOnly, Category = "Input") TArray<FName> Glyphs;
	UPROPERTY(BlueprintReadOnly, Category = "Input") TArray<FText> Labels;
	UPROPERTY(BlueprintReadOnly, Category = "Input") bool bHold = false;
	UPROPERTY(BlueprintReadOnly, Category = "Input") bool bChord = false;
	UPROPERTY(BlueprintReadOnly, Category = "Input") bool bMissingGlyph = false;
	UPROPERTY(BlueprintReadOnly, Category = "Input") bool bUnknownKey = false;
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocActionPresentation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Input") FName ActionId;
	UPROPERTY(BlueprintReadOnly, Category = "Input") EDocInputDeviceCategory Device = EDocInputDeviceCategory::Unknown;
	UPROPERTY(BlueprintReadOnly, Category = "Input") TArray<FDocBindingPresentation> Alternates;
	UPROPERTY(BlueprintReadOnly, Category = "Input") bool bUnbound = true;
	/** Binding revision this presentation was resolved at. */
	UPROPERTY(BlueprintReadOnly, Category = "Input") int64 Revision = 0;
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocRemapConflict
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Input") FName ActionId;
	UPROPERTY(BlueprintReadOnly, Category = "Input") FKey Key;
	UPROPERTY(BlueprintReadOnly, Category = "Input") bool bReserved = false;
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocInputRemapSaveData
{
	GENERATED_BODY()

	static constexpr int32 CurrentSchemaVersion = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Input") int32 SchemaVersion = CurrentSchemaVersion;
	/** Only actions that differ from their defaults. */
	UPROPERTY(BlueprintReadOnly, Category = "Input") TArray<FDocActionBindings> Overrides;
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EDocSettingCategory : uint8
{
	Video,
	Audio,
	Input,
	Gameplay,
	Accessibility,
	Language
};

UENUM(BlueprintType)
enum class EDocSettingType : uint8
{
	Bool,
	Int,
	Float,
	Enum,
	String
};

UENUM(BlueprintType)
enum class EDocSettingScope : uint8
{
	/** One shared authority per machine/display (resolution, window mode...). */
	Machine,
	/** Per player profile (sensitivity, remaps, accessibility preferences). */
	Profile
};

UENUM(BlueprintType)
enum class EDocSettingApplyPolicy : uint8
{
	Immediate,
	/** Applied as a preview with a real-time keep/revert deadline (display mode, resolution). */
	PreviewConfirm,
	/** Stored now, applied by the provider on next launch. */
	RequiresRestart
};

/** A typed setting value. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocSettingValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") EDocSettingType Type = EDocSettingType::Bool;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") bool BoolValue = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") int64 IntValue = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") double FloatValue = 0.0;
	/** Enum option or free string. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") FString StringValue;

	static FDocSettingValue MakeBool(bool V) { FDocSettingValue R; R.Type = EDocSettingType::Bool; R.BoolValue = V; return R; }
	static FDocSettingValue MakeInt(int64 V) { FDocSettingValue R; R.Type = EDocSettingType::Int; R.IntValue = V; return R; }
	static FDocSettingValue MakeFloat(double V) { FDocSettingValue R; R.Type = EDocSettingType::Float; R.FloatValue = V; return R; }
	static FDocSettingValue MakeEnum(const FString& V) { FDocSettingValue R; R.Type = EDocSettingType::Enum; R.StringValue = V; return R; }
	static FDocSettingValue MakeString(const FString& V) { FDocSettingValue R; R.Type = EDocSettingType::String; R.StringValue = V; return R; }

	/** Canonical text form (store format). */
	FString ToString() const;
	/** Parses Text as Type. Returns false for malformed or non-finite input. */
	static bool FromString(EDocSettingType Type, const FString& Text, FDocSettingValue& Out);

	friend bool operator==(const FDocSettingValue& A, const FDocSettingValue& B)
	{
		if (A.Type != B.Type) { return false; }
		switch (A.Type)
		{
		case EDocSettingType::Bool: return A.BoolValue == B.BoolValue;
		case EDocSettingType::Int: return A.IntValue == B.IntValue;
		case EDocSettingType::Float: return FMath::IsNearlyEqual(A.FloatValue, B.FloatValue, 1e-6);
		default: return A.StringValue == B.StringValue;
		}
	}
	friend bool operator!=(const FDocSettingValue& A, const FDocSettingValue& B) { return !(A == B); }
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocSettingDescriptor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") FName SettingId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") FText Help;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") EDocSettingCategory Category = EDocSettingCategory::Gameplay;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") EDocSettingType Type = EDocSettingType::Bool;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") FDocSettingValue Default;
	/** Int/Float range (ignored when Min > Max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") double Min = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") double Max = -1.0;
	/** Enum options (stable ids, not display text). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") TArray<FString> Options;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") EDocSettingScope Scope = EDocSettingScope::Profile;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") EDocSettingApplyPolicy ApplyPolicy = EDocSettingApplyPolicy::Immediate;
	/** Registered provider id; a missing provider makes the setting unavailable (with a reason). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") FName ProviderId;
	/** Enabled only while DependsOnSetting equals DependsOnValue. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") FName DependsOnSetting;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings") FDocSettingValue DependsOnValue;

	bool HasRange() const { return Min <= Max; }
};

/** Read-only view of a setting for one scope. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocSettingState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Settings") FDocSettingDescriptor Descriptor;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") FDocSettingValue Confirmed;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") bool bAvailable = true;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") FText UnavailableReason;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") bool bEnabledByDependency = true;
	/** A consumer reported it applied the confirmed value (a toggle alone proves nothing). */
	UPROPERTY(BlueprintReadOnly, Category = "Settings") bool bConsumerAcknowledged = false;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") bool bRestartPending = false;
	/** Per-field revision (conflict detection). */
	UPROPERTY(BlueprintReadOnly, Category = "Settings") int64 Revision = 0;
};

UENUM(BlueprintType)
enum class EDocSettingsEditState : uint8
{
	Editing,
	/** Preview applied; waiting for keep/revert before the real-time deadline. */
	Previewing,
	Applied,
	Reverted,
	Cancelled,
	/** A revert failed: the host must show recovery UI; never reported as success. */
	RecoveryRequired
};

USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocSettingsEditView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Settings") FDocRequestHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") EDocSettingsEditState State = EDocSettingsEditState::Editing;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") TMap<FName, FDocSettingValue> Pending;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") TArray<FName> Previewed;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") double ConfirmSecondsRemaining = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Settings") FString Diagnostic;
};

/** Persisted settings: confirmed values plus unconfirmed-preview recovery metadata. */
USTRUCT(BlueprintType)
struct DOCGAMEFRAMEWORKUIRUNTIME_API FDocSettingsStoreData
{
	GENERATED_BODY()

	/** Key "M:<SettingId>" or "P:<OwnerScope>:<SettingId>" -> canonical value text. Unknown keys are preserved. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings") TMap<FString, FString> Values;
	/** Keys previewed but never confirmed; recovery applies the confirmed value on next launch. */
	UPROPERTY(BlueprintReadOnly, Category = "Settings") TArray<FString> PendingPreview;
};

// ---------------------------------------------------------------------------
// Settings object
// ---------------------------------------------------------------------------

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Game Framework UI"))
class DOCGAMEFRAMEWORKUIRUNTIME_API UDocUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDocUISettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	const FDocUILayerPolicy* FindLayer(const FGameplayTag& LayerTag) const
	{
		return Layers.FindByPredicate([&LayerTag](const FDocUILayerPolicy& L) { return L.LayerTag == LayerTag; });
	}

	UPROPERTY(Config, EditAnywhere, Category = "Screens") TArray<FDocUILayerPolicy> Layers;
	/** Drive dialog timeouts, notifications and loading-owner checks from the core real-time ticker. */
	UPROPERTY(Config, EditAnywhere, Category = "Screens") bool bAutoTick = true;
	UPROPERTY(Config, EditAnywhere, Category = "Screens") int32 ModalControlPriority = 200;
	/** Bounded history of terminal screen states kept for GetScreenState. */
	UPROPERTY(Config, EditAnywhere, Category = "Screens", meta = (ClampMin = "8")) int32 TerminalStateHistory = 256;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "1")) int32 MaxVisibleNotifications = 3;
	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "1")) int32 MaxQueuedNotifications = 32;
	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.1")) float DefaultNotificationSeconds = 4.f;
	/** Anti-starvation: queued entries gain +1 effective priority per interval. 0 = strict priority. */
	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0")) float NotificationAgingSeconds = 10.f;

	/** Analog magnitude that counts as a deliberate gamepad input. */
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ClampMin = "0", ClampMax = "1")) float GamepadSwitchThreshold = 0.35f;
	/** Mouse movement (pixels per sample) that counts as deliberate. */
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ClampMin = "0")) float MouseSwitchThreshold = 4.f;
	/** Presentation used for gamepads without verified metadata (unless overridden per player). */
	UPROPERTY(Config, EditAnywhere, Category = "Input") EDocInputDeviceCategory UnverifiedGamepadPresentation = EDocInputDeviceCategory::GenericGamepad;
	/** Recovery when the current device disconnects. */
	UPROPERTY(Config, EditAnywhere, Category = "Input") EDocInputDeviceCategory DisconnectFallback = EDocInputDeviceCategory::KeyboardMouse;

	/** Keep/revert deadline for PreviewConfirm settings (real time; runs while paused). */
	UPROPERTY(Config, EditAnywhere, Category = "Settings", meta = (ClampMin = "3")) float PreviewConfirmSeconds = 15.f;
	/** Global pause is refused in networked worlds unless explicitly allowed by the host. */
	UPROPERTY(Config, EditAnywhere, Category = "Pause") bool bAllowGlobalPauseInListenServer = false;
};
