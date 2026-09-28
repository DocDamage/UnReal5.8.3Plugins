#include "DocUIManagerSubsystem.h"
#include "DocGameFrameworkUILog.h"
#include "DocCoreTags.h"
#include "DocPlayerControlSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocUIManagerSubsystem)

// ---------------------------------------------------------------------------
// Pause leases
// ---------------------------------------------------------------------------

static TWeakObjectPtr<UDocUIPauseSubsystem> GDocUIPauseOverride;

UDocUIPauseSubsystem* UDocUIPauseSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocUIPauseSubsystem* Override = GDocUIPauseOverride.Get())
	{
		return Override;
	}
	const UGameInstance* GameInstance = Cast<UGameInstance>(WorldContextObject);
	if (!GameInstance)
	{
		if (const ULocalPlayer* Player = Cast<ULocalPlayer>(WorldContextObject))
		{
			GameInstance = Player->GetGameInstance();
		}
		else if (const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
		{
			GameInstance = World->GetGameInstance();
		}
	}
	return GameInstance ? GameInstance->GetSubsystem<UDocUIPauseSubsystem>() : nullptr;
}

void UDocUIPauseSubsystem::SetSubsystemOverrideForTesting(UDocUIPauseSubsystem* Override)
{
	GDocUIPauseOverride = Override;
}

void UDocUIPauseSubsystem::Deinitialize()
{
	Leases.Reset();
	Recompute();
	Super::Deinitialize();
}

bool UDocUIPauseSubsystem::IsPauseAllowed() const
{
	if (PolicyOverride.IsSet())
	{
		return PolicyOverride.GetValue();
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	switch (World->GetNetMode())
	{
	case NM_Standalone: return true;
	case NM_ListenServer: return GetDefault<UDocUISettings>()->bAllowGlobalPauseInListenServer;
	default: return false; // clients never pause the server; dedicated servers have no menus
	}
}

FDocSystemResult UDocUIPauseSubsystem::AcquirePause(UObject* Owner, const FString& Reason, FDocRequestHandle& OutLease)
{
	OutLease = FDocRequestHandle();
	if (!IsPauseAllowed())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("World pause is not authorized here (MultiplayerNonPause)"), DocUITags::Error_UI_Pause);
	}
	OutLease = Leases.Add(this, FLease{ Owner, Reason });
	Recompute();
	if (!bApplied)
	{
		Leases.Remove(OutLease, this);
		OutLease = FDocRequestHandle();
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("The world refused to pause"), DocUITags::Error_UI_Pause);
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIPauseSubsystem::ReleasePause(const FDocRequestHandle& Lease)
{
	if (!Leases.Remove(Lease, this))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Unknown or released pause lease"));
	}
	Recompute();
	return FDocSystemResult::MakeSuccess();
}

void UDocUIPauseSubsystem::PruneDeadOwners()
{
	if (Leases.RemoveIf([](const FDocRequestHandle&, const FLease& L) { return !L.Owner.IsValid(); }) > 0)
	{
		Recompute();
	}
}

void UDocUIPauseSubsystem::Recompute()
{
	const bool bWant = Leases.Num() > 0;
	if (bWant == bApplied)
	{
		return;
	}
	bool bOk = false;
	if (ApplierOverride)
	{
		bOk = ApplierOverride(bWant);
	}
	else if (const UGameInstance* GameInstance = GetGameInstance())
	{
		bOk = UGameplayStatics::SetGamePaused(GameInstance->GetWorld(), bWant);
	}
	if (bOk)
	{
		bApplied = bWant;
	}
}

// ---------------------------------------------------------------------------
// Manager: lifetime and helpers
// ---------------------------------------------------------------------------

UDocUIManagerSubsystem::FApiScope::FApiScope(UDocUIManagerSubsystem& InOwner)
	: Owner(InOwner)
{
	++Owner.ApiDepth;
}

UDocUIManagerSubsystem::FApiScope::~FApiScope()
{
	if (--Owner.ApiDepth == 0)
	{
		Owner.Flush();
	}
}

UDocUIManagerSubsystem* UDocUIManagerSubsystem::Get(const ULocalPlayer* LocalPlayer)
{
	return LocalPlayer ? LocalPlayer->GetSubsystem<UDocUIManagerSubsystem>() : nullptr;
}

void UDocUIManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GetDefault<UDocUISettings>()->bAutoTick)
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDocUIManagerSubsystem::Tick));
	}
}

void UDocUIManagerSubsystem::Deinitialize()
{
	Shutdown();
	Super::Deinitialize();
}

bool UDocUIManagerSubsystem::Tick(float DeltaSeconds)
{
	AdvanceClock(DeltaSeconds); // core ticker: real time, keeps running while the world is paused
	return true;
}

void UDocUIManagerSubsystem::Shutdown()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	{
		FApiScope Scope(*this);
		// Player removal / parent teardown: dialogs end with OwnerDestroyed, exactly once.
		TArray<int64> DialogOps;
		Dialogs.GetKeys(DialogOps);
		for (int64 Op : DialogOps)
		{
			if (FDialog* D = Dialogs.Find(Op)) { ResolveDialog(D->Handle, EDocDialogResult::OwnerDestroyed); }
		}
		TArray<FDocRequestHandle> All;
		Screens.ForEach([&All](const FDocRequestHandle& H, const FScreen&) { All.Add(H); });
		for (const FDocRequestHandle& H : All)
		{
			const FScreen* S = Screens.Find(H, this);
			Close(H, S && S->bLoaded ? EDocUIScreenState::Closed : EDocUIScreenState::Cancelled);
		}
		LoadingLeases.Reset();
		LegacyLoadingLease = FDocRequestHandle();
		DeferredOps.Reset(); // nothing runs against a removed player
	}
	PendingEvents.Reset();
}

const FDocUILayerPolicy* UDocUIManagerSubsystem::Layer(const FGameplayTag& LayerTag) const
{
	return GetDefault<UDocUISettings>()->FindLayer(LayerTag);
}

bool UDocUIManagerSubsystem::IsLive(EDocUIScreenState State) const
{
	return State == EDocUIScreenState::Requested || State == EDocUIScreenState::LoadingPresentation || State == EDocUIScreenState::Activating
		|| State == EDocUIScreenState::Active || State == EDocUIScreenState::Suspended;
}

void UDocUIManagerSubsystem::Defer(TFunction<void()> Operation)
{
	DeferredOps.Add(MoveTemp(Operation));
}

