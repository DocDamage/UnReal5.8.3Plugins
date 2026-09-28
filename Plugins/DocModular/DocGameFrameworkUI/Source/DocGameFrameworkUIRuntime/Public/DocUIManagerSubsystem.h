#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "DocRequestHandle.h"
#include "DocUIProviders.h"
#include "DocUIManagerSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocUIScreenStateEvent, const FDocUIScreenStateChange&, Change);
DECLARE_MULTICAST_DELEGATE_OneParam(FDocUIScreenStateNative, const FDocUIScreenStateChange&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocUILoadingEvent, const FDocLoadingView&, View);

using FDocDialogCallback = TFunction<void(EDocDialogResult)>;

/**
 * Shared world-level pause leases (handoff 12.3). GlobalPause screens from any local
 * player acquire a lease; the world is paused while at least one lease is held, so two
 * split-screen players never toggle one global flag independently. Refused in networked
 * worlds (MultiplayerNonPause) unless the host explicitly allows listen-server pause.
 */
UCLASS()
class DOCGAMEFRAMEWORKUIRUNTIME_API UDocUIPauseSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDocUIPauseSubsystem* Get(const UObject* WorldContextObject);

	FDocSystemResult AcquirePause(UObject* Owner, const FString& Reason, FDocRequestHandle& OutLease);
	/** Idempotent. Unpauses only when the last lease goes away. */
	FDocSystemResult ReleasePause(const FDocRequestHandle& Lease);
	bool IsPaused() const { return bApplied; }
	int32 GetLeaseCount() const { return Leases.Num(); }
	/** Drops leases whose owner was destroyed. */
	void PruneDeadOwners();

	static void SetSubsystemOverrideForTesting(UDocUIPauseSubsystem* Override);
	/** Replaces the world pause call (returns whether the pause state was applied). */
	void SetPauseApplierForTesting(TFunction<bool(bool)> InApplier) { ApplierOverride = MoveTemp(InApplier); }
	void SetPolicyOverrideForTesting(TOptional<bool> bAllowed) { PolicyOverride = bAllowed; }

	virtual void Deinitialize() override;

private:
	struct FLease
	{
		TWeakObjectPtr<UObject> Owner;
		FString Reason;
	};

	bool IsPauseAllowed() const;
	void Recompute();

	TDocHandleTable<FLease> Leases;
	bool bApplied = false;
	TFunction<bool(bool)> ApplierOverride;
	TOptional<bool> PolicyOverride;
};

/**
 * Per-local-player screen service (UIManagerSubsystem, handoff 12.2-12.3, 12.8-12.9).
 *
 * Each local player owns independent layer stacks, focus history, dialogs and loading
 * leases. Screens move Requested -> LoadingPresentation -> Activating -> Active <-> Suspended
 * -> Deactivating -> Closed (or Failed/Cancelled). Input, cursor and focus go through the
 * shared IDocPlayerControlProvider as owner-scoped claims, and global pause through
 * UDocUIPauseSubsystem leases, so closing one modal never restores stale input over
 * another modal or a sequence.
 *
 * Stack mutations are serialized: requests made from presenter callbacks or state-change
 * handlers while a mutation is running are queued and run after it completes.
 */
