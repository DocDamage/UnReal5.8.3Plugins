#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "DocUIProviders.h"
#include "DocInputPresentationSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDocInputDeviceChangedEvent, EDocInputDeviceCategory, OldDevice, EDocInputDeviceCategory, NewDevice);
DECLARE_MULTICAST_DELEGATE_TwoParams(FDocInputDeviceChangedNative, EDocInputDeviceCategory, EDocInputDeviceCategory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDocBindingsChangedEvent);

/**
 * Per-local-player input-device presentation, effective bindings, glyphs and remapping
 * (handoff 12.4). Never a process-global "last device".
 *
 * - The host input adapter reports samples. Analog noise below the thresholds never switches
 *   device; brand presentation comes only from verified metadata or a configured override.
 * - Presentation resolves the player's effective (remapped) bindings, never only the default.
 *   Rebinding, device switches and culture changes bump the revision and invalidate caches.
 * - Remapping is a transaction: Begin -> edits (conflicts declared, reserved navigation keeps a
 *   working confirm/back path) -> Apply or Cancel. Scopes are keyboard/mouse or gamepad.
 *
 * Enhanced Input user settings and CommonUI action bars are bridge concerns: an adapter feeds
 * samples and default bindings here and installs remaps into its own mapping contexts.
 */
UCLASS()
class DOCGAMEFRAMEWORKUIRUNTIME_API UDocInputPresentationSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UDocInputPresentationSubsystem* Get(const ULocalPlayer* LocalPlayer);

	// ---- Devices ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	void ReportInput(const FDocInputSample& Sample);
	/** Recovery when a device disconnects or focus is lost. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	void NotifyDeviceDisconnected(EDocInputDeviceCategory Device);
	/** Per-player presentation override for gamepads (Unknown clears it). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	void SetGamepadPresentationOverride(EDocInputDeviceCategory Presentation);
	UFUNCTION(BlueprintPure, Category = "Doc|Input")
	EDocInputDeviceCategory GetCurrentInputDevice() const { return Current; }

	// ---- Bindings and glyphs ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	FDocSystemResult RegisterAction(const FDocActionBindings& Defaults);
	void SetGlyphProvider(TSharedPtr<IDocGlyphProvider> Provider) { GlyphProvider = Provider; ++Revision; }
	UFUNCTION(BlueprintPure, Category = "Doc|Input")
	bool GetEffectiveBindings(FName ActionId, FDocActionBindings& Out) const;
	/** Presentation for the current device (or an explicit one). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	FDocActionPresentation GetActionPresentation(FName ActionId, EDocInputDeviceCategory Device = EDocInputDeviceCategory::Unknown);
	UFUNCTION(BlueprintPure, Category = "Doc|Input")
	int64 GetRevision() const { return Revision; }

	// ---- Remapping ----
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	FDocSystemResult BeginRemap();
	/**
	 * Sets one alternate (SlotIndex; == count appends) in one scope. Conflicts are reported in
	 * OutConflicts; with bReplaceConflicts they are unbound from the other (non-reserved) actions,
	 * otherwise the edit is refused. An empty binding clears the slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	FDocSystemResult RemapAction(FName ActionId, bool bGamepad, int32 SlotIndex, const FDocInputBinding& Binding, bool bReplaceConflicts, TArray<FDocRemapConflict>& OutConflicts);
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	FDocSystemResult ResetRemapToDefaults(bool bGamepad);
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	FDocSystemResult ApplyRemap();
	UFUNCTION(BlueprintCallable, Category = "Doc|Input")
	FDocSystemResult CancelRemap();
	UFUNCTION(BlueprintPure, Category = "Doc|Input")
	bool IsRemapping() const { return bRemapping; }

	FDocInputRemapSaveData CaptureRemaps() const;
	FDocSystemResult RestoreRemaps(const FDocInputRemapSaveData& Data);

	UPROPERTY(BlueprintAssignable, Category = "Doc|Input") FDocInputDeviceChangedEvent OnInputDeviceChanged;
	UPROPERTY(BlueprintAssignable, Category = "Doc|Input") FDocBindingsChangedEvent OnBindingsChanged;
	FDocInputDeviceChangedNative OnInputDeviceChangedNative;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	EDocInputDeviceCategory Classify(const FDocInputSample& Sample, bool& bOutDeliberate) const;
	void SwitchTo(EDocInputDeviceCategory Device);
	FDocSystemResult ValidateScope(const TMap<FName, FDocActionBindings>& Map) const;
	void OnCultureChanged();

	TMap<FName, FDocActionBindings> Defaults;
	TMap<FName, FDocActionBindings> Effective;
	TMap<FName, FDocActionBindings> Pending;
	bool bRemapping = false;
	TSharedPtr<IDocGlyphProvider> GlyphProvider;
	EDocInputDeviceCategory Current = EDocInputDeviceCategory::KeyboardMouse;
	EDocInputDeviceCategory GamepadOverride = EDocInputDeviceCategory::Unknown;
	int64 Revision = 1;
	TMap<TPair<FName, uint8>, FDocActionPresentation> Cache;
	FDelegateHandle CultureHandle;
};
