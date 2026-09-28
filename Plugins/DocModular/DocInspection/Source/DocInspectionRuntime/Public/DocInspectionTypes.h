#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "UObject/Interface.h"
#include "DocRequestHandle.h"
#include "DocSystemResult.h"
#include "DocInspectionTypes.generated.h"

class UStaticMesh;
class UTexture2D;
class USoundBase;
class UTextureRenderTarget2D;
class UInputMappingContext;
class UInputAction;
class ULocalPlayer;

namespace DocInspectionTags
{
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Inspection);
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Object3D);
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Document);
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Image);
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Book);
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio);
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Video);
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WorldDetail);
	DOCINSPECTIONRUNTIME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Custom);
}

/** Session lifecycle (handoff 12.4). */
UENUM(BlueprintType)
enum class EDocInspectionState : uint8
{
	None,
	Opening,
	Loading,
	Active,
	Closing,
	Closed,
	Failed,
	Cancelled,
	Cleanup
};

UENUM(BlueprintType)
enum class EDocInspectionMode : uint8
{
	/** Observe the live object in place (the camera provider frames it; the actor is never moved). */
	World,
	/** Render a presentation copy on an isolated preview stage into a session-owned render target. */
	Preview
};

UENUM(BlueprintType)
enum class EDocInspectionOpenPolicy : uint8
{
	/** Opening while a session is active closes the old one first. */
	ReplaceExisting,
	/** Opening while a session is active is refused. */
	RejectWhileActive
};

UENUM(BlueprintType)
enum class EDocInspectionAudioState : uint8
{
	None,
	Playing,
	Paused,
	Finished
};

USTRUCT(BlueprintType)
struct DOCINSPECTIONRUNTIME_API FDocInspectionPage
{
	GENERATED_BODY()

	/** Accessible, localizable text. Prefer this over text baked into an image. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Page", meta = (MultiLine = "true"))
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Page")
	TSoftObjectPtr<UTexture2D> Image;
};

USTRUCT(BlueprintType)
struct DOCINSPECTIONRUNTIME_API FDocInspectionFocusPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Focus")
	FName FocusId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Focus", meta = (MultiLine = "true"))
	FText RevealText;

	/** Offset from the inspected object's origin (cm, local space). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Focus")
	FVector LocalOffset = FVector::ZeroVector;

	/** Focus points that must be discovered before this one is available. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Focus")
	TArray<FName> RequiresDiscovered;

	/** Requested rewards; granted only through an authorized reward provider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Focus")
	FGameplayTagContainer GrantTags;
};

USTRUCT(BlueprintType)
struct DOCINSPECTIONRUNTIME_API FDocInspectionLimits
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits") bool bAllowRotate = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits") float MinPitch = -80.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits") float MaxPitch = 80.f;
	/** 0 = unlimited yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits") float YawRange = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits") bool bAllowPan = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (Units = "cm")) float MaxPan = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (Units = "cm")) float MinDistance = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (Units = "cm")) float MaxDistance = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (Units = "cm")) float DefaultDistance = 120.f;
};

/** Shared, immutable inspection content (handoff 12.2). Per-player state never lives here. */
UCLASS(BlueprintType)
class DOCINSPECTIONRUNTIME_API UDocInspectionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection")
	FName InspectionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection", meta = (Categories = "Inspection"))
	FGameplayTag ContentType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection")
	EDocInspectionMode DefaultMode = EDocInspectionMode::Preview;

	/** Presentation mesh for Preview mode (never the live actor's class). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TSoftObjectPtr<UStaticMesh> PreviewMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TSoftObjectPtr<UTexture2D> Image;

	/** Documents/books: ordered pages. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TArray<FDocInspectionPage> Pages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TSoftObjectPtr<USoundBase> AudioLog;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content", meta = (MultiLine = "true"))
	FText Transcript;

	/** Keep an audio log playing after the session closes (moved to a longer-lived owner). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	bool bAudioContinuesAfterClose = false;

	/** Media source object for the DocInspectionMedia bridge (video). Base: Unsupported without a media provider. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TSoftObjectPtr<UObject> MediaSource;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "View")
	FDocInspectionLimits Limits;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "View")
	TArray<FDocInspectionFocusPoint> FocusPoints;

	/** Opt-in coordinated world pause (Control.Pause claim through the player's control provider). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Session")
	bool bRequestWorldPause = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inspection")
	FGameplayTagContainer SemanticTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility")
	bool bHighContrastAvailable = true;

	void GatherAssetPaths(EDocInspectionMode Mode, TArray<FSoftObjectPath>& Out) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

USTRUCT(BlueprintType)
struct DOCINSPECTIONRUNTIME_API FDocInspectionFocusView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FName FocusId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FText RevealText;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") bool bAvailable = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") bool bDiscovered = false;
};

/** Complete presentation state for any front end (UMG example, CommonUI bridge...). */
USTRUCT(BlueprintType)
struct DOCINSPECTIONRUNTIME_API FDocInspectionViewModel
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FDocRequestHandle Session;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") EDocInspectionState State = EDocInspectionState::None;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") EDocInspectionMode Mode = EDocInspectionMode::World;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FName InspectionId;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FGameplayTag ContentType;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FText Title;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FText Description;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") int32 PageIndex = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") int32 PageCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FText PageText;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") TObjectPtr<UTexture2D> PageImage;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") TObjectPtr<UTexture2D> Image;
	/** Preview mode output (session-owned). */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") TObjectPtr<UTextureRenderTarget2D> PreviewTarget;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FRotator ViewRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FVector2D Pan = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") float Distance = 0.f;
	/** World mode: the view the camera provider should use to frame the object. */
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FTransform WorldViewTransform;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") TArray<FDocInspectionFocusView> FocusPoints;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") EDocInspectionAudioState AudioState = EDocInspectionAudioState::None;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FText Transcript;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") bool bHighContrast = false;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") float TextScale = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Doc|Inspection") FString Diagnostic;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocInspectionViewEvent, const FDocInspectionViewModel&, ViewModel);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocInspectionViewNative, const FDocInspectionViewModel&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocInspectionDiscoveryEvent, FName, InspectionId, FName, FocusId);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocInspectionDiscoveryNative, FName, FName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocInspectionClosedEvent, FDocRequestHandle, Session, EDocInspectionState, EndState);