void UDocUIManagerSubsystem::Flush()
{
	if (bFlushing)
	{
		return; // the outer flush loop picks up anything queued meanwhile
	}
	bFlushing = true;
	int32 Guard = 0;
	while ((!PendingEvents.IsEmpty() || !DeferredOps.IsEmpty()) && Guard++ < 1024)
	{
		TArray<TFunction<void()>> Events = MoveTemp(PendingEvents);
		PendingEvents.Reset();
		for (TFunction<void()>& Event : Events) { Event(); }
		if (!DeferredOps.IsEmpty())
		{
			TFunction<void()> Op = MoveTemp(DeferredOps[0]);
			DeferredOps.RemoveAt(0);
			Op();
		}
	}
	bFlushing = false;
}

void UDocUIManagerSubsystem::SetState(FScreen& Screen, EDocUIScreenState NewState)
{
	const EDocUIScreenState Old = Screen.Info.State;
	if (Old == NewState)
	{
		return;
	}
	Screen.Info.State = NewState;
	FDocUIScreenStateChange Change;
	Change.Handle = Screen.Info.Handle;
	Change.ScreenId = Screen.Info.ScreenId;
	Change.OldState = Old;
	Change.NewState = NewState;
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocUIManagerSubsystem>(this), Change]()
	{
		if (UDocUIManagerSubsystem* This = WeakThis.Get())
		{
			This->OnScreenStateChangedNative.Broadcast(Change);
			This->OnScreenStateChanged.Broadcast(Change);
		}
	});
}

void UDocUIManagerSubsystem::RememberTerminal(const FDocRequestHandle& Handle, EDocUIScreenState State)
{
	TerminalStates.Add(Handle.GetOperationId(), State);
	TerminalOrder.Add(Handle.GetOperationId());
	const int32 Max = GetDefault<UDocUISettings>()->TerminalStateHistory;
	while (TerminalOrder.Num() > Max)
	{
		TerminalStates.Remove(TerminalOrder[0]);
		DialogResults.Remove(TerminalOrder[0]);
		TerminalOrder.RemoveAt(0);
	}
}

UObject* UDocUIManagerSubsystem::ResolveControlProvider() const
{
	if (UObject* Override = ControlOverride.Get())
	{
		return Override;
	}
	const UDocPlayerControlSubsystem* Control = UDocPlayerControlSubsystem::Get(GetLocalPlayer());
	return Control ? Control->GetControlProvider() : nullptr;
}

// ---------------------------------------------------------------------------
// Definitions and validation
// ---------------------------------------------------------------------------

FDocSystemResult UDocUIManagerSubsystem::RegisterScreenDefinition(UDocUIScreenDefinition* Definition)
{
	if (!Definition)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("No definition"));
	}
	TArray<FString> Errors;
	Definition->FindProblems(Errors);
	if (!Layer(Definition->LayerTag))
	{
		Errors.Add(FString::Printf(TEXT("Screen %s: layer %s has no policy"), *Definition->ScreenId.ToString(), *Definition->LayerTag.ToString()));
	}
	if (!Errors.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Join(Errors, TEXT("; ")));
	}
	if (const TObjectPtr<UDocUIScreenDefinition>* Existing = Definitions.Find(Definition->ScreenId))
	{
		return *Existing == Definition ? FDocSystemResult::MakeNoChange()
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("Duplicate ScreenId %s"), *Definition->ScreenId.ToString()), DocUITags::Error_UI_Duplicate);
	}
	Definitions.Add(Definition->ScreenId, Definition);
	DefinitionRefs.Add(Definition);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIManagerSubsystem::Validate(const FDocUIScreenRequest& Request, UDocUIScreenDefinition*& OutDef, bool bSkipDuplicate) const
{
	const TObjectPtr<UDocUIScreenDefinition>* Found = Definitions.Find(Request.ScreenId);
	OutDef = Found ? Found->Get() : nullptr;
	if (!OutDef)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, FString::Printf(TEXT("Unknown screen %s"), *Request.ScreenId.ToString()), DocUITags::Error_UI_UnknownScreen);
	}
	if (Request.ExpectedRevision >= 0 && Request.ExpectedRevision != Revision)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("UI changed (revision %lld, expected %lld)"), Revision, Request.ExpectedRevision), DocUITags::Error_UI_StaleRevision);
	}
	if (Request.PayloadVersion < 1 || Request.PayloadVersion > OutDef->PayloadVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unsupported payload version"));
	}
	if (!ContextTags.HasAll(OutDef->RequiredTags) || ContextTags.HasAny(OutDef->BlockedTags))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Context tags block this screen"), DocUITags::Error_UI_Blocked);
	}
	if (!OutDef->RequiredCapabilities.IsEmpty() && (!Presenter.IsValid() || !Presenter->GetCapabilities().HasAll(OutDef->RequiredCapabilities)))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("The presenter lacks a required capability"), DocUITags::Error_UI_Capability);
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

FDocSystemResult UDocUIManagerSubsystem::PushScreen(const FDocUIScreenRequest& Request, FDocRequestHandle& OutHandle)
{
	OutHandle = FDocRequestHandle();
	const bool bNested = ApiDepth > 0;
	FApiScope Scope(*this);
	UDocUIScreenDefinition* Def = nullptr;
	const FDocSystemResult Valid = Validate(Request, Def, false);
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	FDocRequestHandle Existing;
	Screens.ForEach([&Existing, Def, this](const FDocRequestHandle& H, const FScreen& S)
	{
		if (S.Info.ScreenId == Def->ScreenId && IsLive(S.Info.State)) { Existing = H; }
	});
	if (Existing.IsSet())
	{
		switch (Def->DuplicatePolicy)
		{
		case EDocUIDuplicatePolicy::Reject:
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("%s is already open"), *Def->ScreenId.ToString()), DocUITags::Error_UI_Duplicate);
		case EDocUIDuplicatePolicy::FocusExisting:
		{
			OutHandle = Existing;
			TArray<FDocRequestHandle>& Stack = Stacks.FindOrAdd(Def->LayerTag);
			if (Stack.Num() > 0 && Stack.Last() != Existing)
			{
				Stack.Remove(Existing);
				Stack.Add(Existing);
				++Revision;
				RefreshLayer(Def->LayerTag);
			}
			return FDocSystemResult::MakeNoChange(TEXT("Focused the existing instance"));
		}
		case EDocUIDuplicatePolicy::ReplaceExisting:
			return CreateAndLoad(Def, Request, Existing, bNested, OutHandle); // staged replacement of the existing instance
		default:
			break; // distinct instances get distinct request and instance ids
		}
	}
	const FDocUILayerPolicy* Policy = Layer(Def->LayerTag);
	int32 LiveInLayer = 0;
	for (const FDocRequestHandle& H : Stacks.FindRef(Def->LayerTag)) { if (const FScreen* S = Screens.Find(H, this)) { LiveInLayer += IsLive(S->Info.State) ? 1 : 0; } }
	if (Policy && Policy->Capacity > 0 && LiveInLayer >= Policy->Capacity)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("Layer %s is full"), *Def->LayerTag.ToString()), DocUITags::Error_UI_LayerFull);
	}
	return CreateAndLoad(Def, Request, FDocRequestHandle(), bNested, OutHandle);
}

