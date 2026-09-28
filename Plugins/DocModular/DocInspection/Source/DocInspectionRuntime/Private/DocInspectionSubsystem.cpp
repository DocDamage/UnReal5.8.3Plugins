#include "DocInspectionSubsystem.h"
#include "DocInspectionLog.h"
#include "DocPlayerControlSubsystem.h"
#include "DocCoreTags.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/App.h"
#include "Misc/DataValidation.h"
#include "Sound/SoundBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInspectionSubsystem)

namespace DocInspectionTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Inspection, "Inspection", "DocInspection content types");
	UE_DEFINE_GAMEPLAY_TAG(Object3D, "Inspection.Object3D");
	UE_DEFINE_GAMEPLAY_TAG(Document, "Inspection.Document");
	UE_DEFINE_GAMEPLAY_TAG(Image, "Inspection.Image");
	UE_DEFINE_GAMEPLAY_TAG(Book, "Inspection.Book");
	UE_DEFINE_GAMEPLAY_TAG(Audio, "Inspection.Audio");
	UE_DEFINE_GAMEPLAY_TAG(Video, "Inspection.Video");
	UE_DEFINE_GAMEPLAY_TAG(WorldDetail, "Inspection.WorldDetail");
	UE_DEFINE_GAMEPLAY_TAG(Custom, "Inspection.Custom");
}

// ---------------------------------------------------------------------------
// Definition / component
// ---------------------------------------------------------------------------

void UDocInspectionDefinition::GatherAssetPaths(EDocInspectionMode Mode, TArray<FSoftObjectPath>& Out) const
{
	if (Mode == EDocInspectionMode::Preview && !PreviewMesh.IsNull()) { Out.AddUnique(PreviewMesh.ToSoftObjectPath()); }
	if (!Image.IsNull()) { Out.AddUnique(Image.ToSoftObjectPath()); }
	for (const FDocInspectionPage& Page : Pages)
	{
		if (!Page.Image.IsNull()) { Out.AddUnique(Page.Image.ToSoftObjectPath()); }
	}
	if (!AudioLog.IsNull()) { Out.AddUnique(AudioLog.ToSoftObjectPath()); }
}