/** Authorized reward path for focus-point grants (host/bridge; typically server-validated). */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDocInspectionRewardProvider : public UInterface
{
	GENERATED_BODY()
};

class DOCINSPECTIONRUNTIME_API IDocInspectionRewardProvider
{
	GENERATED_BODY()

public:
	/** Request grants for a discovery. The provider authorizes; the inspection system never mutates authoritative state itself. */
	virtual FDocSystemResult RequestDocInspectionGrant(ULocalPlayer* Player, FName InspectionId, FName FocusId, const FGameplayTagContainer& Tags) = 0;
};

/** Media playback supplied by the DocInspectionMedia bridge (video, seekable audio beyond the base). */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDocInspectionMediaProvider : public UInterface
{
	GENERATED_BODY()
};

class DOCINSPECTIONRUNTIME_API IDocInspectionMediaProvider
{
	GENERATED_BODY()

public:
	virtual bool CanPlayDocInspectionMedia(const UDocInspectionDefinition* Definition) const = 0;
	virtual FDocSystemResult OpenDocInspectionMedia(int64 SessionId, const UDocInspectionDefinition* Definition) = 0;
	virtual void CloseDocInspectionMedia(int64 SessionId) = 0;
};

/** Project Settings → Plugins → Doc Inspection. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Doc Inspection"))
class DOCINSPECTIONRUNTIME_API UDocInspectionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(Config, EditAnywhere, Category = "Session")
	EDocInspectionOpenPolicy OpenPolicy = EDocInspectionOpenPolicy::ReplaceExisting;

	UPROPERTY(Config, EditAnywhere, Category = "Session", meta = (ClampMin = "0.0"))
	float LoadTimeoutSeconds = 10.f;

	/** Capabilities claimed through the player's control provider while inspecting. */
	UPROPERTY(Config, EditAnywhere, Category = "Control")
	FGameplayTagContainer ClaimedCapabilities;

	UPROPERTY(Config, EditAnywhere, Category = "Control")
	int32 ControlPriority = 50;

	/** Temporary mapping context added while a session is active (only if not already installed). */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TSoftObjectPtr<UInputMappingContext> InputContext;

	UPROPERTY(Config, EditAnywhere, Category = "Input")
	int32 InputContextPriority = 100;

	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UInputAction> RotateAction;
	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UInputAction> PanAction;
	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UInputAction> ZoomAction;
	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UInputAction> NextAction;
	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UInputAction> PreviousAction;
	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UInputAction> AcceptAction;
	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UInputAction> BackAction;
	UPROPERTY(Config, EditAnywhere, Category = "Input") TSoftObjectPtr<UInputAction> ActivateFocusAction;

	UPROPERTY(Config, EditAnywhere, Category = "Preview", meta = (ClampMin = "64", ClampMax = "4096"))
	int32 RenderTargetSize = 1024;

	/** Maximum preview captures per second while the view changes; no capture while unchanged. */
	UPROPERTY(Config, EditAnywhere, Category = "Preview", meta = (ClampMin = "1", ClampMax = "120"))
	float MaxCapturesPerSecond = 30.f;

	/** World location of the isolated preview stage (far from gameplay space). */
	UPROPERTY(Config, EditAnywhere, Category = "Preview")
	FVector PreviewStageLocation = FVector(0.f, 0.f, -100000.f);

	UPROPERTY(Config, EditAnywhere, Category = "Preview", meta = (ClampMin = "0.0"))
	float PreviewLightIntensity = 5000.f;

	UPROPERTY(Config, EditAnywhere, Category = "Accessibility", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float TextScale = 1.f;

	UPROPERTY(Config, EditAnywhere, Category = "Accessibility")
	bool bHighContrast = false;
};