FDocSystemResult UDocUIManagerSubsystem::CreateAndLoad(UDocUIScreenDefinition* Def, const FDocUIScreenRequest& Request, const FDocRequestHandle& Replaces, bool bDeferLoad, FDocRequestHandle& OutHandle)
{
	FScreen Screen;
	Screen.Info.InstanceId = FGuid::NewGuid();
	Screen.Info.ScreenId = Def->ScreenId;
	Screen.Info.ScreenTag = Def->ScreenTag;
	Screen.Info.LayerTag = Def->LayerTag;
	Screen.Info.PresentationKey = Def->PresentationKey;
	Screen.Info.Payload = Request.Payload;
	Screen.Info.State = EDocUIScreenState::Requested;
	Screen.Definition = Def;
	Screen.Replaces = Replaces;
	Screen.Order = ++NextOrder;
	OutHandle = Screens.Add(this, MoveTemp(Screen));
	Screens.Find(OutHandle, this)->Info.Handle = OutHandle;
	Stacks.FindOrAdd(Def->LayerTag).Add(OutHandle);
	++Revision;
	if (bDeferLoad)
	{
		// Requested from inside a running mutation (presenter or provider callback): queued.
		const FDocRequestHandle Handle = OutHandle;
		Defer([this, Handle]() { FApiScope Scope(*this); StartLoad(Handle); });
		return FDocSystemResult::MakeSuccess(OutHandle.GetOperationId());
	}
	StartLoad(OutHandle);
	const FScreen* After = Screens.Find(OutHandle, this);
	if (!After)
	{
		const EDocUIScreenState Terminal = GetScreenState(OutHandle);
		if (Terminal == EDocUIScreenState::Failed)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Presentation failed to load"), DocUITags::Error_UI_PresentationFailed);
		}
	}
	return FDocSystemResult::MakeSuccess(OutHandle.GetOperationId());
}

void UDocUIManagerSubsystem::StartLoad(const FDocRequestHandle& Handle)
{
	FScreen* S = Screens.Find(Handle, this);
	if (!S || S->Info.State != EDocUIScreenState::Requested)
	{
		return;
	}
	SetState(*S, EDocUIScreenState::LoadingPresentation);
	const int32 Token = ++S->LoadToken;
	const FDocUIScreenInfo Info = S->Info;
	if (!Presenter.IsValid())
	{
		OnPresentationLoaded(Handle, Token, true); // headless: logical state only
		return;
	}
	TSharedPtr<IDocUIPresenter> Keep = Presenter;
	Keep->LoadPresentation(Info, [WeakThis = TWeakObjectPtr<UDocUIManagerSubsystem>(this), Handle, Token](bool bSuccess)
	{
		if (UDocUIManagerSubsystem* This = WeakThis.Get())
		{
			FApiScope Scope(*This);
			This->OnPresentationLoaded(Handle, Token, bSuccess);
		}
	});
}

void UDocUIManagerSubsystem::OnPresentationLoaded(const FDocRequestHandle& Handle, int32 Token, bool bSuccess)
{
	FScreen* S = Screens.Find(Handle, this);
	if (!S || S->LoadToken != Token || S->Info.State != EDocUIScreenState::LoadingPresentation)
	{
		++StaleCallbacks; // cancelled, closed or removed player: never resurrected
		return;
	}
	if (!bSuccess)
	{
		UE_LOG(LogDocGameFrameworkUI, Warning, TEXT("Presentation for %s failed to load"), *S->Info.ScreenId.ToString());
		Close(Handle, EDocUIScreenState::Failed); // a staged replacement leaves the old screen usable
		return;
	}
	S->bLoaded = true;
	SetState(*S, EDocUIScreenState::Activating);
	const FDocRequestHandle Replaces = S->Replaces;
	const FGameplayTag LayerTag = S->Info.LayerTag;
	if (Replaces.IsSet())
	{
		S->Replaces = FDocRequestHandle();
		Close(Replaces, EDocUIScreenState::Closed);
	}
	RefreshLayer(LayerTag);
}

void UDocUIManagerSubsystem::RefreshLayer(const FGameplayTag& LayerTag)
{
	const FDocUILayerPolicy* Policy = Layer(LayerTag);
	const bool bExclusive = Policy && Policy->bExclusive;
	TArray<FDocRequestHandle> Loaded;
	for (const FDocRequestHandle& H : Stacks.FindRef(LayerTag))
	{
		const FScreen* S = Screens.Find(H, this);
		if (S && S->bLoaded && IsLive(S->Info.State)) { Loaded.Add(H); }
	}
	// Suspend first so claims are released before the new top acquires its own.
	for (int32 i = 0; i < Loaded.Num(); ++i)
	{
		const bool bTop = i == Loaded.Num() - 1;
		if (bExclusive && !bTop)
		{
			if (FScreen* S = Screens.Find(Loaded[i], this)) { Suspend(*S); }
		}
	}
	for (int32 i = 0; i < Loaded.Num(); ++i)
	{
		const bool bTop = i == Loaded.Num() - 1;
		if (!bExclusive || bTop)
		{
			if (FScreen* S = Screens.Find(Loaded[i], this)) { Activate(*S); }
		}
	}
}

void UDocUIManagerSubsystem::Activate(FScreen& Screen)
{
	if (Screen.Info.State == EDocUIScreenState::Active)
	{
		return;
	}
	SetState(Screen, EDocUIScreenState::Active);
	if (Presenter.IsValid()) { Presenter->ShowScreen(Screen.Info); }
	AcquireClaims(Screen);
	ApplyFocus(Screen);
}

void UDocUIManagerSubsystem::Suspend(FScreen& Screen)
{
	if (Screen.Info.State == EDocUIScreenState::Suspended)
	{
		return;
	}
	const bool bWasActive = Screen.Info.State == EDocUIScreenState::Active;
	if (bWasActive && Presenter.IsValid()) { Presenter->SuspendScreen(Screen.Info); }
	ReleaseClaims(Screen); // focus target is kept for restoration
	SetState(Screen, EDocUIScreenState::Suspended);
}

