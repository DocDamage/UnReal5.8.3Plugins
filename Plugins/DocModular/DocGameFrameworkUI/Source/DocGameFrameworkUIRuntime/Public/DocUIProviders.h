#pragma once

#include "CoreMinimal.h"
#include "DocUITypes.h"

/**
 * Native provider contracts for DocGameFrameworkUI. The base never creates widgets,
 * plays sounds, writes video modes or binds Enhanced Input itself: adapters do, through
 * these interfaces. All calls are game-thread only.
 */

/**
 * Screen presenter (the CommonUI bridge, a UMG reference presenter, or a test double).
 * Without a presenter the manager runs headless: presentation loads succeed immediately
 * and only logical state changes.
 */
class DOCGAMEFRAMEWORKUIRUNTIME_API IDocUIPresenter
{
public:
	virtual ~IDocUIPresenter() = default;

	/**
	 * Begin loading the presentation for PresentationKey. Call OnLoaded exactly once,
	 * synchronously or later. The manager ignores stale calls (cancelled, closed, removed
	 * player), so a late callback can never resurrect a screen.
	 */
	virtual void LoadPresentation(const FDocUIScreenInfo& Screen, TFunction<void(bool bSuccess)> OnLoaded) = 0;
	virtual void CancelLoad(const FDocUIScreenInfo& Screen) {}
	virtual void ShowScreen(const FDocUIScreenInfo& Screen) = 0;
	virtual void SuspendScreen(const FDocUIScreenInfo& Screen) {}
	virtual void HideScreen(const FDocUIScreenInfo& Screen) = 0;
	/** Whether a saved focus target still exists and can take focus. */
	virtual bool IsFocusable(const FDocUIScreenInfo& Screen, FName Target) const { return !Target.IsNone(); }
	virtual void ApplyFocus(const FDocUIScreenInfo& Screen, FName Target) {}
	/** Capabilities this presenter supports (checked against RequiredCapabilities). */
	virtual FGameplayTagContainer GetCapabilities() const { return FGameplayTagContainer(); }
};

/** Approved notification action. Revalidates the current context; never a serialized callback. */
class DOCGAMEFRAMEWORKUIRUNTIME_API IDocNotificationActionHandler
{
public:
	virtual ~IDocNotificationActionHandler() = default;
	virtual FDocSystemResult CanExecute(const FDocNotificationEntry& Entry) const { return FDocSystemResult::MakeSuccess(); }
	virtual FDocSystemResult Execute(const FDocNotificationEntry& Entry) = 0;
};

/**
 * Applies settings to their real consumer (UGameUserSettings, sound classes, culture...).
 * Providers with side effects declare their transactional limits.
 */
class DOCGAMEFRAMEWORKUIRUNTIME_API IDocSettingsProvider
{
public:
	virtual ~IDocSettingsProvider() = default;

	/** Capability check (hardware, renderer, installed plugin). */
	virtual bool IsAvailable(const FDocSettingDescriptor& Setting, FText& OutReason) const { return true; }
	/** Apply a value. bPreview = PreviewConfirm preview (reverted or confirmed later). */
	virtual FDocSystemResult Apply(const FDocSettingDescriptor& Setting, const FDocSettingValue& Value, bool bPreview) = 0;
	/** The consumer's actual reported state; false = cannot be observed (no proof either way). */
	virtual bool ReadActual(const FDocSettingDescriptor& Setting, FDocSettingValue& OutValue) const { return false; }
	/** Keep a previewed value (e.g. ConfirmVideoMode). */
	virtual FDocSystemResult Confirm(const FDocSettingDescriptor& Setting) { return FDocSystemResult::MakeSuccess(); }
	/** Persist confirmed native state (e.g. SaveSettings). Called only after confirmation. */
	virtual FDocSystemResult Persist() { return FDocSystemResult::MakeSuccess(); }
};

/** Durable settings storage. Save always receives the full current state, never an old snapshot. */
class DOCGAMEFRAMEWORKUIRUNTIME_API IDocSettingsStore
{
public:
	virtual ~IDocSettingsStore() = default;
	virtual bool Load(FDocSettingsStoreData& OutData) = 0;
	virtual bool Save(const FDocSettingsStoreData& Data) = 0;
};

/** Glyph art lookup per device presentation. None = no art (text fallback is used). */
class DOCGAMEFRAMEWORKUIRUNTIME_API IDocGlyphProvider
{
public:
	virtual ~IDocGlyphProvider() = default;
	virtual FName GetGlyph(const FKey& Key, EDocInputDeviceCategory Device) const = 0;
};

