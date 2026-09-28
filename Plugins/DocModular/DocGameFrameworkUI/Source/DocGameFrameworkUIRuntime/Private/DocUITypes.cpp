#include "DocUITypes.h"
#include "DocUIProviders.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericWindow.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ConfigCacheIni.h"
#include "UnrealEngine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocUITypes)

namespace DocUITags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Layer, "UI.Layer", "DocGameFrameworkUI semantic layers");
	UE_DEFINE_GAMEPLAY_TAG(Layer_Game, "UI.Layer.Game");
	UE_DEFINE_GAMEPLAY_TAG(Layer_HUD, "UI.Layer.HUD");
	UE_DEFINE_GAMEPLAY_TAG(Layer_Menu, "UI.Layer.Menu");
	UE_DEFINE_GAMEPLAY_TAG(Layer_Modal, "UI.Layer.Modal");
	UE_DEFINE_GAMEPLAY_TAG(Layer_Popup, "UI.Layer.Popup");
	UE_DEFINE_GAMEPLAY_TAG(Layer_Loading, "UI.Layer.Loading");
	UE_DEFINE_GAMEPLAY_TAG(Layer_Debug, "UI.Layer.Debug");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Screen, "UI.Screen", "Screen tags");
	UE_DEFINE_GAMEPLAY_TAG(Screen_Dialog, "UI.Screen.Dialog");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Notification, "UI.Notification", "Notification tags");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Error_UI, "Doc.Error.UI", "DocGameFrameworkUI errors");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_UnknownScreen, "Doc.Error.UI.UnknownScreen");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Duplicate, "Doc.Error.UI.Duplicate");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_LayerFull, "Doc.Error.UI.LayerFull");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Blocked, "Doc.Error.UI.Blocked");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_StaleRevision, "Doc.Error.UI.StaleRevision");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_StaleHandle, "Doc.Error.UI.StaleHandle");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_PresentationFailed, "Doc.Error.UI.PresentationFailed");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Capability, "Doc.Error.UI.Capability");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Dialog, "Doc.Error.UI.Dialog");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Pause, "Doc.Error.UI.Pause");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Loading, "Doc.Error.UI.Loading");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Notification, "Doc.Error.UI.Notification");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Remap, "Doc.Error.UI.Remap");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_Setting, "Doc.Error.UI.Setting");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_SettingUnavailable, "Doc.Error.UI.SettingUnavailable");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_SettingConflict, "Doc.Error.UI.SettingConflict");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_SettingNotApplied, "Doc.Error.UI.SettingNotApplied");
	UE_DEFINE_GAMEPLAY_TAG(Error_UI_RevertFailed, "Doc.Error.UI.RevertFailed");
}

void UDocUIScreenDefinition::FindProblems(TArray<FString>& OutErrors) const
{
	if (ScreenId.IsNone()) { OutErrors.Add(TEXT("ScreenId is required")); }
	if (!LayerTag.IsValid()) { OutErrors.Add(FString::Printf(TEXT("Screen %s: LayerTag is required"), *ScreenId.ToString())); }
	if (PayloadVersion < 1) { OutErrors.Add(FString::Printf(TEXT("Screen %s: PayloadVersion must be >= 1"), *ScreenId.ToString())); }
	if (bRestorableAfterTravel && LayerTag == DocUITags::Layer_Modal)
	{
		OutErrors.Add(FString::Printf(TEXT("Screen %s: modals are never restored after travel"), *ScreenId.ToString()));
	}
	if (RequiredTags.HasAny(BlockedTags)) { OutErrors.Add(FString::Printf(TEXT("Screen %s: a tag is both required and blocked"), *ScreenId.ToString())); }
}

UDocUISettings::UDocUISettings()
{
	auto Layer = [this](const FGameplayTag& Tag, int32 Priority, bool bExclusive, bool bBlocks, int32 Capacity)
	{
		FDocUILayerPolicy& L = Layers.AddDefaulted_GetRef();
		L.LayerTag = Tag;
		L.Priority = Priority;
		L.bExclusive = bExclusive;
		L.bBlocksLowerLayers = bBlocks;
		L.Capacity = Capacity;
	};
	Layer(DocUITags::Layer_Game, 0, false, false, 0);
	Layer(DocUITags::Layer_HUD, 10, false, false, 0);
	Layer(DocUITags::Layer_Menu, 20, true, true, 16);
	Layer(DocUITags::Layer_Popup, 30, false, false, 8);
	Layer(DocUITags::Layer_Modal, 40, true, true, 8);
	Layer(DocUITags::Layer_Loading, 50, true, true, 1);
	Layer(DocUITags::Layer_Debug, 60, false, false, 0);
}