#if WITH_EDITOR
EDataValidationResult UDocInspectionDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (InspectionId.IsNone() || !ContentType.IsValid())
	{
		Context.AddError(FText::FromString(TEXT("InspectionId and ContentType are required")));
		Result = EDataValidationResult::Invalid;
	}
	if (ContentType == DocInspectionTags::Object3D && DefaultMode == EDocInspectionMode::Preview && PreviewMesh.IsNull())
	{
		Context.AddWarning(FText::FromString(TEXT("Preview mode without a PreviewMesh: the inspectable's own static mesh is used if it has one")));
	}
	TSet<FName> Ids;
	for (const FDocInspectionFocusPoint& Point : FocusPoints)
	{
		bool bDuplicate = false;
		Ids.Add(Point.FocusId, &bDuplicate);
		if (Point.FocusId.IsNone() || bDuplicate)
		{
			Context.AddError(FText::FromString(TEXT("Focus ids must be set and unique")));
			Result = EDataValidationResult::Invalid;
		}
	}
	if (Limits.MinDistance > Limits.MaxDistance)
	{
		Context.AddError(FText::FromString(TEXT("MinDistance > MaxDistance")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

EDocInspectionMode UDocInspectableComponent::GetEffectiveMode() const
{
	if (bOverrideMode) { return Mode; }
	return Definition ? Definition->DefaultMode : EDocInspectionMode::World;
}

// ---------------------------------------------------------------------------
// Preview stage
// ---------------------------------------------------------------------------

ADocInspectionPreviewStage::ADocInspectionPreviewStage()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
	bReplicates = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetRelativeLocation(FVector(-200.f, 150.f, 200.f));
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
}

void ADocInspectionPreviewStage::Setup(UStaticMesh* InMesh, UTextureRenderTarget2D* Target, float LightIntensity)
{
	Mesh->SetStaticMesh(InMesh);
	Light->SetIntensity(LightIntensity);
	Capture->TextureTarget = Target;
	Capture->ShowOnlyActors.Reset();
	Capture->ShowOnlyActors.Add(this); // isolate: nothing from the gameplay world is captured
}

void ADocInspectionPreviewStage::ApplyView(const FRotator& Orbit, const FVector2D& PanOffset, float Distance)
{
	const FBoxSphereBounds Bounds = Mesh->GetStaticMesh() ? Mesh->Bounds : FBoxSphereBounds(GetActorLocation(), FVector(50.f), 50.f);
	const FVector Center = Bounds.Origin;
	const FVector Direction = Orbit.Vector();
	const FRotator LookRot = (-Direction).Rotation();
	const FVector Right = FRotationMatrix(LookRot).GetUnitAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(LookRot).GetUnitAxis(EAxis::Z);
	const FVector Location = Center + Direction * Distance + Right * PanOffset.X + Up * PanOffset.Y;
	Capture->SetWorldLocationAndRotation(Location, LookRot);
}

void ADocInspectionPreviewStage::CaptureNow()
{
	if (Capture->TextureTarget && FApp::CanEverRender())
	{
		Capture->CaptureScene();
	}
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocInspectionSubsystem* UDocInspectionSubsystem::Get(const ULocalPlayer* Player)
{
	return Player ? Player->GetSubsystem<UDocInspectionSubsystem>() : nullptr;
}

void UDocInspectionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocInspectionSubsystem::InitializeForTesting(UWorld* World)
{
	WorldOverride = World;
}

UWorld* UDocInspectionSubsystem::GetWorld() const
{
	if (UWorld* Override = WorldOverride.Get())
	{
		return Override;
	}
	const ULocalPlayer* Player = GetLocalPlayer();
	return Player ? Player->GetWorld() : nullptr;
}

void UDocInspectionSubsystem::Deinitialize()
{
	bDeinitializing = true;
	End(EDocInspectionState::Cancelled, TEXT("Player removed"));
	for (UAudioComponent* Audio : ContinuedAudio)
	{
		if (IsValid(Audio)) { Audio->Stop(); Audio->DestroyComponent(); }
	}
	ContinuedAudio.Reset();
	Super::Deinitialize();
}

void UDocInspectionSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	// Travel / possession change: the old controller's input stack and claims are no longer valid.
	if (Session.IsSet())
	{
		End(EDocInspectionState::Cancelled, TEXT("Player controller changed (travel)"));
	}
	for (UAudioComponent* Audio : ContinuedAudio)
	{
		if (IsValid(Audio) && Audio->GetWorld() != GetWorld()) { Audio->Stop(); Audio->DestroyComponent(); }
	}
	ContinuedAudio.RemoveAll([](const TObjectPtr<UAudioComponent>& A) { return !IsValid(A) || A->IsBeingDestroyed(); });
}

TStatId UDocInspectionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocInspectionSubsystem, STATGROUP_Tickables);
}

void UDocInspectionSubsystem::SetRewardProvider(UObject* Provider)
{
	RewardProvider = (Provider && Cast<IDocInspectionRewardProvider>(Provider)) ? Provider : nullptr;
}

void UDocInspectionSubsystem::SetMediaProvider(UObject* Provider)
{
	MediaProvider = (Provider && Cast<IDocInspectionMediaProvider>(Provider)) ? Provider : nullptr;
}

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------

FDocSystemResult UDocInspectionSubsystem::OpenInspection(UDocInspectableComponent* Inspectable, FDocRequestHandle& OutSession)
{
	if (!Inspectable || !Inspectable->Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Inspectable with a Definition required"));
	}
	return Open(Inspectable->Definition, Inspectable, OutSession);
}

FDocSystemResult UDocInspectionSubsystem::OpenDefinition(UDocInspectionDefinition* Definition, FDocRequestHandle& OutSession)
{
	if (!Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Definition required"));
	}
	return Open(Definition, nullptr, OutSession);
}