void UDocUIManagerSubsystem::AcquireClaims(FScreen& Screen)
{
	const UDocUIScreenDefinition* Def = Screen.Definition.Get();
	if (!Def)
	{
		return;
	}
	FGameplayTagContainer Capabilities;
	if (Def->InputMode == EDocUIInputMode::UIOnly) { Capabilities.AddTag(DocCoreTags::Control_Input); }
	if (Def->InputMode != EDocUIInputMode::Game && Def->FocusPolicy != EDocUIFocusPolicy::None) { Capabilities.AddTag(DocCoreTags::Control_Focus); }
	if (Def->CursorPolicy == EDocUICursorPolicy::Show) { Capabilities.AddTag(DocCoreTags::Control_Cursor); }
	UObject* Provider = ResolveControlProvider();
	if (!Capabilities.IsEmpty() && Provider && Provider->GetClass()->ImplementsInterface(UDocPlayerControlProvider::StaticClass()) && !Screen.ControlClaim.IsSet())
	{
		FDocControlClaimRequest Request;
		Request.LocalPlayer = GetLocalPlayer();
		Request.Owner = this;
		Request.Capabilities = Capabilities;
		Request.Priority = Def->ControlPriority;
		Request.DebugReason = FString::Printf(TEXT("UI %s"), *Def->ScreenId.ToString());
		FDocRequestHandle Claim;
		if (IDocPlayerControlProvider::Execute_AcquireDocControl(Provider, Request, Claim).IsSuccess())
		{
			Screen.ControlClaim = Claim;
		}
	}
	Screen.Info.bHasControlClaim = Screen.ControlClaim.IsSet();
	if (Def->PausePolicy == EDocUIPausePolicy::GlobalPause && !Screen.PauseLease.IsSet())
	{
		if (UDocUIPauseSubsystem* Pause = UDocUIPauseSubsystem::Get(GetLocalPlayer()))
		{
			const FDocSystemResult Paused = Pause->AcquirePause(this, Def->ScreenId.ToString(), Screen.PauseLease);
			if (!Paused.IsSuccess())
			{
				UE_LOG(LogDocGameFrameworkUI, Verbose, TEXT("%s opened without world pause: %s"), *Def->ScreenId.ToString(), *Paused.Diagnostic);
			}
		}
	}
}

void UDocUIManagerSubsystem::ReleaseClaims(FScreen& Screen)
{
	if (Screen.ControlClaim.IsSet())
	{
		if (UObject* Provider = ResolveControlProvider())
		{
			if (Provider->GetClass()->ImplementsInterface(UDocPlayerControlProvider::StaticClass()))
			{
				IDocPlayerControlProvider::Execute_ReleaseDocControl(Provider, Screen.ControlClaim); // only this screen's claim
			}
		}
		Screen.ControlClaim = FDocRequestHandle();
	}
	Screen.Info.bHasControlClaim = false;
	if (Screen.PauseLease.IsSet())
	{
		if (UDocUIPauseSubsystem* Pause = UDocUIPauseSubsystem::Get(GetLocalPlayer()))
		{
			Pause->ReleasePause(Screen.PauseLease);
		}
		Screen.PauseLease = FDocRequestHandle();
	}
}

void UDocUIManagerSubsystem::ApplyFocus(FScreen& Screen)
{
	const UDocUIScreenDefinition* Def = Screen.Definition.Get();
	if (!Def || Def->FocusPolicy == EDocUIFocusPolicy::None)
	{
		return;
	}
	auto Focusable = [this, &Screen](FName Target) { return !Target.IsNone() && (!Presenter.IsValid() || Presenter->IsFocusable(Screen.Info, Target)); };
	FName Target = NAME_None;
	if (Def->FocusPolicy == EDocUIFocusPolicy::RestoreLast && Focusable(Screen.Info.FocusTarget))
	{
		Target = Screen.Info.FocusTarget;
	}
	else if (Focusable(Def->DefaultFocusTarget))
	{
		Target = Def->DefaultFocusTarget; // explicit fallback inside the surviving screen
	}
	Screen.Info.FocusTarget = Target;
	// An outranked claimant (another modal, a sequence) never steals navigation focus.
	UObject* Provider = ResolveControlProvider();
	if (Screen.ControlClaim.IsSet() && Provider && Provider->GetClass()->ImplementsInterface(UDocPlayerControlProvider::StaticClass())
		&& !IDocPlayerControlProvider::Execute_IsDocEffectiveControlOwner(Provider, Screen.ControlClaim, DocCoreTags::Control_Focus))
	{
		return;
	}
	if (Presenter.IsValid() && !Target.IsNone())
	{
		Presenter->ApplyFocus(Screen.Info, Target);
	}
}

void UDocUIManagerSubsystem::Close(const FDocRequestHandle& Handle, EDocUIScreenState Terminal)
{
	FScreen* S = Screens.Find(Handle, this);
	if (!S)
	{
		return;
	}
	const EDocUIScreenState Old = S->Info.State;
	if (Old == EDocUIScreenState::LoadingPresentation && Presenter.IsValid())
	{
		Presenter->CancelLoad(S->Info);
	}
	++S->LoadToken; // any in-flight load callback is now stale
	if (Old == EDocUIScreenState::Active || Old == EDocUIScreenState::Suspended || Old == EDocUIScreenState::Activating)
	{
		SetState(*S, EDocUIScreenState::Deactivating);
		if (Presenter.IsValid()) { Presenter->HideScreen(S->Info); }
	}
	ReleaseClaims(*S);
	SetState(*S, Terminal);
	const FGameplayTag LayerTag = S->Info.LayerTag;
	const int64 Op = Handle.GetOperationId();
	if (FDialog* D = Dialogs.Find(Op))
	{
		if (D->Result == EDocDialogResult::None)
		{
			// Closed by teardown/clear/travel rather than input: one terminal result, never affirmative.
			D->Result = Terminal == EDocUIScreenState::Failed ? EDocDialogResult::Failed
				: (D->bOwnerWasSet && !D->Owner.IsValid() ? EDocDialogResult::OwnerDestroyed : EDocDialogResult::Cancelled);
		}
		DialogResults.Add(Op, D->Result);
		if (D->Callback)
		{
			FDocDialogCallback Callback = MoveTemp(D->Callback);
			D->Callback = nullptr;
			const EDocDialogResult Result = D->Result;
			PendingEvents.Add([Callback = MoveTemp(Callback), Result]() { Callback(Result); });
		}
		Dialogs.Remove(Op);
	}
	Stacks.FindOrAdd(LayerTag).Remove(Handle);
	Screens.Remove(Handle, this);
	RememberTerminal(Handle, Terminal);
	++Revision;
	RefreshLayer(LayerTag);
	FocusTopmost();
}

void UDocUIManagerSubsystem::FocusTopmost()
{
	const UDocUISettings* Settings = GetDefault<UDocUISettings>();
	FDocRequestHandle Best;
	int32 BestPriority = MIN_int32;
	for (const TPair<FGameplayTag, TArray<FDocRequestHandle>>& Pair : Stacks)
	{
		const FDocUILayerPolicy* Policy = Settings->FindLayer(Pair.Key);
		const int32 Priority = Policy ? Policy->Priority : 0;
		for (int32 i = Pair.Value.Num() - 1; i >= 0; --i)
		{
			const FScreen* S = Screens.Find(Pair.Value[i], this);
			const UDocUIScreenDefinition* Def = S ? S->Definition.Get() : nullptr;
			if (S && Def && S->Info.State == EDocUIScreenState::Active && Def->InputMode != EDocUIInputMode::Game && Def->FocusPolicy != EDocUIFocusPolicy::None)
			{
				if (Priority > BestPriority) { Best = Pair.Value[i]; BestPriority = Priority; }
				break;
			}
		}
	}
	if (FScreen* S = Screens.Find(Best, this))
	{
		ApplyFocus(*S); // validated against the presenter; outranked claimants are skipped inside
	}
}