UCLASS()
class DOCGAMEFRAMEWORKUIRUNTIME_API UDocUIManagerSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UDocUIManagerSubsystem* Get(const ULocalPlayer* LocalPlayer);

	// ---- Definitions, presenter, context ----
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult RegisterScreenDefinition(UDocUIScreenDefinition* Definition);
	void SetPresenter(TSharedPtr<IDocUIPresenter> InPresenter) { Presenter = InPresenter; }
	/** Host-owned context tags checked against RequiredTags/BlockedTags. */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	void SetContextTags(const FGameplayTagContainer& Tags) { ContextTags = Tags; }
	/** Use this object as the control provider instead of the player's UDocPlayerControlSubsystem. */
	void SetControlProviderOverride(UObject* ProviderObject) { ControlOverride = ProviderObject; }

	// ---- Screens ----
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult PushScreen(const FDocUIScreenRequest& Request, FDocRequestHandle& OutHandle);
	/** Closes an active screen or cancels a pending one. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult PopScreen(const FDocRequestHandle& Handle);
	/** Pops the top screen of a layer. */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult PopTopScreen(FGameplayTag LayerTag);
	/**
	 * Stages the replacement and closes Old only after it loaded. A failed load leaves Old
	 * usable. bDestructive closes Old first (explicitly authored destructive transitions).
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult ReplaceScreen(const FDocRequestHandle& Old, const FDocUIScreenRequest& Request, FDocRequestHandle& OutHandle, bool bDestructive = false);
	/** Closes this player's screens on one layer only. */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult ClearLayer(FGameplayTag LayerTag);
	/** Cancels a request whose presentation has not finished loading. */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult CancelScreenRequest(const FDocRequestHandle& Handle);
	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	FDocRequestHandle GetActiveScreen(FGameplayTag LayerTag) const;
	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	bool IsScreenActive(FName ScreenId) const;
	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	bool GetScreenInfo(const FDocRequestHandle& Handle, FDocUIScreenInfo& OutInfo) const;
	/** Includes terminal states (bounded history). */
	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	EDocUIScreenState GetScreenState(const FDocRequestHandle& Handle) const;
	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	int64 GetRevision() const { return Revision; }
	/** Screens of a layer, bottom to top. */
	TArray<FDocUIScreenInfo> GetLayerScreens(const FGameplayTag& LayerTag) const;

	// ---- Focus and back ----
	/** Records the navigation focus inside a screen (presenter reports user navigation here). */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult SetFocusTarget(const FDocRequestHandle& Handle, FName Target);
	/** Back/cancel: routed to the top screen of the highest blocking layer (dialogs resolve Cancelled). */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult HandleBack();

	// ---- Dialogs ----
	/** Opens a modal dialog. Callback runs exactly once with the terminal result. */
	FDocSystemResult ShowDialog(const FDocDialogRequest& Request, UObject* Owner, FDocDialogCallback Callback, FDocRequestHandle& OutHandle);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult SubmitDialogInput(const FDocRequestHandle& Dialog, EDocDialogInput Input);
	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	EDocDialogResult GetDialogResult(const FDocRequestHandle& Dialog) const;

	// ---- Loading leases ----
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult AcquireLoading(UObject* Owner, FText Status, FDocRequestHandle& OutLease);
	/** Progress in [0,1]; only producers with a meaningful denominator report it. */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult SetLoadingLeaseProgress(const FDocRequestHandle& Lease, float Progress);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult SetLoadingLeaseIndeterminate(const FDocRequestHandle& Lease);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult SetLoadingLeaseStatus(const FDocRequestHandle& Lease, FText Status);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult ReleaseLoading(const FDocRequestHandle& Lease, EDocLoadingOutcome Outcome, FText ErrorText);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	void DismissLoadingError();
	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	FDocLoadingView GetLoadingView() const;

	/** Legacy API over one manager-owned lease (other owners' leases are unaffected). */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	void ShowLoading(FText Status);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	void HideLoading();
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	void SetLoadingProgress(float Progress);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	void SetLoadingStatus(FText Status);

	// ---- Travel ----
	/** Descriptors for restorable, non-modal screens (stable id + plain payload only). */
	TArray<FDocUIRestorationDescriptor> CaptureRestorationDescriptors() const;
	/** Closes every screen, resolves dialogs as OwnerDestroyed and drops dead-owner leases. */
	void NotifyTravel();
	/** Validates and pushes descriptors; unknown or non-restorable ids are skipped. */
	int32 RestoreScreens(const TArray<FDocUIRestorationDescriptor>& Descriptors);

	// ---- Clock ----
	/** Real-time step (dialog timeouts, owner checks). Auto-driven by the core ticker unless disabled. */
	void AdvanceClock(double Seconds);

	// ---- Diagnostics ----
	int32 GetStaleCallbackRejections() const { return StaleCallbacks; }
	TArray<FString> DescribeState() const;

	UPROPERTY(BlueprintAssignable, Category = "Doc|UI") FDocUIScreenStateEvent OnScreenStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|UI") FDocUILoadingEvent OnLoadingChanged;
	FDocUIScreenStateNative OnScreenStateChangedNative;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The screen's current control claim (diagnostics and tests). */
	FDocRequestHandle GetControlClaimForTesting(const FDocRequestHandle& Screen) const;

	/** Test entry: the player's teardown path without a subsystem collection. */
	void ShutdownForTesting() { Shutdown(); }