FDocSystemResult UDocInspectionSubsystem::Open(UDocInspectionDefinition* Definition, UDocInspectableComponent* Inspectable, FDocRequestHandle& OutSession)
{
	OutSession = FDocRequestHandle();
	if (bDeinitializing || !GetWorld())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No world for this local player"));
	}
	if (Session.IsSet())
	{
		if (GetDefault<UDocInspectionSettings>()->OpenPolicy == EDocInspectionOpenPolicy::RejectWhileActive)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("An inspection is already open for this player"));
		}
		End(EDocInspectionState::Closed, TEXT("Replaced by a new inspection"));
	}
	if (Definition->ContentType.MatchesTag(DocInspectionTags::Video))
	{
		const IDocInspectionMediaProvider* Media = Cast<IDocInspectionMediaProvider>(MediaProvider.Get());
		if (!Media || !Media->CanPlayDocInspectionMedia(Definition))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Video inspection needs the DocInspectionMedia bridge"));
		}
	}

	FSession NewSession;
	NewSession.Id = FDocHandleAllocator::NextOperationId();
	NewSession.Definition = Definition;
	NewSession.Inspectable = Inspectable;
	NewSession.Mode = Inspectable ? Inspectable->GetEffectiveMode() : EDocInspectionMode::Preview;
	if (!Inspectable && NewSession.Mode == EDocInspectionMode::World)
	{
		NewSession.Mode = EDocInspectionMode::Preview;
	}
	NewSession.Distance = FMath::Clamp(Definition->Limits.DefaultDistance, Definition->Limits.MinDistance, Definition->Limits.MaxDistance);
	NewSession.State = EDocInspectionState::Opening;
	const float Timeout = GetDefault<UDocInspectionSettings>()->LoadTimeoutSeconds;
	NewSession.LoadDeadline = Timeout > 0.f ? Clock + Timeout : 0.0;
	const int64 Id = NewSession.Id;
	Session = MoveTemp(NewSession);
	SessionDefinitionRef = Definition;
	OutSession = FDocHandleAllocator::MakeHandle(Id, 1);
	Broadcast();

	TArray<FSoftObjectPath> Paths;
	Definition->GatherAssetPaths(Session->Mode, Paths);
	Paths.RemoveAll([](const FSoftObjectPath& P) { return P.ResolveObject() != nullptr; });
	Session->State = EDocInspectionState::Loading;
	if (Paths.Num() == 0)
	{
		OnAssetsLoaded(Id);
		return FDocSystemResult::MakeSuccess();
	}
	Broadcast();
	TWeakObjectPtr<UDocInspectionSubsystem> WeakThis(this);
	auto Done = [WeakThis, Id]() { if (UDocInspectionSubsystem* Self = WeakThis.Get()) { Self->OnAssetsLoaded(Id); } };
	if (LoaderOverride)
	{
		LoaderOverride(Paths, Done);
	}
	else
	{
		TSharedPtr<FStreamableHandle> Handle = Streamable.RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda(Done));
		if (Session.IsSet() && Session->Id == Id) { Session->Load = Handle; }
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocInspectionSubsystem::OnAssetsLoaded(int64 SessionId)
{
	if (!Session.IsSet() || Session->Id != SessionId || Session->State != EDocInspectionState::Loading)
	{
		return; // closed during load, or replaced: never activates a stale session
	}
	const UDocInspectionDefinition* Definition = Session->Definition;
	TArray<FSoftObjectPath> Paths;
	Definition->GatherAssetPaths(Session->Mode, Paths);
	LoadedAssets.Reset();
	for (const FSoftObjectPath& Path : Paths)
	{
		UObject* Asset = Path.ResolveObject();
		if (!Asset)
		{
			End(EDocInspectionState::Failed, FString::Printf(TEXT("Failed to load %s"), *Path.ToString()));
			return;
		}
		LoadedAssets.Add(Asset);
	}
	Session->Load.Reset();
	Activate();
}

void UDocInspectionSubsystem::Activate()
{
	FSession& S = *Session;
	UWorld* World = GetWorld();
	const UDocInspectionSettings* Settings = GetDefault<UDocInspectionSettings>();

	if (S.Mode == EDocInspectionMode::Preview && S.Definition->ContentType.MatchesTag(DocInspectionTags::Object3D))
	{
		UStaticMesh* Mesh = S.Definition->PreviewMesh.Get();
		if (!Mesh)
		{
			// Presentation copy of the inspectable's own static mesh (never its actor class).
			if (const UDocInspectableComponent* Inspectable = S.Inspectable.Get())
			{
				if (const AActor* Owner = Inspectable->GetOwner())
				{
					if (const UStaticMeshComponent* Source = Owner->FindComponentByClass<UStaticMeshComponent>())
					{
						Mesh = Source->GetStaticMesh();
					}
				}
			}
		}
		if (!Mesh)
		{
			End(EDocInspectionState::Failed, TEXT("Preview mode needs a PreviewMesh or an inspectable with a static mesh"));
			return;
		}
		if (!RenderTarget)
		{
			RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, Settings->RenderTargetSize, Settings->RenderTargetSize);
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		ADocInspectionPreviewStage* NewStage = World->SpawnActor<ADocInspectionPreviewStage>(ADocInspectionPreviewStage::StaticClass(),
			FTransform(Settings->PreviewStageLocation), Params);
		if (!NewStage)
		{
			End(EDocInspectionState::Failed, TEXT("Could not spawn the preview stage"));
			return;
		}
		NewStage->Setup(Mesh, RenderTarget, Settings->PreviewLightIntensity);
		Stage = NewStage;
	}

	if (S.Definition->ContentType.MatchesTag(DocInspectionTags::Video))
	{
		if (IDocInspectionMediaProvider* Media = Cast<IDocInspectionMediaProvider>(MediaProvider.Get()))
		{
			const FDocSystemResult Opened = Media->OpenDocInspectionMedia(S.Id, S.Definition);
			if (!Opened.IsSuccess())
			{
				End(EDocInspectionState::Failed, Opened.ToString());
				return;
			}
			S.bMediaOpen = true;
		}
	}

	AcquireControl();
	InstallInput();
	S.State = EDocInspectionState::Active;
	S.bViewDirty = true;
	UpdateView(/*bForceCapture*/ true);
	Broadcast();
}