FDocSystemResult UDocUIManagerSubsystem::PopScreen(const FDocRequestHandle& Handle)
{
	if (ApiDepth > 0)
	{
		Defer([this, Handle]() { PopScreen(Handle); });
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Queued behind the current stack mutation"));
	}
	FApiScope Scope(*this);
	const FScreen* S = Screens.Find(Handle, this);
	if (!S)
	{
		return TerminalStates.Contains(Handle.GetOperationId()) ? FDocSystemResult::MakeNoChange(TEXT("Already closed"))
			: FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown or stale screen handle"), DocUITags::Error_UI_StaleHandle);
	}
	Close(Handle, S->bLoaded ? EDocUIScreenState::Closed : EDocUIScreenState::Cancelled);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIManagerSubsystem::PopTopScreen(FGameplayTag LayerTag)
{
	const TArray<FDocRequestHandle> Stack = Stacks.FindRef(LayerTag);
	return Stack.IsEmpty() ? FDocSystemResult::MakeNoChange(TEXT("Layer is empty")) : PopScreen(Stack.Last());
}

FDocSystemResult UDocUIManagerSubsystem::ReplaceScreen(const FDocRequestHandle& Old, const FDocUIScreenRequest& Request, FDocRequestHandle& OutHandle, bool bDestructive)
{
	OutHandle = FDocRequestHandle();
	const bool bNested = ApiDepth > 0;
	FApiScope Scope(*this);
	const FScreen* OldScreen = Screens.Find(Old, this);
	if (!OldScreen || !IsLive(OldScreen->Info.State))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("The screen to replace is not open"), DocUITags::Error_UI_StaleHandle);
	}
	UDocUIScreenDefinition* Def = nullptr;
	const FDocSystemResult Valid = Validate(Request, Def, true);
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	if (bDestructive)
	{
		if (bNested)
		{
			Defer([this, Old]() { PopScreen(Old); });
		}
		else
		{
			Close(Old, OldScreen->bLoaded ? EDocUIScreenState::Closed : EDocUIScreenState::Cancelled);
		}
		return CreateAndLoad(Def, Request, FDocRequestHandle(), bNested, OutHandle);
	}
	return CreateAndLoad(Def, Request, Old, bNested, OutHandle); // staged: Old closes only after the new one loads
}

FDocSystemResult UDocUIManagerSubsystem::ClearLayer(FGameplayTag LayerTag)
{
	if (ApiDepth > 0)
	{
		Defer([this, LayerTag]() { ClearLayer(LayerTag); });
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Queued behind the current stack mutation"));
	}
	FApiScope Scope(*this);
	const TArray<FDocRequestHandle> Stack = Stacks.FindRef(LayerTag);
	if (Stack.IsEmpty())
	{
		return FDocSystemResult::MakeNoChange(TEXT("Layer is empty"));
	}
	for (int32 i = Stack.Num() - 1; i >= 0; --i)
	{
		const FScreen* S = Screens.Find(Stack[i], this);
		if (S) { Close(Stack[i], S->bLoaded ? EDocUIScreenState::Closed : EDocUIScreenState::Cancelled); }
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIManagerSubsystem::CancelScreenRequest(const FDocRequestHandle& Handle)
{
	FApiScope Scope(*this);
	const FScreen* S = Screens.Find(Handle, this);
	if (!S)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Unknown or finished request"));
	}
	if (S->Info.State != EDocUIScreenState::Requested && S->Info.State != EDocUIScreenState::LoadingPresentation)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Already presented; use PopScreen"));
	}
	Close(Handle, EDocUIScreenState::Cancelled);
	return FDocSystemResult::MakeSuccess();
}

FDocRequestHandle UDocUIManagerSubsystem::GetActiveScreen(FGameplayTag LayerTag) const
{
	const TArray<FDocRequestHandle> Stack = Stacks.FindRef(LayerTag);
	for (int32 i = Stack.Num() - 1; i >= 0; --i)
	{
		const FScreen* S = Screens.Find(Stack[i], this);
		if (S && S->Info.State == EDocUIScreenState::Active) { return Stack[i]; }
	}
	return FDocRequestHandle();
}

bool UDocUIManagerSubsystem::IsScreenActive(FName ScreenId) const
{
	bool bActive = false;
	Screens.ForEach([&bActive, ScreenId](const FDocRequestHandle&, const FScreen& S)
	{
		bActive |= S.Info.ScreenId == ScreenId && S.Info.State == EDocUIScreenState::Active;
	});
	return bActive;
}

bool UDocUIManagerSubsystem::GetScreenInfo(const FDocRequestHandle& Handle, FDocUIScreenInfo& OutInfo) const
{
	if (const FScreen* S = Screens.Find(Handle, this))
	{
		OutInfo = S->Info;
		return true;
	}
	return false;
}

EDocUIScreenState UDocUIManagerSubsystem::GetScreenState(const FDocRequestHandle& Handle) const
{
	if (const FScreen* S = Screens.Find(Handle, this))
	{
		return S->Info.State;
	}
	const EDocUIScreenState* Terminal = TerminalStates.Find(Handle.GetOperationId());
	return Terminal ? *Terminal : EDocUIScreenState::Closed;
}