// ---------------------------------------------------------------------------
// Setting values
// ---------------------------------------------------------------------------

FString FDocSettingValue::ToString() const
{
	switch (Type)
	{
	case EDocSettingType::Bool: return BoolValue ? TEXT("true") : TEXT("false");
	case EDocSettingType::Int: return LexToString(IntValue);
	case EDocSettingType::Float: return FString::Printf(TEXT("%.9g"), FloatValue);
	default: return StringValue;
	}
}

bool FDocSettingValue::FromString(EDocSettingType InType, const FString& Text, FDocSettingValue& Out)
{
	Out = FDocSettingValue();
	Out.Type = InType;
	const FString Trimmed = Text.TrimStartAndEnd();
	switch (InType)
	{
	case EDocSettingType::Bool:
		if (Trimmed == TEXT("true")) { Out.BoolValue = true; return true; }
		if (Trimmed == TEXT("false")) { Out.BoolValue = false; return true; }
		return false;
	case EDocSettingType::Int:
		if (Trimmed.IsEmpty() || !Trimmed.IsNumeric() || Trimmed.Contains(TEXT("."))) { return false; }
		LexFromString(Out.IntValue, *Trimmed);
		return true;
	case EDocSettingType::Float:
	{
		if (Trimmed.IsEmpty() || !Trimmed.IsNumeric()) { return false; }
		double V = 0.0;
		LexFromString(V, *Trimmed);
		if (!FMath::IsFinite(V)) { return false; }
		Out.FloatValue = V;
		return true;
	}
	default:
		Out.StringValue = Text;
		return true;
	}
}

// ---------------------------------------------------------------------------
// Culture provider
// ---------------------------------------------------------------------------

FDocSystemResult FDocCultureSettingsProvider::Apply(const FDocSettingDescriptor& Setting, const FDocSettingValue& Value, bool bPreview)
{
	FInternationalization& I18N = FInternationalization::Get();
	if (!I18N.GetCulture(Value.StringValue).IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("Unknown culture %s"), *Value.StringValue), DocUITags::Error_UI_Setting);
	}
	if (!I18N.SetCurrentCulture(Value.StringValue))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, FString::Printf(TEXT("Culture %s could not be applied"), *Value.StringValue), DocUITags::Error_UI_SettingNotApplied);
	}
	return FDocSystemResult::MakeSuccess();
}

bool FDocCultureSettingsProvider::ReadActual(const FDocSettingDescriptor& Setting, FDocSettingValue& OutValue) const
{
	const FCultureRef Current = FInternationalization::Get().GetCurrentCulture();
	OutValue = Setting.Type == EDocSettingType::Enum ? FDocSettingValue::MakeEnum(Current->GetName()) : FDocSettingValue::MakeString(Current->GetName());
	return true;
}

// ---------------------------------------------------------------------------
// UGameUserSettings provider
// ---------------------------------------------------------------------------

namespace DocUIVideo
{
	UGameUserSettings* Settings() { return GEngine ? GEngine->GetGameUserSettings() : nullptr; }

	bool ParseResolution(const FString& Text, FIntPoint& Out)
	{
		FString X, Y;
		if (!Text.Split(TEXT("x"), &X, &Y) || !X.IsNumeric() || !Y.IsNumeric()) { return false; }
		Out = FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y));
		return Out.X > 0 && Out.Y > 0;
	}

	bool ParseWindowMode(const FString& Text, EWindowMode::Type& Out)
	{
		if (Text == TEXT("Fullscreen")) { Out = EWindowMode::Fullscreen; return true; }
		if (Text == TEXT("WindowedFullscreen")) { Out = EWindowMode::WindowedFullscreen; return true; }
		if (Text == TEXT("Windowed")) { Out = EWindowMode::Windowed; return true; }
		return false;
	}

	const TCHAR* WindowModeName(EWindowMode::Type Mode)
	{
		switch (Mode)
		{
		case EWindowMode::Fullscreen: return TEXT("Fullscreen");
		case EWindowMode::WindowedFullscreen: return TEXT("WindowedFullscreen");
		default: return TEXT("Windowed");
		}
	}

	bool IsQuality(FName Id)
	{
		static const TSet<FName> Names = { TEXT("Video.Scalability"), TEXT("Video.AntiAliasing"), TEXT("Video.Shadows"), TEXT("Video.Textures"),
			TEXT("Video.Effects"), TEXT("Video.PostProcess"), TEXT("Video.ViewDistance") };
		return Names.Contains(Id);
	}
}