FDocSystemResult UDocInspectionSubsystem::CloseInspection(FDocRequestHandle Handle)
{
	if (!Session.IsSet() || Handle.GetOperationId() != Session->Id)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Session not open"));
	}
	const bool bDuringLoad = Session->State == EDocInspectionState::Opening || Session->State == EDocInspectionState::Loading;
	End(bDuringLoad ? EDocInspectionState::Cancelled : EDocInspectionState::Closed, bDuringLoad ? TEXT("Closed during load") : TEXT("Closed"));
	return FDocSystemResult::MakeSuccess();
}

void UDocInspectionSubsystem::End(EDocInspectionState EndState, const FString& Reason)
{
	if (!Session.IsSet())
	{
		return;
	}
	FSession& S = *Session;
	S.Diagnostic = Reason;
	S.State = (EndState == EDocInspectionState::Closed) ? EDocInspectionState::Closing : EndState;
	Broadcast();
	if (EndState != EDocInspectionState::Closed)
	{
		S.State = EDocInspectionState::Cleanup;
	}

	if (S.Load.IsValid())
	{
		S.Load->CancelHandle();
		S.Load.Reset();
	}
	RemoveInput();
	for (const TPair<TWeakObjectPtr<ULocalPlayer>, FDocRequestHandle>& Claim : S.ControlClaims)
	{
		if (UDocPlayerControlSubsystem* Control = UDocPlayerControlSubsystem::Get(Claim.Key.Get()))
		{
			Control->ReleaseControl(Claim.Value); // only this session's claim; other owners keep theirs
		}
	}
	S.ControlClaims.Reset();
	if (ADocInspectionPreviewStage* OldStage = Stage.Get())
	{
		const UWorld* World = OldStage->GetWorld();
		if (World && !World->bIsTearingDown) { OldStage->Destroy(); }
	}
	Stage.Reset();
	if (S.bMediaOpen)
	{
		if (IDocInspectionMediaProvider* Media = Cast<IDocInspectionMediaProvider>(MediaProvider.Get()))
		{
			Media->CloseDocInspectionMedia(S.Id);
		}
		S.bMediaOpen = false;
	}
	if (SessionAudio)
	{
		const bool bContinue = S.Definition && S.Definition->bAudioContinuesAfterClose && EndState == EDocInspectionState::Closed
			&& SessionAudio->IsPlaying() && !bDeinitializing;
		if (bContinue)
		{
			ContinuedAudio.Add(SessionAudio); // deliberate transfer to a longer-lived owner
		}
		else
		{
			SessionAudio->Stop();
			SessionAudio->DestroyComponent();
		}
		SessionAudio = nullptr;
	}
	const FDocRequestHandle Handle = FDocHandleAllocator::MakeHandle(S.Id, 1);
	S.State = EDocInspectionState::Closed;
	Broadcast();
	Session.Reset();
	SessionDefinitionRef = nullptr;
	LoadedAssets.Reset();
	OnInspectionEnded.Broadcast(Handle, EndState);
}

bool UDocInspectionSubsystem::IsInspecting() const
{
	return Session.IsSet() && Session->State == EDocInspectionState::Active;
}

// ---------------------------------------------------------------------------
// Control and input ownership
// ---------------------------------------------------------------------------