TArray<FDocUIScreenInfo> UDocUIManagerSubsystem::GetLayerScreens(const FGameplayTag& LayerTag) const
{
	TArray<FDocUIScreenInfo> Out;
	for (const FDocRequestHandle& H : Stacks.FindRef(LayerTag))
	{
		if (const FScreen* S = Screens.Find(H, this)) { Out.Add(S->Info); }
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Focus and back
// ---------------------------------------------------------------------------

FDocSystemResult UDocUIManagerSubsystem::SetFocusTarget(const FDocRequestHandle& Handle, FName Target)
{
	FScreen* S = Screens.Find(Handle, this);
	if (!S)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown screen"), DocUITags::Error_UI_StaleHandle);
	}
	if (Target.IsNone() || (Presenter.IsValid() && !Presenter->IsFocusable(S->Info, Target)))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Target is not focusable"));
	}
	S->Info.FocusTarget = Target;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIManagerSubsystem::HandleBack()
{
	const UDocUISettings* Settings = GetDefault<UDocUISettings>();
	FDocRequestHandle Best;
	int32 BestPriority = MIN_int32;
	for (const TPair<FGameplayTag, TArray<FDocRequestHandle>>& Pair : Stacks)
	{
		const FDocUILayerPolicy* Policy = Settings->FindLayer(Pair.Key);
		const int32 Priority = Policy ? Policy->Priority : 0;
		for (int32 i = Pair.Value.Num() - 1; i >= 0; --i)
		{
			const FScreen* S = Screens.Find(Pair.Value[i], this);
			const UDocUIScreenDefinition* Def = S ? S->Definition.Get() : nullptr;
			if (S && Def && S->Info.State == EDocUIScreenState::Active && Def->InputMode != EDocUIInputMode::Game)
			{
				if (Priority > BestPriority) { Best = Pair.Value[i]; BestPriority = Priority; }
				break;
			}
		}
	}
	if (!Best.IsSet())
	{
		return FDocSystemResult::MakeNoChange(TEXT("Nothing handles back"));
	}
	if (Dialogs.Contains(Best.GetOperationId()))
	{
		return SubmitDialogInput(Best, EDocDialogInput::Back);
	}
	return PopScreen(Best);
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

FDocSystemResult UDocUIManagerSubsystem::ShowDialog(const FDocDialogRequest& Request, UObject* Owner, FDocDialogCallback Callback, FDocRequestHandle& OutHandle)
{
	OutHandle = FDocRequestHandle();
	const bool bNested = ApiDepth > 0;
	FApiScope Scope(*this);
	if (!DialogDefinition)
	{
		DialogDefinition = NewObject<UDocUIScreenDefinition>(this, TEXT("DocDialogDefinition"));
		DialogDefinition->ScreenId = TEXT("Doc.Dialog");
		DialogDefinition->ScreenTag = DocUITags::Screen_Dialog;
		DialogDefinition->LayerTag = DocUITags::Layer_Modal;
		DialogDefinition->InputMode = EDocUIInputMode::UIOnly;
		DialogDefinition->CursorPolicy = EDocUICursorPolicy::Show;
		DialogDefinition->DuplicatePolicy = EDocUIDuplicatePolicy::AllowDistinctInstances;
		DialogDefinition->FocusPolicy = EDocUIFocusPolicy::RestoreLast;
		DialogDefinition->PresentationKey = TEXT("Doc.Dialog");
		DialogDefinition->ControlPriority = GetDefault<UDocUISettings>()->ModalControlPriority;
	}
	const FDocUILayerPolicy* Policy = Layer(DocUITags::Layer_Modal);
	if (Policy && Policy->Capacity > 0 && Stacks.FindRef(DocUITags::Layer_Modal).Num() >= Policy->Capacity)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Modal layer is full"), DocUITags::Error_UI_LayerFull);
	}
	FDocUIScreenRequest ScreenRequest;
	ScreenRequest.ScreenId = DialogDefinition->ScreenId;
	ScreenRequest.Payload.Add(TEXT("Title"), Request.Title.ToString());
	ScreenRequest.Payload.Add(TEXT("Body"), Request.Body.ToString());
	ScreenRequest.Payload.Add(TEXT("Confirm"), Request.ConfirmText.ToString());
	ScreenRequest.Payload.Add(TEXT("Cancel"), Request.CancelText.ToString());
	if (!Request.ThirdActionText.IsEmpty()) { ScreenRequest.Payload.Add(TEXT("Third"), Request.ThirdActionText.ToString()); }
	ScreenRequest.Payload.Add(TEXT("Destructive"), Request.bDestructive ? TEXT("true") : TEXT("false"));

	// Register the dialog before the screen can load, so an immediate close resolves it.
	FDialog Dialog;
	Dialog.Request = Request;
	Dialog.Owner = Owner;
	Dialog.bOwnerWasSet = Owner != nullptr;
	Dialog.Callback = MoveTemp(Callback);
	Dialog.Remaining = Request.TimeoutSeconds;

	FScreen Screen;
	Screen.Info.InstanceId = FGuid::NewGuid();
	Screen.Info.ScreenId = DialogDefinition->ScreenId;
	Screen.Info.ScreenTag = DialogDefinition->ScreenTag;
	Screen.Info.LayerTag = DialogDefinition->LayerTag;
	Screen.Info.PresentationKey = DialogDefinition->PresentationKey;
	Screen.Info.Payload = ScreenRequest.Payload;
	Screen.Info.FocusTarget = Request.bDestructive ? FName(TEXT("Cancel")) : FName(TEXT("Confirm")); // no affirmative default for destructive prompts
	Screen.Definition = DialogDefinition;
	Screen.Order = ++NextOrder;
	OutHandle = Screens.Add(this, MoveTemp(Screen));
	Screens.Find(OutHandle, this)->Info.Handle = OutHandle;
	Dialog.Handle = OutHandle;
	Dialogs.Add(OutHandle.GetOperationId(), MoveTemp(Dialog));
	Stacks.FindOrAdd(DocUITags::Layer_Modal).Add(OutHandle);
	++Revision;
	if (bNested)
	{
		const FDocRequestHandle Handle = OutHandle;
		Defer([this, Handle]() { FApiScope Inner(*this); StartLoad(Handle); });
	}
	else
	{
		StartLoad(OutHandle);
	}
	return FDocSystemResult::MakeSuccess(OutHandle.GetOperationId());
}

void UDocUIManagerSubsystem::ResolveDialog(const FDocRequestHandle& Handle, EDocDialogResult Result)
{
	FDialog* D = Dialogs.Find(Handle.GetOperationId());
	if (!D || D->Result != EDocDialogResult::None)
	{
		return;
	}
	D->Result = Result;
	const FScreen* S = Screens.Find(Handle, this);
	Close(Handle, S && S->bLoaded ? EDocUIScreenState::Closed : EDocUIScreenState::Cancelled); // queues the callback once
}

FDocSystemResult UDocUIManagerSubsystem::SubmitDialogInput(const FDocRequestHandle& DialogHandle, EDocDialogInput Input)
{
	const int64 Op = DialogHandle.GetOperationId();
	const FDialog* D = Dialogs.Find(Op);
	if (!D || !Screens.Find(DialogHandle, this))
	{
		return DialogResults.Contains(Op) ? FDocSystemResult::MakeNoChange(TEXT("Dialog already resolved"))
			: FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown dialog"), DocUITags::Error_UI_Dialog);
	}
	if (D->Result != EDocDialogResult::None)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Dialog already resolved"));
	}
	EDocDialogResult Result = EDocDialogResult::Cancelled;
	switch (Input)
	{
	case EDocDialogInput::Confirm:
		if (Screens.Find(DialogHandle, this)->Info.State != EDocUIScreenState::Active)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("The prompt is not visible yet"), DocUITags::Error_UI_Dialog);
		}
		Result = EDocDialogResult::Confirmed;
		break;
	case EDocDialogInput::ThirdAction:
		if (D->Request.ThirdActionText.IsEmpty())
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("This dialog has no third action"), DocUITags::Error_UI_Dialog);
		}
		Result = EDocDialogResult::ThirdAction;
		break;
	default:
		Result = EDocDialogResult::Cancelled;
		break;
	}
	if (ApiDepth > 0)
	{
		// Record the choice now so a second input in the same frame cannot also resolve it.
		Dialogs[Op].Result = Result;
		Defer([this, DialogHandle]()
		{
			FApiScope Scope(*this);
			const FScreen* S = Screens.Find(DialogHandle, this);
			Close(DialogHandle, S && S->bLoaded ? EDocUIScreenState::Closed : EDocUIScreenState::Cancelled);
		});
		return FDocSystemResult::MakeSuccess();
	}
	FApiScope Scope(*this);
	ResolveDialog(DialogHandle, Result);
	return FDocSystemResult::MakeSuccess();
}

