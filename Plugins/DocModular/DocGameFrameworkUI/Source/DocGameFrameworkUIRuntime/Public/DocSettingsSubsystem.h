#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DocRequestHandle.h"
#include "DocUIProviders.h"
#include "DocSettingsSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocSettingChangedEvent, FName, SettingId, const FDocOwnerScope&, Scope);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocSettingChangedNative, FName, const FDocOwnerScope&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDocSettingsEditEvent, const FDocSettingsEditView&, Edit);

/**
 * Typed settings registry and the single settings authority (handoff 12.5-12.6).
 *
 * - Machine settings (display) have one shared authority and per-field revisions. Profile
 *   settings are keyed by FDocOwnerScope. Two local players edit snapshots; applying a field
 *   that changed since the snapshot returns Conflict, while independent fields merge.
 * - Every edit is a transaction: BeginEdit -> SetPending (typed validation, capability,
 *   dependency) -> ApplyEdit -> (PreviewConfirm: ConfirmEdit or RevertEdit / real-time
 *   deadline / owner loss) -> persist confirmed values.
 * - Unconfirmed previews are recorded in the store before they are applied, so abnormal
 *   termination recovers to the confirmed values on the next launch (RecoverOnStartup).
 * - A value is reported applied only when the provider's actual state matches (when it can be
 *   observed). A failed revert is RecoveryRequired, never success.
 * - Consumer acknowledgement (AcknowledgeSetting) is separate: a toggle alone proves nothing.
 */
