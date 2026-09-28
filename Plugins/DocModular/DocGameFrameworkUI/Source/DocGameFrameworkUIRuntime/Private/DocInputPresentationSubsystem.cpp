#include "DocInputPresentationSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Internationalization/Internationalization.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocInputPresentationSubsystem)

namespace DocInputPrivate
{
	bool IsGamepadCategory(EDocInputDeviceCategory C)
	{
		return C == EDocInputDeviceCategory::XboxController || C == EDocInputDeviceCategory::PlayStationController
			|| C == EDocInputDeviceCategory::GenericGamepad || C == EDocInputDeviceCategory::Custom;
	}

	bool IsAxis(const FKey& Key)
	{
		return Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D();
	}

	bool SameKeySet(const FDocInputBinding& A, const FDocInputBinding& B)
	{
		if (A.Keys.Num() != B.Keys.Num()) { return false; }
		for (const FKey& K : A.Keys) { if (!B.Keys.Contains(K)) { return false; } }
		return true;
	}

	bool ValidBinding(const FDocInputBinding& Binding, bool bGamepad)
	{
		for (const FKey& K : Binding.Keys)
		{
			if (!K.IsValid() || K.IsTouch() || K.IsGamepadKey() != bGamepad) { return false; }
		}
		return true;
	}

	bool SameBindings(const TArray<FDocInputBinding>& A, const TArray<FDocInputBinding>& B)
	{
		if (A.Num() != B.Num()) { return false; }
		for (int32 i = 0; i < A.Num(); ++i) { if (!(A[i] == B[i])) { return false; } }
		return true;
	}
}

using namespace DocInputPrivate;

UDocInputPresentationSubsystem* UDocInputPresentationSubsystem::Get(const ULocalPlayer* LocalPlayer)
{
	return LocalPlayer ? LocalPlayer->GetSubsystem<UDocInputPresentationSubsystem>() : nullptr;
}

void UDocInputPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CultureHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UDocInputPresentationSubsystem::OnCultureChanged);
}

void UDocInputPresentationSubsystem::Deinitialize()
{
	if (CultureHandle.IsValid() && FInternationalization::IsAvailable())
	{
		FInternationalization::Get().OnCultureChanged().Remove(CultureHandle);
	}
	CultureHandle.Reset();
	Cache.Reset();
	Super::Deinitialize();
}

void UDocInputPresentationSubsystem::OnCultureChanged()
{
	++Revision; // key labels are localized text
	Cache.Reset();
}

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

EDocInputDeviceCategory UDocInputPresentationSubsystem::Classify(const FDocInputSample& Sample, bool& bOutDeliberate) const
{
	const UDocUISettings* Settings = GetDefault<UDocUISettings>();
	const FKey& Key = Sample.Key;
	bOutDeliberate = false;
	if (!Key.IsValid())
	{
		return EDocInputDeviceCategory::Unknown;
	}
	if (Key.IsTouch())
	{
		bOutDeliberate = true;
		return EDocInputDeviceCategory::Touch;
	}
	if (Key.IsGamepadKey())
	{
		bOutDeliberate = !IsAxis(Key) || FMath::Abs(Sample.AnalogValue) >= Settings->GamepadSwitchThreshold; // stick drift is not a switch
		if (GamepadOverride != EDocInputDeviceCategory::Unknown) { return GamepadOverride; }
		if (IsGamepadCategory(Sample.VerifiedHardware)) { return Sample.VerifiedHardware; }
		return Settings->UnverifiedGamepadPresentation; // never infer a brand from the key
	}
	if (IsAxis(Key))
	{
		bOutDeliberate = FMath::Abs(Sample.AnalogValue) >= Settings->MouseSwitchThreshold; // insignificant mouse movement ignored
		return EDocInputDeviceCategory::KeyboardMouse;
	}
	bOutDeliberate = true;
	return EDocInputDeviceCategory::KeyboardMouse;
}