EDocDialogResult UDocUIManagerSubsystem::GetDialogResult(const FDocRequestHandle& DialogHandle) const
{
	if (const FDialog* D = Dialogs.Find(DialogHandle.GetOperationId()))
	{
		return D->Result;
	}
	const EDocDialogResult* Result = DialogResults.Find(DialogHandle.GetOperationId());
	return Result ? *Result : EDocDialogResult::None;
}

// ---------------------------------------------------------------------------
// Loading leases
// ---------------------------------------------------------------------------

void UDocUIManagerSubsystem::BroadcastLoading()
{
	PendingEvents.Add([WeakThis = TWeakObjectPtr<UDocUIManagerSubsystem>(this)]()
	{
		if (UDocUIManagerSubsystem* This = WeakThis.Get())
		{
			This->OnLoadingChanged.Broadcast(This->GetLoadingView());
		}
	});
}

FDocSystemResult UDocUIManagerSubsystem::AcquireLoading(UObject* Owner, FText Status, FDocRequestHandle& OutLease)
{
	FApiScope Scope(*this);
	FLoadingLease Lease;
	Lease.Owner = Owner;
	Lease.bOwnerWasSet = Owner != nullptr;
	Lease.Status = Status;
	Lease.Order = ++LoadingOrder;
	OutLease = LoadingLeases.Add(this, MoveTemp(Lease));
	BroadcastLoading();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIManagerSubsystem::SetLoadingLeaseProgress(const FDocRequestHandle& Lease, float Progress)
{
	FApiScope Scope(*this);
	FLoadingLease* L = LoadingLeases.Find(Lease, this);
	if (!L)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown loading lease"), DocUITags::Error_UI_Loading);
	}
	if (!FMath::IsFinite(Progress) || Progress < 0.f || Progress > 1.f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Progress must be in [0,1]"), DocUITags::Error_UI_Loading);
	}
	L->Progress = Progress;
	BroadcastLoading();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIManagerSubsystem::SetLoadingLeaseIndeterminate(const FDocRequestHandle& Lease)
{
	FApiScope Scope(*this);
	FLoadingLease* L = LoadingLeases.Find(Lease, this);
	if (!L)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown loading lease"), DocUITags::Error_UI_Loading);
	}
	L->Progress.Reset();
	BroadcastLoading();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIManagerSubsystem::SetLoadingLeaseStatus(const FDocRequestHandle& Lease, FText Status)
{
	FApiScope Scope(*this);
	FLoadingLease* L = LoadingLeases.Find(Lease, this);
	if (!L)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown loading lease"), DocUITags::Error_UI_Loading);
	}
	L->Status = Status;
	L->Order = ++LoadingOrder;
	BroadcastLoading();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocUIManagerSubsystem::ReleaseLoading(const FDocRequestHandle& Lease, EDocLoadingOutcome Outcome, FText ErrorText)
{
	FApiScope Scope(*this);
	if (!LoadingLeases.Remove(Lease, this))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Unknown or released loading lease"));
	}
	if (Outcome != EDocLoadingOutcome::Completed)
	{
		LoadingError.bHasError = true;
		LoadingError.ErrorOutcome = Outcome;
		LoadingError.ErrorText = !ErrorText.IsEmpty() ? ErrorText
			: (Outcome == EDocLoadingOutcome::Cancelled ? NSLOCTEXT("DocGameFrameworkUI", "LoadingCancelled", "Loading was cancelled.")
				: NSLOCTEXT("DocGameFrameworkUI", "LoadingFailed", "Loading failed."));
	}
	BroadcastLoading();
	return FDocSystemResult::MakeSuccess();
}

void UDocUIManagerSubsystem::DismissLoadingError()
{
	FApiScope Scope(*this);
	if (LoadingError.bHasError)
	{
		LoadingError = FDocLoadingView();
		BroadcastLoading();
	}
}

FDocLoadingView UDocUIManagerSubsystem::GetLoadingView() const
{
	FDocLoadingView View;
	View.bHasError = LoadingError.bHasError;
	View.ErrorOutcome = LoadingError.ErrorOutcome;
	View.ErrorText = LoadingError.ErrorText;
	int64 LatestStatus = -1;
	bool bAllDeterminate = true;
	float MinProgress = 1.f;
	LoadingLeases.ForEach([&](const FDocRequestHandle&, const FLoadingLease& L)
	{
		++View.ActiveLeases;
		if (L.Progress.IsSet()) { MinProgress = FMath::Min(MinProgress, L.Progress.GetValue()); }
		else { bAllDeterminate = false; }
		if (!L.Status.IsEmpty() && L.Order > LatestStatus) { LatestStatus = L.Order; View.Status = L.Status; }
	});
	View.bVisible = View.ActiveLeases > 0 || View.bHasError;
	View.bIndeterminate = View.ActiveLeases == 0 || !bAllDeterminate;
	View.Progress = View.bIndeterminate ? 0.f : MinProgress;
	return View;
}

void UDocUIManagerSubsystem::ShowLoading(FText Status)
{
	if (LoadingLeases.Find(LegacyLoadingLease, this))
	{
		SetLoadingLeaseStatus(LegacyLoadingLease, Status);
		return;
	}
	AcquireLoading(this, Status, LegacyLoadingLease);
}

void UDocUIManagerSubsystem::HideLoading()
{
	ReleaseLoading(LegacyLoadingLease, EDocLoadingOutcome::Completed, FText::GetEmpty()); // only the legacy lease
	LegacyLoadingLease = FDocRequestHandle();
}

void UDocUIManagerSubsystem::SetLoadingProgress(float Progress)
{
	SetLoadingLeaseProgress(LegacyLoadingLease, Progress);
}

void UDocUIManagerSubsystem::SetLoadingStatus(FText Status)
{
	SetLoadingLeaseStatus(LegacyLoadingLease, Status);
}