/** Generic function-backed settings provider for project hookups and tests. */
class DOCGAMEFRAMEWORKUIRUNTIME_API FDocCallbackSettingsProvider : public IDocSettingsProvider
{
public:
	TFunction<bool(const FDocSettingDescriptor&, FText&)> AvailableFn;
	TFunction<FDocSystemResult(const FDocSettingDescriptor&, const FDocSettingValue&, bool)> ApplyFn;
	TFunction<bool(const FDocSettingDescriptor&, FDocSettingValue&)> ReadFn;
	TFunction<FDocSystemResult(const FDocSettingDescriptor&)> ConfirmFn;

	virtual bool IsAvailable(const FDocSettingDescriptor& Setting, FText& OutReason) const override { return AvailableFn ? AvailableFn(Setting, OutReason) : true; }
	virtual FDocSystemResult Apply(const FDocSettingDescriptor& Setting, const FDocSettingValue& Value, bool bPreview) override
	{
		return ApplyFn ? ApplyFn(Setting, Value, bPreview) : FDocSystemResult::MakeSuccess();
	}
	virtual bool ReadActual(const FDocSettingDescriptor& Setting, FDocSettingValue& OutValue) const override { return ReadFn ? ReadFn(Setting, OutValue) : false; }
	virtual FDocSystemResult Confirm(const FDocSettingDescriptor& Setting) override { return ConfirmFn ? ConfirmFn(Setting) : FDocSystemResult::MakeSuccess(); }
};

/** Stores preferences only; the consumer proves application through AcknowledgeSetting. */
class DOCGAMEFRAMEWORKUIRUNTIME_API FDocPreferenceSettingsProvider : public IDocSettingsProvider
{
public:
	virtual FDocSystemResult Apply(const FDocSettingDescriptor& Setting, const FDocSettingValue& Value, bool bPreview) override { return FDocSystemResult::MakeSuccess(); }
};

/** Language/culture through FInternationalization. Unknown cultures are refused, never half-applied. */
class DOCGAMEFRAMEWORKUIRUNTIME_API FDocCultureSettingsProvider : public IDocSettingsProvider
{
public:
	virtual FDocSystemResult Apply(const FDocSettingDescriptor& Setting, const FDocSettingValue& Value, bool bPreview) override;
	virtual bool ReadActual(const FDocSettingDescriptor& Setting, FDocSettingValue& OutValue) const override;
};

/**
 * Video settings through the installed UGameUserSettings. Previews apply resolution/window
 * mode without saving (ApplySettings would also save); Confirm calls ConfirmVideoMode and
 * Persist calls SaveSettings. ReadActual reports the applied system resolution, so a setter
 * returning without error is never treated as proof.
 */
class DOCGAMEFRAMEWORKUIRUNTIME_API FDocGameUserSettingsProvider : public IDocSettingsProvider
{
public:
	virtual bool IsAvailable(const FDocSettingDescriptor& Setting, FText& OutReason) const override;
	virtual FDocSystemResult Apply(const FDocSettingDescriptor& Setting, const FDocSettingValue& Value, bool bPreview) override;
	virtual bool ReadActual(const FDocSettingDescriptor& Setting, FDocSettingValue& OutValue) const override;
	virtual FDocSystemResult Confirm(const FDocSettingDescriptor& Setting) override;
	virtual FDocSystemResult Persist() override;
};

/** Settings store in a config file section (GGameUserSettingsIni by default). */
class DOCGAMEFRAMEWORKUIRUNTIME_API FDocConfigSettingsStore : public IDocSettingsStore
{
public:
	explicit FDocConfigSettingsStore(const FString& InFilename = FString(), const FString& InSection = TEXT("DocGameFrameworkUI.Settings"))
		: Filename(InFilename), Section(InSection) {}

	virtual bool Load(FDocSettingsStoreData& OutData) override;
	virtual bool Save(const FDocSettingsStoreData& Data) override;

private:
	const FString& ResolveFilename() const;
	FString Filename;
	FString Section;
};

/** In-memory store (tests, platforms that persist through a save-game adapter). */
class DOCGAMEFRAMEWORKUIRUNTIME_API FDocMemorySettingsStore : public IDocSettingsStore
{
public:
	FDocSettingsStoreData Data;
	int32 Saves = 0;
	bool bFailSaves = false;

	virtual bool Load(FDocSettingsStoreData& OutData) override { OutData = Data; return true; }
	virtual bool Save(const FDocSettingsStoreData& InData) override
	{
		if (bFailSaves) { return false; }
		Data = InData;
		++Saves;
		return true;
	}
};