void UDocInputPresentationSubsystem::ReportInput(const FDocInputSample& Sample)
{
	bool bDeliberate = false;
	const EDocInputDeviceCategory Device = Classify(Sample, bDeliberate);
	if (!bDeliberate || Device == EDocInputDeviceCategory::Unknown || Device == Current)
	{
		return;
	}
	// Same device class without new verified metadata: keep the more specific presentation.
	if (IsGamepadCategory(Device) && IsGamepadCategory(Current) && GamepadOverride == EDocInputDeviceCategory::Unknown
		&& !IsGamepadCategory(Sample.VerifiedHardware))
	{
		return;
	}
	SwitchTo(Device);
}

void UDocInputPresentationSubsystem::SwitchTo(EDocInputDeviceCategory Device)
{
	const EDocInputDeviceCategory Old = Current;
	if (Old == Device)
	{
		return;
	}
	Current = Device;
	++Revision;
	Cache.Reset();
	OnInputDeviceChangedNative.Broadcast(Old, Device);
	OnInputDeviceChanged.Broadcast(Old, Device);
}

void UDocInputPresentationSubsystem::NotifyDeviceDisconnected(EDocInputDeviceCategory Device)
{
	const bool bCurrentLost = Device == Current || (IsGamepadCategory(Device) && IsGamepadCategory(Current));
	if (bCurrentLost)
	{
		SwitchTo(GetDefault<UDocUISettings>()->DisconnectFallback);
	}
}

void UDocInputPresentationSubsystem::SetGamepadPresentationOverride(EDocInputDeviceCategory Presentation)
{
	GamepadOverride = IsGamepadCategory(Presentation) ? Presentation : EDocInputDeviceCategory::Unknown;
	if (IsGamepadCategory(Current) && GamepadOverride != EDocInputDeviceCategory::Unknown)
	{
		SwitchTo(GamepadOverride);
	}
	++Revision;
	Cache.Reset();
}

// ---------------------------------------------------------------------------
// Bindings and presentation
// ---------------------------------------------------------------------------

FDocSystemResult UDocInputPresentationSubsystem::RegisterAction(const FDocActionBindings& InDefaults)
{
	if (InDefaults.ActionId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("ActionId is required"), DocUITags::Error_UI_Remap);
	}
	for (const FDocInputBinding& B : InDefaults.KeyboardMouse)
	{
		if (!ValidBinding(B, false)) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Keyboard/mouse scope contains an invalid or gamepad key"), DocUITags::Error_UI_Remap); }
	}
	for (const FDocInputBinding& B : InDefaults.Gamepad)
	{
		if (!ValidBinding(B, true)) { return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Gamepad scope contains an invalid or non-gamepad key"), DocUITags::Error_UI_Remap); }
	}
	if (InDefaults.bReservedNavigation && (InDefaults.KeyboardMouse.IsEmpty() || InDefaults.Gamepad.IsEmpty()))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Reserved navigation needs a binding in both scopes"), DocUITags::Error_UI_Remap);
	}
	Defaults.Add(InDefaults.ActionId, InDefaults);
	if (!Effective.Contains(InDefaults.ActionId))
	{
		Effective.Add(InDefaults.ActionId, InDefaults);
	}
	++Revision;
	Cache.Reset();
	return FDocSystemResult::MakeSuccess();
}

bool UDocInputPresentationSubsystem::GetEffectiveBindings(FName ActionId, FDocActionBindings& Out) const
{
	if (const FDocActionBindings* B = Effective.Find(ActionId))
	{
		Out = *B;
		return true;
	}
	return false;
}