// ---------------------------------------------------------------------------
// Travel, clock, diagnostics
// ---------------------------------------------------------------------------

TArray<FDocUIRestorationDescriptor> UDocUIManagerSubsystem::CaptureRestorationDescriptors() const
{
	TArray<TPair<int64, FDocUIRestorationDescriptor>> Ordered;
	Screens.ForEach([&Ordered, this](const FDocRequestHandle& H, const FScreen& S)
	{
		const UDocUIScreenDefinition* Def = S.Definition.Get();
		if (Def && Def != DialogDefinition && Def->bRestorableAfterTravel && Def->LayerTag != DocUITags::Layer_Modal && S.bLoaded && IsLive(S.Info.State))
		{
			FDocUIRestorationDescriptor D;
			D.ScreenId = Def->ScreenId;
			D.PayloadVersion = Def->PayloadVersion;
			D.Payload = S.Info.Payload;
			Ordered.Add(TPair<int64, FDocUIRestorationDescriptor>(S.Order, D));
		}
	});
	Ordered.Sort([](const TPair<int64, FDocUIRestorationDescriptor>& A, const TPair<int64, FDocUIRestorationDescriptor>& B) { return A.Key < B.Key; });
	TArray<FDocUIRestorationDescriptor> Out;
	for (const TPair<int64, FDocUIRestorationDescriptor>& P : Ordered) { Out.Add(P.Value); }
	return Out;
}

void UDocUIManagerSubsystem::NotifyTravel()
{
	FApiScope Scope(*this);
	TArray<int64> DialogOps;
	Dialogs.GetKeys(DialogOps);
	for (int64 Op : DialogOps)
	{
		if (FDialog* D = Dialogs.Find(Op)) { ResolveDialog(D->Handle, EDocDialogResult::OwnerDestroyed); }
	}
	TArray<FDocRequestHandle> All;
	Screens.ForEach([&All](const FDocRequestHandle& H, const FScreen&) { All.Add(H); });
	for (const FDocRequestHandle& H : All)
	{
		const FScreen* S = Screens.Find(H, this);
		if (S) { Close(H, S->bLoaded ? EDocUIScreenState::Closed : EDocUIScreenState::Cancelled); }
	}
	// Old-world owners are gone: their leases end; other owners' leases stay visible.
	if (LoadingLeases.RemoveIf([](const FDocRequestHandle&, const FLoadingLease& L) { return L.bOwnerWasSet && !L.Owner.IsValid(); }) > 0)
	{
		BroadcastLoading();
	}
}

int32 UDocUIManagerSubsystem::RestoreScreens(const TArray<FDocUIRestorationDescriptor>& Descriptors)
{
	int32 Restored = 0;
	for (const FDocUIRestorationDescriptor& D : Descriptors)
	{
		const TObjectPtr<UDocUIScreenDefinition>* Def = Definitions.Find(D.ScreenId);
		if (!Def || !(*Def)->bRestorableAfterTravel || (*Def)->LayerTag == DocUITags::Layer_Modal)
		{
			continue;
		}
		FDocUIScreenRequest Request;
		Request.ScreenId = D.ScreenId;
		Request.Payload = D.Payload;
		Request.PayloadVersion = D.PayloadVersion;
		FDocRequestHandle Handle;
		Restored += PushScreen(Request, Handle).IsSuccess() ? 1 : 0;
	}
	return Restored;
}

void UDocUIManagerSubsystem::AdvanceClock(double Seconds)
{
	if (!(Seconds > 0.0))
	{
		return;
	}
	FApiScope Scope(*this);
	TArray<TPair<FDocRequestHandle, EDocDialogResult>> Resolve;
	for (TPair<int64, FDialog>& Pair : Dialogs)
	{
		FDialog& D = Pair.Value;
		if (D.Result != EDocDialogResult::None)
		{
			continue;
		}
		if (D.bOwnerWasSet && !D.Owner.IsValid())
		{
			Resolve.Add(TPair<FDocRequestHandle, EDocDialogResult>(D.Handle, EDocDialogResult::OwnerDestroyed));
			continue;
		}
		if (D.Request.TimeoutSeconds > 0.f)
		{
			D.Remaining -= Seconds;
			if (D.Remaining <= 0.0)
			{
				EDocDialogResult R = D.Request.TimeoutResult;
				if (D.Request.bDestructive || R == EDocDialogResult::None || R == EDocDialogResult::OwnerDestroyed || R == EDocDialogResult::Failed
					|| (R == EDocDialogResult::ThirdAction && D.Request.ThirdActionText.IsEmpty()))
				{
					R = EDocDialogResult::Cancelled;
				}
				Resolve.Add(TPair<FDocRequestHandle, EDocDialogResult>(D.Handle, R));
			}
		}
	}
	for (const TPair<FDocRequestHandle, EDocDialogResult>& R : Resolve)
	{
		ResolveDialog(R.Key, R.Value);
	}
	if (LoadingLeases.RemoveIf([](const FDocRequestHandle&, const FLoadingLease& L) { return L.bOwnerWasSet && !L.Owner.IsValid(); }) > 0)
	{
		BroadcastLoading();
	}
	if (UDocUIPauseSubsystem* Pause = UDocUIPauseSubsystem::Get(GetLocalPlayer()))
	{
		Pause->PruneDeadOwners();
	}
}

FDocRequestHandle UDocUIManagerSubsystem::GetControlClaimForTesting(const FDocRequestHandle& Screen) const
{
	const FScreen* S = Screens.Find(Screen, this);
	return S ? S->ControlClaim : FDocRequestHandle();
}

TArray<FString> UDocUIManagerSubsystem::DescribeState() const
{
	TArray<FString> Lines;
	for (const TPair<FGameplayTag, TArray<FDocRequestHandle>>& Pair : Stacks)
	{
		for (const FDocRequestHandle& H : Pair.Value)
		{
			if (const FScreen* S = Screens.Find(H, this))
			{
				Lines.Add(FString::Printf(TEXT("%s %s state=%s focus=%s claim=%d pause=%d"), *Pair.Key.ToString(), *S->Info.ScreenId.ToString(),
					*UEnum::GetValueAsString(S->Info.State), *S->Info.FocusTarget.ToString(), S->ControlClaim.IsSet() ? 1 : 0, S->PauseLease.IsSet() ? 1 : 0));
			}
		}
	}
	const FDocLoadingView Loading = GetLoadingView();
	Lines.Add(FString::Printf(TEXT("loading leases=%d indeterminate=%d error=%d"), Loading.ActiveLeases, Loading.bIndeterminate ? 1 : 0, Loading.bHasError ? 1 : 0));
	Lines.Add(FString::Printf(TEXT("dialogs=%d revision=%lld staleCallbacks=%d deferred=%d"), Dialogs.Num(), Revision, StaleCallbacks, DeferredOps.Num()));
	return Lines;
}