bool FDocGameUserSettingsProvider::IsAvailable(const FDocSettingDescriptor& Setting, FText& OutReason) const
{
	if (!DocUIVideo::Settings())
	{
		OutReason = NSLOCTEXT("DocGameFrameworkUI", "NoGameUserSettings", "Video settings are not available on this host.");
		return false;
	}
	static const TSet<FName> Supported = { TEXT("Video.Resolution"), TEXT("Video.WindowMode"), TEXT("Video.VSync"), TEXT("Video.FrameLimit"), TEXT("Video.ResolutionScale") };
	if (!Supported.Contains(Setting.SettingId) && !DocUIVideo::IsQuality(Setting.SettingId))
	{
		OutReason = NSLOCTEXT("DocGameFrameworkUI", "UnsupportedVideoSetting", "This option is not provided by the native video settings.");
		return false;
	}
	return true;
}

FDocSystemResult FDocGameUserSettingsProvider::Apply(const FDocSettingDescriptor& Setting, const FDocSettingValue& Value, bool bPreview)
{
	UGameUserSettings* S = DocUIVideo::Settings();
	if (!S)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No UGameUserSettings"), DocUITags::Error_UI_SettingUnavailable);
	}
	const FName Id = Setting.SettingId;
	if (Id == FName(TEXT("Video.Resolution")))
	{
		FIntPoint Resolution;
		if (!DocUIVideo::ParseResolution(Value.StringValue, Resolution)) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Resolution must be WxH"), DocUITags::Error_UI_Setting); }
		S->SetScreenResolution(Resolution);
		S->ApplyResolutionSettings(false); // applies without SaveSettings (unlike ApplySettings)
		return FDocSystemResult::MakeSuccess();
	}
	if (Id == FName(TEXT("Video.WindowMode")))
	{
		EWindowMode::Type Mode;
		if (!DocUIVideo::ParseWindowMode(Value.StringValue, Mode)) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unknown window mode"), DocUITags::Error_UI_Setting); }
		S->SetFullscreenMode(Mode);
		S->ApplyResolutionSettings(false);
		return FDocSystemResult::MakeSuccess();
	}
	if (Id == FName(TEXT("Video.VSync"))) { S->SetVSyncEnabled(Value.BoolValue); }
	else if (Id == FName(TEXT("Video.FrameLimit"))) { S->SetFrameRateLimit(static_cast<float>(Value.FloatValue)); }
	else if (Id == FName(TEXT("Video.ResolutionScale"))) { S->SetResolutionScaleNormalized(static_cast<float>(Value.FloatValue)); }
	else if (Id == FName(TEXT("Video.Scalability"))) { S->SetOverallScalabilityLevel(static_cast<int32>(Value.IntValue)); }
	else if (Id == FName(TEXT("Video.AntiAliasing"))) { S->SetAntiAliasingQuality(static_cast<int32>(Value.IntValue)); }
	else if (Id == FName(TEXT("Video.Shadows"))) { S->SetShadowQuality(static_cast<int32>(Value.IntValue)); }
	else if (Id == FName(TEXT("Video.Textures"))) { S->SetTextureQuality(static_cast<int32>(Value.IntValue)); }
	else if (Id == FName(TEXT("Video.Effects"))) { S->SetVisualEffectQuality(static_cast<int32>(Value.IntValue)); }
	else if (Id == FName(TEXT("Video.PostProcess"))) { S->SetPostProcessingQuality(static_cast<int32>(Value.IntValue)); }
	else if (Id == FName(TEXT("Video.ViewDistance"))) { S->SetViewDistanceQuality(static_cast<int32>(Value.IntValue)); }
	else { return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Unsupported video setting"), DocUITags::Error_UI_SettingUnavailable); }
	S->ApplyNonResolutionSettings();
	return FDocSystemResult::MakeSuccess();
}