void UDocInspectionSubsystem::AcquireControl()
{
	const UDocInspectionSettings* Settings = GetDefault<UDocInspectionSettings>();
	FGameplayTagContainer Capabilities = Settings->ClaimedCapabilities;
	if (Session->Definition->bRequestWorldPause)
	{
		Capabilities.AddTag(DocCoreTags::Control_Pause); // coordinated by the provider, never a direct global pause
	}
	ULocalPlayer* Player = GetLocalPlayer();
	UDocPlayerControlSubsystem* Control = Player ? UDocPlayerControlSubsystem::Get(Player) : nullptr;
	if (Capabilities.IsEmpty() || !Control)
	{
		return;
	}
	if (!Control->HasControlProvider())
	{
		Session->Diagnostic = TEXT("No control provider: camera/input/pause not claimed");
		return;
	}
	FDocControlClaimRequest Request;
	Request.LocalPlayer = Player;
	Request.Owner = this;
	Request.Capabilities = Capabilities;
	Request.Priority = Settings->ControlPriority;
	Request.DebugReason = FString::Printf(TEXT("Inspection %s"), *Session->Definition->InspectionId.ToString());
	FDocRequestHandle Claim;
	if (Control->AcquireControl(Request, Claim).IsSuccess() && Claim.IsSet())
	{
		Session->ControlClaims.Add(TPair<TWeakObjectPtr<ULocalPlayer>, FDocRequestHandle>(Player, Claim));
	}
}