UCLASS()
class DOCGAMEFRAMEWORKUIRUNTIME_API UDocSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UDocSettingsSubsystem* Get(const UObject* WorldContextObject);

	// ---- Registry ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	FDocSystemResult RegisterSetting(const FDocSettingDescriptor& Descriptor);
	void RegisterProvider(FName ProviderId, TSharedPtr<IDocSettingsProvider> Provider);
	void SetStore(TSharedPtr<IDocSettingsStore> InStore) { Store = InStore; }
	/**
	 * Registers the standard Video/Audio/Input/Accessibility/Language descriptors and the
	 * built-in Preference and Culture providers (and the UGameUserSettings provider when the
	 * host has one). Audio channels and upscaling stay unavailable until a project provider is
	 * registered for "Audio" / "Upscaling".
	 */
	void RegisterStandardSettings(bool bInstallNativeVideoProvider = true);

	// ---- Reads ----
	/** Confirmed value (or default). Scope is ignored for machine settings. */
	UFUNCTION(BlueprintPure, Category = "Doc|Settings")
	bool GetValue(FName SettingId, const FDocOwnerScope& Scope, FDocSettingValue& OutValue) const;
	UFUNCTION(BlueprintPure, Category = "Doc|Settings")
	bool GetSettingState(FName SettingId, const FDocOwnerScope& Scope, FDocSettingState& OutState) const;
	UFUNCTION(BlueprintPure, Category = "Doc|Settings")
	TArray<FDocSettingDescriptor> GetSettings(EDocSettingCategory Category) const;
	UFUNCTION(BlueprintPure, Category = "Doc|Settings")
	int64 GetMachineRevision() const { return MachineRevision; }

	// ---- Edit transactions ----
	/** Owner: the screen/player object editing; losing it reverts an unconfirmed preview. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	FDocSystemResult BeginEdit(UObject* Owner, const FDocOwnerScope& ProfileScope, FDocRequestHandle& OutEdit);
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	FDocSystemResult SetPending(const FDocRequestHandle& Edit, FName SettingId, const FDocSettingValue& Value);
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	FDocSystemResult ApplyEdit(const FDocRequestHandle& Edit);
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	FDocSystemResult ConfirmEdit(const FDocRequestHandle& Edit);
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	FDocSystemResult RevertEdit(const FDocRequestHandle& Edit);
	/** Discards this edit's pending values (reverting its preview); never touches newer unrelated updates. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	FDocSystemResult CancelEdit(const FDocRequestHandle& Edit);
	UFUNCTION(BlueprintPure, Category = "Doc|Settings")
	bool GetEditView(const FDocRequestHandle& Edit, FDocSettingsEditView& OutView) const;
	/** Frees a finished edit record. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	void CloseEdit(const FDocRequestHandle& Edit);

	// ---- Consumers and recovery ----
	/** A consumer reports that it applied Value (e.g. the subtitle renderer took the new size). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	void AcknowledgeSetting(FName SettingId, const FDocOwnerScope& Scope, const FDocSettingValue& Value);
	/** Loads the store, preserves unknown/unsupported entries, and undoes any unconfirmed preview. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Settings")
	FDocSystemResult RecoverOnStartup();

	/** Real-time step (confirmation deadlines keep running while paused or dilated). */
	void AdvanceClock(double Seconds);

	UPROPERTY(BlueprintAssignable, Category = "Doc|Settings") FDocSettingChangedEvent OnSettingChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Settings") FDocSettingsEditEvent OnEditChanged;
	FDocSettingChangedNative OnSettingChangedNative;

	static void SetSubsystemOverrideForTesting(UDocSettingsSubsystem* Override);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	struct FEdit
	{
		TWeakObjectPtr<UObject> Owner;
		bool bOwnerWasSet = false;
		FDocOwnerScope Profile;
		TMap<FName, FDocSettingValue> Pending;
		TMap<FName, int64> BaseRevisions;
		EDocSettingsEditState State = EDocSettingsEditState::Editing;
		TArray<FName> Previewed;
		TMap<FName, FDocSettingValue> LastKnownGood;
		double ConfirmRemaining = 0.0;
		FString Diagnostic;
	};

	const FDocSettingDescriptor* FindDescriptor(FName SettingId) const;
	IDocSettingsProvider* FindProvider(const FDocSettingDescriptor& D) const;
	bool IsAvailable(const FDocSettingDescriptor& D, FText& OutReason) const;
	bool IsEnabledByDependency(const FDocSettingDescriptor& D, const FDocOwnerScope& Scope, const TMap<FName, FDocSettingValue>* Pending) const;
	FDocSystemResult ValidateValue(const FDocSettingDescriptor& D, const FDocSettingValue& V) const;
	FString StoreKey(const FDocSettingDescriptor& D, const FDocOwnerScope& Scope) const;
	FDocOwnerScope ScopeFor(const FDocSettingDescriptor& D, const FDocOwnerScope& Profile) const;
	int64 FieldRevision(const FDocSettingDescriptor& D, const FDocOwnerScope& Scope) const;
	void Commit(const FDocSettingDescriptor& D, const FDocOwnerScope& Scope, const FDocSettingValue& V);
	FDocSystemResult ApplyAndVerify(const FDocSettingDescriptor& D, const FDocSettingValue& V, bool bPreview);
	FDocSystemResult RevertPreview(FEdit& Edit, const FString& Why);
	bool SaveStore();
	void BroadcastEdit(const FDocRequestHandle& Handle, const FEdit& Edit);
	FDocSettingsEditView MakeView(const FDocRequestHandle& Handle, const FEdit& Edit) const;
	bool Tick(float DeltaSeconds);

	TArray<FDocSettingDescriptor> Descriptors;
	TMap<FName, int32> DescriptorIndex;
	TMap<FName, TSharedPtr<IDocSettingsProvider>> Providers;
	TSharedPtr<IDocSettingsStore> Store;

	/** Confirmed values keyed by store key ("M:<id>" / "P:<scope>:<id>"). */
	TMap<FString, FDocSettingValue> Confirmed;
	TMap<FString, int64> FieldRevisions;
	TMap<FString, FDocSettingValue> Acknowledged;
	TSet<FString> RestartPending;
	/** Entries loaded from the store that no registered setting understands (written back untouched). */
	TMap<FString, FString> Preserved;
	TArray<FString> PendingPreviewKeys;
	int64 MachineRevision = 0;

	TDocHandleTable<FEdit> Edits;
	FTSTicker::FDelegateHandle TickHandle;
};