bool FDocGameUserSettingsProvider::ReadActual(const FDocSettingDescriptor& Setting, FDocSettingValue& OutValue) const
{
	const UGameUserSettings* S = DocUIVideo::Settings();
	if (!S)
	{
		return false;
	}
	const FName Id = Setting.SettingId;
	if (Id == FName(TEXT("Video.Resolution")))
	{
		// The applied system resolution, not the requested value.
		OutValue = FDocSettingValue::MakeString(FString::Printf(TEXT("%ux%u"), GSystemResolution.ResX, GSystemResolution.ResY));
		return true;
	}
	if (Id == FName(TEXT("Video.WindowMode")))
	{
		OutValue = FDocSettingValue::MakeEnum(DocUIVideo::WindowModeName(GSystemResolution.WindowMode));
		return true;
	}
	if (Id == FName(TEXT("Video.VSync"))) { OutValue = FDocSettingValue::MakeBool(S->IsVSyncEnabled()); return true; }
	if (Id == FName(TEXT("Video.FrameLimit"))) { OutValue = FDocSettingValue::MakeFloat(S->GetFrameRateLimit()); return true; }
	if (Id == FName(TEXT("Video.ResolutionScale"))) { OutValue = FDocSettingValue::MakeFloat(S->GetResolutionScaleNormalized()); return true; }
	if (Id == FName(TEXT("Video.Scalability"))) { OutValue = FDocSettingValue::MakeInt(S->GetOverallScalabilityLevel()); return true; }
	if (Id == FName(TEXT("Video.AntiAliasing"))) { OutValue = FDocSettingValue::MakeInt(S->GetAntiAliasingQuality()); return true; }
	if (Id == FName(TEXT("Video.Shadows"))) { OutValue = FDocSettingValue::MakeInt(S->GetShadowQuality()); return true; }
	if (Id == FName(TEXT("Video.Textures"))) { OutValue = FDocSettingValue::MakeInt(S->GetTextureQuality()); return true; }
	if (Id == FName(TEXT("Video.Effects"))) { OutValue = FDocSettingValue::MakeInt(S->GetVisualEffectQuality()); return true; }
	if (Id == FName(TEXT("Video.PostProcess"))) { OutValue = FDocSettingValue::MakeInt(S->GetPostProcessingQuality()); return true; }
	if (Id == FName(TEXT("Video.ViewDistance"))) { OutValue = FDocSettingValue::MakeInt(S->GetViewDistanceQuality()); return true; }
	return false;
}

FDocSystemResult FDocGameUserSettingsProvider::Confirm(const FDocSettingDescriptor& Setting)
{
	if (UGameUserSettings* S = DocUIVideo::Settings())
	{
		S->ConfirmVideoMode();
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No UGameUserSettings"), DocUITags::Error_UI_SettingUnavailable);
}

FDocSystemResult FDocGameUserSettingsProvider::Persist()
{
	if (UGameUserSettings* S = DocUIVideo::Settings())
	{
		S->SaveSettings();
		return FDocSystemResult::MakeSuccess();
	}
	return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No UGameUserSettings"), DocUITags::Error_UI_SettingUnavailable);
}

// ---------------------------------------------------------------------------
// Config store
// ---------------------------------------------------------------------------

const FString& FDocConfigSettingsStore::ResolveFilename() const
{
	return Filename.IsEmpty() ? GGameUserSettingsIni : Filename;
}

bool FDocConfigSettingsStore::Load(FDocSettingsStoreData& OutData)
{
	OutData = FDocSettingsStoreData();
	if (!GConfig)
	{
		return false;
	}
	TArray<FString> Lines;
	GConfig->GetArray(*Section, TEXT("Value"), Lines, ResolveFilename());
	for (const FString& Line : Lines)
	{
		FString Key, Value;
		if (Line.Split(TEXT("="), &Key, &Value)) // first '=' only; values may contain '='
		{
			OutData.Values.Add(Key, Value);
		}
	}
	GConfig->GetArray(*Section, TEXT("PendingPreview"), OutData.PendingPreview, ResolveFilename());
	return true;
}

bool FDocConfigSettingsStore::Save(const FDocSettingsStoreData& Data)
{
	if (!GConfig)
	{
		return false;
	}
	TArray<FString> Lines;
	for (const TPair<FString, FString>& Pair : Data.Values)
	{
		Lines.Add(Pair.Key + TEXT("=") + Pair.Value);
	}
	Lines.Sort();
	GConfig->SetArray(*Section, TEXT("Value"), Lines, ResolveFilename());
	GConfig->SetArray(*Section, TEXT("PendingPreview"), Data.PendingPreview, ResolveFilename());
	GConfig->Flush(false, ResolveFilename());
	return true;
}