FDocActionPresentation UDocInputPresentationSubsystem::GetActionPresentation(FName ActionId, EDocInputDeviceCategory Device)
{
	if (Device == EDocInputDeviceCategory::Unknown)
	{
		Device = Current;
	}
	const TPair<FName, uint8> Key(ActionId, static_cast<uint8>(Device));
	if (const FDocActionPresentation* Cached = Cache.Find(Key))
	{
		if (Cached->Revision == Revision) { return *Cached; }
	}
	FDocActionPresentation P;
	P.ActionId = ActionId;
	P.Device = Device;
	P.Revision = Revision;
	const FDocActionBindings* Bindings = Effective.Find(ActionId); // the player's effective mapping, never only the default
	if (Bindings && Device != EDocInputDeviceCategory::Touch)
	{
		for (const FDocInputBinding& B : Bindings->Scope(IsGamepadCategory(Device)))
		{
			FDocBindingPresentation& Out = P.Alternates.AddDefaulted_GetRef();
			Out.Keys = B.Keys;
			Out.bHold = B.bHold;
			Out.bChord = B.Keys.Num() > 1;
			for (const FKey& K : B.Keys)
			{
				if (!K.IsValid())
				{
					Out.bUnknownKey = true;
					Out.bMissingGlyph = true;
					Out.Glyphs.Add(NAME_None);
					Out.Labels.Add(NSLOCTEXT("DocGameFrameworkUI", "UnknownKey", "?"));
					continue;
				}
				const FName Glyph = GlyphProvider.IsValid() ? GlyphProvider->GetGlyph(K, Device) : NAME_None;
				Out.bMissingGlyph |= Glyph.IsNone();
				Out.Glyphs.Add(Glyph);
				Out.Labels.Add(K.GetDisplayName());
			}
		}
	}
	P.bUnbound = P.Alternates.IsEmpty();
	Cache.Add(Key, P);
	return P;
}

// ---------------------------------------------------------------------------
// Remapping
// ---------------------------------------------------------------------------