void UDocInspectionSubsystem::InstallInput()
{
	const UDocInspectionSettings* Settings = GetDefault<UDocInspectionSettings>();
	ULocalPlayer* Player = GetLocalPlayer();
	APlayerController* PC = Player ? Player->GetPlayerController(GetWorld()) : nullptr;
	if (!PC)
	{
		return; // no controller (tests, dedicated hosts): API-driven navigation only
	}
	if (UEnhancedInputLocalPlayerSubsystem* Input = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
	{
		if (UInputMappingContext* Context = Settings->InputContext.LoadSynchronous())
		{
			// Add only if absent; remember that we added it so we remove only our own.
			if (!Input->HasMappingContext(Context))
			{
				Input->AddMappingContext(Context, Settings->InputContextPriority);
				Session->bAddedMappingContext = true;
			}
		}
	}
	InputComponent = NewObject<UEnhancedInputComponent>(PC, TEXT("DocInspectionInput"), RF_Transient);
	InputComponent->Priority = Settings->InputContextPriority;
	InputComponent->RegisterComponent();
	auto Bind = [this](const TSoftObjectPtr<UInputAction>& Soft, ETriggerEvent Event, auto Method)
	{
		if (UInputAction* Action = Soft.LoadSynchronous())
		{
			InputComponent->BindAction(Action, Event, this, Method);
		}
	};
	Bind(Settings->RotateAction, ETriggerEvent::Triggered, &UDocInspectionSubsystem::HandleRotate);
	Bind(Settings->PanAction, ETriggerEvent::Triggered, &UDocInspectionSubsystem::HandlePan);
	Bind(Settings->ZoomAction, ETriggerEvent::Triggered, &UDocInspectionSubsystem::HandleZoom);
	if (UInputAction* A = Settings->NextAction.LoadSynchronous()) { InputComponent->BindAction(A, ETriggerEvent::Started, this, &UDocInspectionSubsystem::HandleNext); }
	if (UInputAction* A = Settings->PreviousAction.LoadSynchronous()) { InputComponent->BindAction(A, ETriggerEvent::Started, this, &UDocInspectionSubsystem::HandlePrevious); }
	if (UInputAction* A = Settings->AcceptAction.LoadSynchronous()) { InputComponent->BindAction(A, ETriggerEvent::Started, this, &UDocInspectionSubsystem::Accept); }
	if (UInputAction* A = Settings->BackAction.LoadSynchronous()) { InputComponent->BindAction(A, ETriggerEvent::Started, this, &UDocInspectionSubsystem::Back); }
	PC->PushInputComponent(InputComponent);
	InputOwner = PC;
}

void UDocInspectionSubsystem::RemoveInput()
{
	if (InputComponent)
	{
		if (APlayerController* PC = InputOwner.Get())
		{
			PC->PopInputComponent(InputComponent);
		}
		InputComponent->DestroyComponent();
		InputComponent = nullptr;
	}
	InputOwner.Reset();
	if (Session.IsSet() && Session->bAddedMappingContext)
	{
		ULocalPlayer* Player = GetLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Input = Player ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		UInputMappingContext* Context = GetDefault<UDocInspectionSettings>()->InputContext.Get();
		if (Input && Context)
		{
			Input->RemoveMappingContext(Context); // never ClearAllMappings
		}
		Session->bAddedMappingContext = false;
	}
}

void UDocInspectionSubsystem::HandleRotate(const FInputActionValue& Value) { Rotate(Value.Get<FVector2D>()); }
void UDocInspectionSubsystem::HandlePan(const FInputActionValue& Value) { Pan(Value.Get<FVector2D>()); }
void UDocInspectionSubsystem::HandleZoom(const FInputActionValue& Value) { Zoom(Value.Get<float>()); }
void UDocInspectionSubsystem::HandleNext() { NextPage(); }
void UDocInspectionSubsystem::HandlePrevious() { PreviousPage(); }

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void UDocInspectionSubsystem::Rotate(FVector2D Delta)
{
	if (!IsInspecting() || !Session->Definition->Limits.bAllowRotate)
	{
		return;
	}
	const FDocInspectionLimits& L = Session->Definition->Limits;
	FRotator& Orbit = Session->Orbit;
	Orbit.Yaw += Delta.X;
	if (L.YawRange > 0.f) { Orbit.Yaw = FMath::Clamp(Orbit.Yaw, -L.YawRange * 0.5f, L.YawRange * 0.5f); }
	Orbit.Pitch = FMath::Clamp(Orbit.Pitch + Delta.Y, L.MinPitch, L.MaxPitch);
	Session->bViewDirty = true;
	UpdateView(false);
}

void UDocInspectionSubsystem::Pan(FVector2D Delta)
{
	if (!IsInspecting() || !Session->Definition->Limits.bAllowPan)
	{
		return;
	}
	const float Max = Session->Definition->Limits.MaxPan;
	Session->PanOffset.X = FMath::Clamp(Session->PanOffset.X + Delta.X, -Max, Max);
	Session->PanOffset.Y = FMath::Clamp(Session->PanOffset.Y + Delta.Y, -Max, Max);
	Session->bViewDirty = true;
	UpdateView(false);
}

void UDocInspectionSubsystem::Zoom(float Delta)
{
	if (!IsInspecting())
	{
		return;
	}
	const FDocInspectionLimits& L = Session->Definition->Limits;
	Session->Distance = FMath::Clamp(Session->Distance - Delta, L.MinDistance, L.MaxDistance);
	Session->bViewDirty = true;
	UpdateView(false);
}

bool UDocInspectionSubsystem::NextPage()
{
	if (!IsInspecting() || Session->PageIndex + 1 >= Session->Definition->Pages.Num())
	{
		return false;
	}
	++Session->PageIndex;
	Broadcast();
	return true;
}

bool UDocInspectionSubsystem::PreviousPage()
{
	if (!IsInspecting() || Session->PageIndex <= 0)
	{
		return false;
	}
	--Session->PageIndex;
	Broadcast();
	return true;
}

void UDocInspectionSubsystem::Accept()
{
	if (IsInspecting() && Session->Definition->Pages.Num() > 1 && NextPage())
	{
		return;
	}
}

void UDocInspectionSubsystem::Back()
{
	if (Session.IsSet())
	{
		CloseInspection(FDocHandleAllocator::MakeHandle(Session->Id, 1));
	}
}

FTransform UDocInspectionSubsystem::ComputeWorldView() const
{
	const UDocInspectableComponent* Inspectable = Session.IsSet() ? Session->Inspectable.Get() : nullptr;
	if (!Inspectable)
	{
		return FTransform::Identity;
	}
	const FVector Center = Inspectable->GetComponentLocation();
	const FVector Direction = Session->Orbit.Vector();
	const FRotator LookRot = (-Direction).Rotation();
	const FVector Right = FRotationMatrix(LookRot).GetUnitAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(LookRot).GetUnitAxis(EAxis::Z);
	return FTransform(LookRot, Center + Direction * Session->Distance + Right * Session->PanOffset.X + Up * Session->PanOffset.Y);
}

void UDocInspectionSubsystem::UpdateView(bool bForceCapture)
{
	if (!Session.IsSet())
	{
		return;
	}
	if (ADocInspectionPreviewStage* S = Stage.Get())
	{
		S->ApplyView(Session->Orbit, Session->PanOffset, Session->Distance);
		// Throttled capture: only when the view changed, at most MaxCapturesPerSecond.
		if (bForceCapture || (Session->bViewDirty && Clock >= Session->NextCaptureTime))
		{
			S->CaptureNow();
			++CaptureCount;
			Session->bViewDirty = false;
			Session->NextCaptureTime = Clock + 1.0 / FMath::Max(1.f, GetDefault<UDocInspectionSettings>()->MaxCapturesPerSecond);
		}
	}
	else
	{
		Session->bViewDirty = false;
	}
	Broadcast();
}

// ---------------------------------------------------------------------------
// Focus points and discovery
// ---------------------------------------------------------------------------

bool UDocInspectionSubsystem::IsFocusAvailable(const UDocInspectionDefinition* Definition, const FDocInspectionFocusPoint& Point) const
{
	for (const FName& Required : Point.RequiresDiscovered)
	{
		if (!IsFocusDiscovered(Definition->InspectionId, Required))
		{
			return false;
		}
	}
	return true;
}

FDocSystemResult UDocInspectionSubsystem::ActivateFocusPoint(FName FocusId)
{
	if (!IsInspecting())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No active inspection"));
	}
	const UDocInspectionDefinition* Definition = Session->Definition;
	const FDocInspectionFocusPoint* Point = Definition->FocusPoints.FindByPredicate([FocusId](const FDocInspectionFocusPoint& P) { return P.FocusId == FocusId; });
	if (!Point)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("No focus point %s"), *FocusId.ToString()));
	}
	if (IsFocusDiscovered(Definition->InspectionId, FocusId))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already discovered"));
	}
	if (!IsFocusAvailable(Definition, *Point))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Focus point is locked by its dependencies"));
	}
	Discovered.FindOrAdd(Definition->InspectionId).Add(FocusId);
	FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	if (!Point->GrantTags.IsEmpty())
	{
		// Inspecting on a client is not authority: grants go through the authorized provider or not at all.
		if (IDocInspectionRewardProvider* Provider = Cast<IDocInspectionRewardProvider>(RewardProvider.Get()))
		{
			const FDocSystemResult Grant = Provider->RequestDocInspectionGrant(GetLocalPlayer(), Definition->InspectionId, FocusId, Point->GrantTags);
			if (!Grant.IsSuccess()) { Result.Diagnostic = FString::Printf(TEXT("Grant not applied: %s"), *Grant.ToString()); }
		}
		else
		{
			Result.Diagnostic = TEXT("Grant requested but no reward provider is installed");
		}
	}
	OnFocusDiscoveredNative.Broadcast(Definition->InspectionId, FocusId);
	OnFocusDiscovered.Broadcast(Definition->InspectionId, FocusId);
	Broadcast();
	return Result;
}