private:
	struct FScreen
	{
		FDocUIScreenInfo Info;
		TWeakObjectPtr<UDocUIScreenDefinition> Definition;
		int32 LoadToken = 0;
		FDocRequestHandle ControlClaim;
		FDocRequestHandle PauseLease;
		/** Replacement staging: close this screen once the new one loads. */
		FDocRequestHandle Replaces;
		bool bLoaded = false;
		int64 Order = 0;
	};

	struct FDialog
	{
		FDocRequestHandle Handle;
		FDocDialogRequest Request;
		TWeakObjectPtr<UObject> Owner;
		bool bOwnerWasSet = false;
		FDocDialogCallback Callback;
		double Remaining = 0.0;
		EDocDialogResult Result = EDocDialogResult::None;
	};

	struct FLoadingLease
	{
		TWeakObjectPtr<UObject> Owner;
		bool bOwnerWasSet = false;
		FText Status;
		TOptional<float> Progress;
		int64 Order = 0;
	};

	struct FApiScope
	{
		explicit FApiScope(UDocUIManagerSubsystem& InOwner);
		~FApiScope();
		UDocUIManagerSubsystem& Owner;
	};

	const FDocUILayerPolicy* Layer(const FGameplayTag& LayerTag) const;
	bool IsLive(EDocUIScreenState State) const;
	FDocSystemResult Validate(const FDocUIScreenRequest& Request, UDocUIScreenDefinition*& OutDef, bool bSkipDuplicate) const;
	FDocSystemResult CreateAndLoad(UDocUIScreenDefinition* Def, const FDocUIScreenRequest& Request, const FDocRequestHandle& Replaces, bool bDeferLoad, FDocRequestHandle& OutHandle);
	void StartLoad(const FDocRequestHandle& Handle);
	void OnPresentationLoaded(const FDocRequestHandle& Handle, int32 Token, bool bSuccess);
	void SetState(FScreen& Screen, EDocUIScreenState NewState);
	void Close(const FDocRequestHandle& Handle, EDocUIScreenState Terminal);
	void RefreshLayer(const FGameplayTag& LayerTag);
	void Activate(FScreen& Screen);
	void Suspend(FScreen& Screen);
	void AcquireClaims(FScreen& Screen);
	void ReleaseClaims(FScreen& Screen);
	void ApplyFocus(FScreen& Screen);
	/** After a close, give navigation focus back to the topmost surviving interactive screen. */
	void FocusTopmost();
	UObject* ResolveControlProvider() const;
	void ResolveDialog(const FDocRequestHandle& Handle, EDocDialogResult Result);
	void RememberTerminal(const FDocRequestHandle& Handle, EDocUIScreenState State);
	void BroadcastLoading();
	void Defer(TFunction<void()> Operation);
	void Flush();
	void Shutdown();
	bool Tick(float DeltaSeconds);

	UPROPERTY() TArray<TObjectPtr<UDocUIScreenDefinition>> DefinitionRefs;
	TMap<FName, TObjectPtr<UDocUIScreenDefinition>> Definitions;
	UPROPERTY() TObjectPtr<UDocUIScreenDefinition> DialogDefinition;

	TSharedPtr<IDocUIPresenter> Presenter;
	TWeakObjectPtr<UObject> ControlOverride;
	FGameplayTagContainer ContextTags;

	TDocHandleTable<FScreen> Screens;
	TMap<FGameplayTag, TArray<FDocRequestHandle>> Stacks;
	TMap<int64, EDocUIScreenState> TerminalStates;
	TArray<int64> TerminalOrder;
	TMap<int64, FDialog> Dialogs; // keyed by screen operation id
	TMap<int64, EDocDialogResult> DialogResults;

	TDocHandleTable<FLoadingLease> LoadingLeases;
	FDocRequestHandle LegacyLoadingLease;
	FDocLoadingView LoadingError;
	int64 LoadingOrder = 0;

	int64 Revision = 0;
	int64 NextOrder = 0;
	int32 ApiDepth = 0;
	bool bFlushing = false;
	TArray<TFunction<void()>> DeferredOps;
	TArray<TFunction<void()>> PendingEvents;
	int32 StaleCallbacks = 0;
	FTSTicker::FDelegateHandle TickHandle;
};