FDocSystemResult UDocInputPresentationSubsystem::BeginRemap()
{
	if (bRemapping)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Remap already in progress"));
	}
	Pending = Effective;
	bRemapping = true;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInputPresentationSubsystem::ValidateScope(const TMap<FName, FDocActionBindings>& Map) const
{
	for (const TPair<FName, FDocActionBindings>& Pair : Map)
	{
		if (Pair.Value.bReservedNavigation && (Pair.Value.KeyboardMouse.IsEmpty() || Pair.Value.Gamepad.IsEmpty()))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("%s must keep a binding in each scope"), *Pair.Key.ToString()), DocUITags::Error_UI_Remap);
		}
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInputPresentationSubsystem::RemapAction(FName ActionId, bool bGamepad, int32 SlotIndex, const FDocInputBinding& Binding, bool bReplaceConflicts, TArray<FDocRemapConflict>& OutConflicts)
{
	OutConflicts.Reset();
	if (!bRemapping)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Call BeginRemap first"), DocUITags::Error_UI_Remap);
	}
	FDocActionBindings* Action = Pending.Find(ActionId);
	if (!Action)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown action"), DocUITags::Error_UI_Remap);
	}
	TArray<FDocInputBinding>& Scope = Action->Scope(bGamepad);
	if (SlotIndex < 0 || SlotIndex > Scope.Num())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Invalid binding slot"), DocUITags::Error_UI_Remap);
	}
	if (Binding.Keys.IsEmpty())
	{
		if (SlotIndex == Scope.Num())
		{
			return FDocSystemResult::MakeNoChange(TEXT("Nothing to clear"));
		}
		if (Action->bReservedNavigation && Scope.Num() == 1)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("Navigation must keep a working confirm/back binding"), DocUITags::Error_UI_Remap);
		}
		Scope.RemoveAt(SlotIndex);
		return FDocSystemResult::MakeSuccess();
	}
	if (!ValidBinding(Binding, bGamepad))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Keys are invalid for this scope"), DocUITags::Error_UI_Remap);
	}
	for (const TPair<FName, FDocActionBindings>& Pair : Pending)
	{
		if (Pair.Key == ActionId) { continue; }
		for (const FDocInputBinding& Other : Pair.Value.Scope(bGamepad))
		{
			bool bConflict = SameKeySet(Other, Binding);
			// Reserved navigation keys can never be taken by another action.
			if (!bConflict && Pair.Value.bReservedNavigation && Other.Keys.Num() == 1 && Binding.Keys.Contains(Other.Keys[0]))
			{
				bConflict = true;
			}
			if (bConflict)
			{
				FDocRemapConflict& C = OutConflicts.AddDefaulted_GetRef();
				C.ActionId = Pair.Key;
				C.Key = Other.Keys.Num() > 0 ? Other.Keys[0] : FKey();
				C.bReserved = Pair.Value.bReservedNavigation;
			}
		}
	}
	if (!OutConflicts.IsEmpty())
	{
		const bool bReserved = OutConflicts.ContainsByPredicate([](const FDocRemapConflict& C) { return C.bReserved; });
		if (bReserved || !bReplaceConflicts)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, bReserved ? TEXT("Conflicts with reserved navigation") : TEXT("Conflicts with other actions"), DocUITags::Error_UI_Remap);
		}
		for (const FDocRemapConflict& C : OutConflicts)
		{
			if (FDocActionBindings* Other = Pending.Find(C.ActionId))
			{
				Other->Scope(bGamepad).RemoveAll([&Binding](const FDocInputBinding& B) { return SameKeySet(B, Binding); });
			}
		}
		Action = Pending.Find(ActionId); // stable map, but re-find for clarity
	}
	TArray<FDocInputBinding>& Target = Action->Scope(bGamepad);
	if (SlotIndex == Target.Num()) { Target.Add(Binding); } else { Target[SlotIndex] = Binding; }
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInputPresentationSubsystem::ResetRemapToDefaults(bool bGamepad)
{
	if (!bRemapping)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Call BeginRemap first"), DocUITags::Error_UI_Remap);
	}
	for (TPair<FName, FDocActionBindings>& Pair : Pending)
	{
		if (const FDocActionBindings* D = Defaults.Find(Pair.Key))
		{
			Pair.Value.Scope(bGamepad) = D->Scope(bGamepad);
		}
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInputPresentationSubsystem::ApplyRemap()
{
	if (!bRemapping)
	{
		return FDocSystemResult::MakeNoChange(TEXT("No remap in progress"));
	}
	const FDocSystemResult Valid = ValidateScope(Pending);
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	Effective = MoveTemp(Pending);
	Pending.Reset();
	bRemapping = false;
	++Revision; // cached glyphs are invalid now
	Cache.Reset();
	OnBindingsChanged.Broadcast();
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocInputPresentationSubsystem::CancelRemap()
{
	if (!bRemapping)
	{
		return FDocSystemResult::MakeNoChange(TEXT("No remap in progress"));
	}
	Pending.Reset();
	bRemapping = false;
	return FDocSystemResult::MakeSuccess();
}

FDocInputRemapSaveData UDocInputPresentationSubsystem::CaptureRemaps() const
{
	FDocInputRemapSaveData Data;
	for (const TPair<FName, FDocActionBindings>& Pair : Effective)
	{
		const FDocActionBindings* D = Defaults.Find(Pair.Key);
		if (!D || !SameBindings(D->KeyboardMouse, Pair.Value.KeyboardMouse) || !SameBindings(D->Gamepad, Pair.Value.Gamepad))
		{
			Data.Overrides.Add(Pair.Value);
		}
	}
	Data.Overrides.Sort([](const FDocActionBindings& A, const FDocActionBindings& B) { return A.ActionId.LexicalLess(B.ActionId); });
	return Data;
}

FDocSystemResult UDocInputPresentationSubsystem::RestoreRemaps(const FDocInputRemapSaveData& Data)
{
	if (Data.SchemaVersion <= 0 || Data.SchemaVersion > FDocInputRemapSaveData::CurrentSchemaVersion)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Unsupported remap schema"), DocUITags::Error_UI_Remap);
	}
	TMap<FName, FDocActionBindings> Candidate = Defaults;
	for (const FDocActionBindings& Override : Data.Overrides)
	{
		FDocActionBindings* Target = Candidate.Find(Override.ActionId);
		if (!Target)
		{
			continue; // the action no longer exists
		}
		const bool bValid = !Override.KeyboardMouse.ContainsByPredicate([](const FDocInputBinding& B) { return !ValidBinding(B, false); })
			&& !Override.Gamepad.ContainsByPredicate([](const FDocInputBinding& B) { return !ValidBinding(B, true); });
		if (bValid)
		{
			Target->KeyboardMouse = Override.KeyboardMouse;
			Target->Gamepad = Override.Gamepad;
		}
	}
	const FDocSystemResult Valid = ValidateScope(Candidate);
	if (!Valid.IsSuccess())
	{
		return Valid; // never restore a remap without a working navigation path
	}
	Effective = MoveTemp(Candidate);
	bRemapping = false;
	Pending.Reset();
	++Revision;
	Cache.Reset();
	OnBindingsChanged.Broadcast();
	return FDocSystemResult::MakeSuccess();
}