bool UDocInspectionSubsystem::IsFocusDiscovered(FName InspectionId, FName FocusId) const
{
	const TSet<FName>* Set = Discovered.Find(InspectionId);
	return Set && Set->Contains(FocusId);
}

TMap<FName, TArray<FName>> UDocInspectionSubsystem::GetDiscoveries() const
{
	TMap<FName, TArray<FName>> Out;
	for (const TPair<FName, TSet<FName>>& Pair : Discovered)
	{
		Out.Add(Pair.Key, Pair.Value.Array());
	}
	return Out;
}

void UDocInspectionSubsystem::RestoreDiscoveries(const TMap<FName, TArray<FName>>& In)
{
	Discovered.Reset();
	for (const TPair<FName, TArray<FName>>& Pair : In)
	{
		Discovered.Add(Pair.Key, TSet<FName>(Pair.Value));
	}
}

// ---------------------------------------------------------------------------
// Audio logs
// ---------------------------------------------------------------------------

FDocSystemResult UDocInspectionSubsystem::PlayAudioLog(float StartSeconds)
{
	if (!IsInspecting())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("No active inspection"));
	}
	USoundBase* Sound = Session->Definition->AudioLog.Get();
	if (!Sound)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Definition has no audio log"));
	}
	if (!SessionAudio)
	{
		SessionAudio = UGameplayStatics::CreateSound2D(GetWorld(), Sound, 1.f, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
		if (!SessionAudio)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No audio device"));
		}
	}
	// Seeking = restart at StartSeconds; whether a source honours it depends on the asset (see README).
	SessionAudio->Play(FMath::Max(0.f, StartSeconds));
	Broadcast();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInspectionSubsystem::PauseAudioLog()
{
	if (!SessionAudio || !SessionAudio->IsPlaying())
	{
		return FDocSystemResult::MakeNoChange();
	}
	SessionAudio->SetPaused(true);
	Broadcast();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInspectionSubsystem::ResumeAudioLog()
{
	if (!SessionAudio || !SessionAudio->bIsPaused)
	{
		return FDocSystemResult::MakeNoChange();
	}
	SessionAudio->SetPaused(false);
	Broadcast();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInspectionSubsystem::StopContinuedAudio()
{
	if (ContinuedAudio.Num() == 0)
	{
		return FDocSystemResult::MakeNoChange();
	}
	for (UAudioComponent* Audio : ContinuedAudio)
	{
		if (IsValid(Audio)) { Audio->Stop(); Audio->DestroyComponent(); }
	}
	ContinuedAudio.Reset();
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Tick and view model
// ---------------------------------------------------------------------------

void UDocInspectionSubsystem::TickInspection(float DeltaSeconds)
{
	Clock += FMath::Max(0.f, DeltaSeconds);
	ContinuedAudio.RemoveAll([](const TObjectPtr<UAudioComponent>& A)
	{
		if (IsValid(A) && !A->IsPlaying() && !A->bIsPaused) { A->DestroyComponent(); return true; }
		return !IsValid(A);
	});
	if (!Session.IsSet())
	{
		return;
	}
	if (Session->State == EDocInspectionState::Loading && Session->LoadDeadline > 0.0 && Clock > Session->LoadDeadline)
	{
		End(EDocInspectionState::Failed, TEXT("Load timed out"));
		return;
	}
	if (Session->State == EDocInspectionState::Active)
	{
		if (Session->Inspectable.IsStale())
		{
			End(EDocInspectionState::Cancelled, TEXT("Inspected object was destroyed"));
			return;
		}
		if (Session->bViewDirty)
		{
			UpdateView(false);
		}
	}
}

FDocInspectionViewModel UDocInspectionSubsystem::GetViewModel() const
{
	FDocInspectionViewModel VM;
	const UDocInspectionSettings* Settings = GetDefault<UDocInspectionSettings>();
	VM.TextScale = Settings->TextScale;
	if (!Session.IsSet())
	{
		VM.State = EDocInspectionState::None;
		return VM;
	}
	const FSession& S = *Session;
	const UDocInspectionDefinition* D = S.Definition;
	VM.Session = FDocHandleAllocator::MakeHandle(S.Id, 1);
	VM.State = S.State;
	VM.Mode = S.Mode;
	VM.InspectionId = D->InspectionId;
	VM.ContentType = D->ContentType;
	VM.Title = D->Title;
	VM.Description = D->Description;
	VM.PageCount = D->Pages.Num();
	VM.PageIndex = S.PageIndex;
	if (D->Pages.IsValidIndex(S.PageIndex))
	{
		VM.PageText = D->Pages[S.PageIndex].Text;
		VM.PageImage = D->Pages[S.PageIndex].Image.Get();
	}
	VM.Image = D->Image.Get();
	VM.PreviewTarget = Stage.IsValid() ? RenderTarget.Get() : nullptr;
	VM.ViewRotation = S.Orbit;
	VM.Pan = S.PanOffset;
	VM.Distance = S.Distance;
	VM.WorldViewTransform = S.Mode == EDocInspectionMode::World ? ComputeWorldView() : FTransform::Identity;
	for (const FDocInspectionFocusPoint& Point : D->FocusPoints)
	{
		FDocInspectionFocusView View;
		View.FocusId = Point.FocusId;
		View.bDiscovered = IsFocusDiscovered(D->InspectionId, Point.FocusId);
		View.bAvailable = IsFocusAvailable(D, Point);
		View.RevealText = View.bDiscovered ? Point.RevealText : FText::GetEmpty();
		VM.FocusPoints.Add(View);
	}
	if (SessionAudio)
	{
		VM.AudioState = SessionAudio->bIsPaused ? EDocInspectionAudioState::Paused
			: SessionAudio->IsPlaying() ? EDocInspectionAudioState::Playing : EDocInspectionAudioState::Finished;
	}
	VM.Transcript = D->Transcript;
	VM.bHighContrast = Settings->bHighContrast && D->bHighContrastAvailable;
	VM.Diagnostic = S.Diagnostic;
	return VM;
}

void UDocInspectionSubsystem::Broadcast()
{
	const FDocInspectionViewModel VM = GetViewModel();
	OnViewModelChangedNative.Broadcast(VM);
	OnViewModelChanged.Broadcast(VM);
}
