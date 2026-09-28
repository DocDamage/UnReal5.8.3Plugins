#include "DocSettingsSubsystem.h"
#include "DocGameFrameworkUILog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocSettingsSubsystem)

#define LOCTEXT_NAMESPACE "DocGameFrameworkUISettings"

static TWeakObjectPtr<UDocSettingsSubsystem> GDocSettingsOverride;

UDocSettingsSubsystem* UDocSettingsSubsystem::Get(const UObject* WorldContextObject)
{
	if (UDocSettingsSubsystem* Override = GDocSettingsOverride.Get())
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
	return GameInstance ? GameInstance->GetSubsystem<UDocSettingsSubsystem>() : nullptr;
}

void UDocSettingsSubsystem::SetSubsystemOverrideForTesting(UDocSettingsSubsystem* Override)
{
	GDocSettingsOverride = Override;
}

void UDocSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!Store.IsValid())
	{
		Store = MakeShared<FDocConfigSettingsStore>();
	}
	RegisterStandardSettings(true);
	RecoverOnStartup(); // an unconfirmed preview from a crashed session goes back to safe values
	if (GetDefault<UDocUISettings>()->bAutoTick)
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDocSettingsSubsystem::Tick));
	}
}

void UDocSettingsSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	// Leaving with an open preview: revert now rather than keeping an unconfirmed mode.
	TArray<FDocRequestHandle> Previewing;
	Edits.ForEach([&Previewing](const FDocRequestHandle& H, const FEdit& E) { if (E.State == EDocSettingsEditState::Previewing) { Previewing.Add(H); } });
	for (const FDocRequestHandle& H : Previewing)
	{
		if (FEdit* E = Edits.Find(H, this)) { RevertPreview(*E, TEXT("Settings authority shut down")); }
	}
	Edits.Reset();
	Super::Deinitialize();
}

bool UDocSettingsSubsystem::Tick(float DeltaSeconds)
{
	AdvanceClock(DeltaSeconds);
	return true;
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

const FDocSettingDescriptor* UDocSettingsSubsystem::FindDescriptor(FName SettingId) const
{
	const int32* Index = DescriptorIndex.Find(SettingId);
	return Index ? &Descriptors[*Index] : nullptr;
}

IDocSettingsProvider* UDocSettingsSubsystem::FindProvider(const FDocSettingDescriptor& D) const
{
	const TSharedPtr<IDocSettingsProvider>* P = Providers.Find(D.ProviderId);
	return P && P->IsValid() ? P->Get() : nullptr;
}

void UDocSettingsSubsystem::RegisterProvider(FName ProviderId, TSharedPtr<IDocSettingsProvider> Provider)
{
	if (Provider.IsValid()) { Providers.Add(ProviderId, Provider); } else { Providers.Remove(ProviderId); }
}

FDocSystemResult UDocSettingsSubsystem::ValidateValue(const FDocSettingDescriptor& D, const FDocSettingValue& V) const
{
	if (V.Type != D.Type)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("%s: wrong value type"), *D.SettingId.ToString()), DocUITags::Error_UI_Setting);
	}
	switch (D.Type)
	{
	case EDocSettingType::Int:
		if (D.HasRange() && (V.IntValue < D.Min || V.IntValue > D.Max))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("%s: %lld out of range"), *D.SettingId.ToString(), V.IntValue), DocUITags::Error_UI_Setting);
		}
		break;
	case EDocSettingType::Float:
		if (!FMath::IsFinite(V.FloatValue) || (D.HasRange() && (V.FloatValue < D.Min || V.FloatValue > D.Max)))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("%s: value out of range"), *D.SettingId.ToString()), DocUITags::Error_UI_Setting);
		}
		break;
	case EDocSettingType::Enum:
		if (!D.Options.Contains(V.StringValue))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Printf(TEXT("%s: unknown option %s"), *D.SettingId.ToString(), *V.StringValue), DocUITags::Error_UI_Setting);
		}
		break;
	case EDocSettingType::String:
		if (V.StringValue.Len() > 256)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Value too long"), DocUITags::Error_UI_Setting);
		}
		break;
	default:
		break;
	}
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSettingsSubsystem::RegisterSetting(const FDocSettingDescriptor& Descriptor)
{
	if (Descriptor.SettingId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("SettingId is required"), DocUITags::Error_UI_Setting);
	}
	if (Descriptor.Type == EDocSettingType::Enum && Descriptor.Options.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Enum settings need options"), DocUITags::Error_UI_Setting);
	}
	const FDocSystemResult DefaultValid = ValidateValue(Descriptor, Descriptor.Default);
	if (!DefaultValid.IsSuccess())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Invalid default: %s"), *DefaultValid.Diagnostic), DocUITags::Error_UI_Setting);
	}
	if (FindDescriptor(Descriptor.SettingId))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Already registered"));
	}
	DescriptorIndex.Add(Descriptor.SettingId, Descriptors.Add(Descriptor));
	// Adopt values that were loaded before this setting was registered.
	const FString Suffix = TEXT(":") + Descriptor.SettingId.ToString();
	TArray<FString> Adopted;
	for (const TPair<FString, FString>& Pair : Preserved)
	{
		const bool bMatches = Descriptor.Scope == EDocSettingScope::Machine ? Pair.Key == TEXT("M") + Suffix
			: (Pair.Key.StartsWith(TEXT("P:")) && Pair.Key.EndsWith(Suffix));
		FDocSettingValue Value;
		if (bMatches && FDocSettingValue::FromString(Descriptor.Type, Pair.Value, Value) && ValidateValue(Descriptor, Value).IsSuccess())
		{
			Confirmed.Add(Pair.Key, Value);
			Adopted.Add(Pair.Key);
		}
	}
	for (const FString& Key : Adopted) { Preserved.Remove(Key); }
	return FDocSystemResult::MakeSuccess();
}

TArray<FDocSettingDescriptor> UDocSettingsSubsystem::GetSettings(EDocSettingCategory Category) const
{
	return Descriptors.FilterByPredicate([Category](const FDocSettingDescriptor& D) { return D.Category == Category; });
}

FString UDocSettingsSubsystem::StoreKey(const FDocSettingDescriptor& D, const FDocOwnerScope& Scope) const
{
	return D.Scope == EDocSettingScope::Machine ? FString::Printf(TEXT("M:%s"), *D.SettingId.ToString())
		: FString::Printf(TEXT("P:%s:%s"), *Scope.ToString(), *D.SettingId.ToString());
}

FDocOwnerScope UDocSettingsSubsystem::ScopeFor(const FDocSettingDescriptor& D, const FDocOwnerScope& Profile) const
{
	return D.Scope == EDocSettingScope::Machine ? FDocOwnerScope() : Profile;
}

int64 UDocSettingsSubsystem::FieldRevision(const FDocSettingDescriptor& D, const FDocOwnerScope& Scope) const
{
	return FieldRevisions.FindRef(StoreKey(D, Scope));
}

bool UDocSettingsSubsystem::IsAvailable(const FDocSettingDescriptor& D, FText& OutReason) const
{
	IDocSettingsProvider* Provider = FindProvider(D);
	if (!Provider)
	{
		OutReason = FText::Format(LOCTEXT("NoProvider", "Not supported on this system ({0})."), FText::FromName(D.ProviderId));
		return false;
	}
	return Provider->IsAvailable(D, OutReason);
}

bool UDocSettingsSubsystem::IsEnabledByDependency(const FDocSettingDescriptor& D, const FDocOwnerScope& Scope, const TMap<FName, FDocSettingValue>* Pending) const
{
	if (D.DependsOnSetting.IsNone())
	{
		return true;
	}
	const FDocSettingValue* PendingValue = Pending ? Pending->Find(D.DependsOnSetting) : nullptr;
	FDocSettingValue Value;
	if (PendingValue)
	{
		Value = *PendingValue;
	}
	else if (!GetValue(D.DependsOnSetting, Scope, Value))
	{
		return false; // unknown dependency: fail closed
	}
	return Value == D.DependsOnValue;
}

bool UDocSettingsSubsystem::GetValue(FName SettingId, const FDocOwnerScope& Scope, FDocSettingValue& OutValue) const
{
	const FDocSettingDescriptor* D = FindDescriptor(SettingId);
	if (!D)
	{
		return false;
	}
	const FDocSettingValue* V = Confirmed.Find(StoreKey(*D, ScopeFor(*D, Scope)));
	OutValue = V ? *V : D->Default;
	return true;
}

bool UDocSettingsSubsystem::GetSettingState(FName SettingId, const FDocOwnerScope& Scope, FDocSettingState& OutState) const
{
	const FDocSettingDescriptor* D = FindDescriptor(SettingId);
	if (!D)
	{
		return false;
	}
	const FDocOwnerScope S = ScopeFor(*D, Scope);
	const FString Key = StoreKey(*D, S);
	OutState = FDocSettingState();
	OutState.Descriptor = *D;
	GetValue(SettingId, S, OutState.Confirmed);
	OutState.bAvailable = IsAvailable(*D, OutState.UnavailableReason);
	OutState.bEnabledByDependency = IsEnabledByDependency(*D, S, nullptr);
	const FDocSettingValue* Ack = Acknowledged.Find(Key);
	OutState.bConsumerAcknowledged = Ack && *Ack == OutState.Confirmed;
	OutState.bRestartPending = RestartPending.Contains(Key);
	OutState.Revision = FieldRevisions.FindRef(Key);
	return true;
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

FDocSettingsEditView UDocSettingsSubsystem::MakeView(const FDocRequestHandle& Handle, const FEdit& Edit) const
{
	FDocSettingsEditView View;
	View.Handle = Handle;
	View.State = Edit.State;
	View.Pending = Edit.Pending;
	View.Previewed = Edit.Previewed;
	View.ConfirmSecondsRemaining = Edit.State == EDocSettingsEditState::Previewing ? FMath::Max(0.0, Edit.ConfirmRemaining) : 0.0;
	View.Diagnostic = Edit.Diagnostic;
	return View;
}

void UDocSettingsSubsystem::BroadcastEdit(const FDocRequestHandle& Handle, const FEdit& Edit)
{
	OnEditChanged.Broadcast(MakeView(Handle, Edit));
}

bool UDocSettingsSubsystem::GetEditView(const FDocRequestHandle& Edit, FDocSettingsEditView& OutView) const
{
	if (const FEdit* E = Edits.Find(Edit, this))
	{
		OutView = MakeView(Edit, *E);
		return true;
	}
	return false;
}

void UDocSettingsSubsystem::CloseEdit(const FDocRequestHandle& Edit)
{
	const FEdit* E = Edits.Find(Edit, this);
	if (E && E->State != EDocSettingsEditState::Previewing) // an open preview must be confirmed or reverted first
	{
		Edits.Remove(Edit, this);
	}
}

FDocSystemResult UDocSettingsSubsystem::BeginEdit(UObject* Owner, const FDocOwnerScope& ProfileScope, FDocRequestHandle& OutEdit)
{
	FEdit Edit;
	Edit.Owner = Owner;
	Edit.bOwnerWasSet = Owner != nullptr;
	Edit.Profile = ProfileScope;
	OutEdit = Edits.Add(this, MoveTemp(Edit));
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSettingsSubsystem::SetPending(const FDocRequestHandle& EditHandle, FName SettingId, const FDocSettingValue& Value)
{
	FEdit* E = Edits.Find(EditHandle, this);
	if (!E)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown edit"), DocUITags::Error_UI_Setting);
	}
	if (E->State != EDocSettingsEditState::Editing)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("This edit was already applied or closed"), DocUITags::Error_UI_Setting);
	}
	const FDocSettingDescriptor* D = FindDescriptor(SettingId);
	if (!D)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown setting"), DocUITags::Error_UI_Setting);
	}
	if (D->Scope == EDocSettingScope::Profile && !E->Profile.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Profile settings need a profile scope"), DocUITags::Error_UI_Setting);
	}
	const FDocSystemResult Valid = ValidateValue(*D, Value);
	if (!Valid.IsSuccess())
	{
		return Valid;
	}
	FText Reason;
	if (!IsAvailable(*D, Reason))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, Reason.ToString(), DocUITags::Error_UI_SettingUnavailable);
	}
	const FDocOwnerScope Scope = ScopeFor(*D, E->Profile);
	if (!IsEnabledByDependency(*D, Scope, &E->Pending))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, FString::Printf(TEXT("Disabled while %s differs"), *D->DependsOnSetting.ToString()), DocUITags::Error_UI_SettingUnavailable);
	}
	if (!E->BaseRevisions.Contains(SettingId))
	{
		E->BaseRevisions.Add(SettingId, FieldRevision(*D, Scope)); // the snapshot this edit is based on
	}
	E->Pending.Add(SettingId, Value);
	BroadcastEdit(EditHandle, *E);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSettingsSubsystem::ApplyAndVerify(const FDocSettingDescriptor& D, const FDocSettingValue& V, bool bPreview)
{
	IDocSettingsProvider* Provider = FindProvider(D);
	if (!Provider)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Provider missing"), DocUITags::Error_UI_SettingUnavailable);
	}
	const FDocSystemResult Applied = Provider->Apply(D, V, bPreview);
	if (!Applied.IsSuccess())
	{
		return Applied;
	}
	FDocSettingValue Actual;
	if (Provider->ReadActual(D, Actual) && Actual != V)
	{
		// A setter returning without error is not proof: the consumer reports something else.
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, FString::Printf(TEXT("%s reported %s instead of %s"),
			*D.SettingId.ToString(), *Actual.ToString(), *V.ToString()), DocUITags::Error_UI_SettingNotApplied);
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocSettingsSubsystem::Commit(const FDocSettingDescriptor& D, const FDocOwnerScope& Scope, const FDocSettingValue& V)
{
	const FString Key = StoreKey(D, Scope);
	Confirmed.Add(Key, V);
	FieldRevisions.FindOrAdd(Key) += 1;
	if (D.Scope == EDocSettingScope::Machine)
	{
		++MachineRevision;
	}
	OnSettingChangedNative.Broadcast(D.SettingId, Scope);
	OnSettingChanged.Broadcast(D.SettingId, Scope);
}

bool UDocSettingsSubsystem::SaveStore()
{
	if (!Store.IsValid())
	{
		return true;
	}
	FDocSettingsStoreData Data;
	Data.Values = Preserved; // unknown/unsupported preferences are kept, never overwritten by an old file
	for (const TPair<FString, FDocSettingValue>& Pair : Confirmed)
	{
		Data.Values.Add(Pair.Key, Pair.Value.ToString());
	}
	Data.PendingPreview = PendingPreviewKeys;
	const bool bSaved = Store->Save(Data);
	if (!bSaved)
	{
		UE_LOG(LogDocGameFrameworkUI, Warning, TEXT("Settings store rejected a save"));
	}
	return bSaved;
}

FDocSystemResult UDocSettingsSubsystem::ApplyEdit(const FDocRequestHandle& EditHandle)
{
	FEdit* E = Edits.Find(EditHandle, this);
	if (!E)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown edit"), DocUITags::Error_UI_Setting);
	}
	if (E->State != EDocSettingsEditState::Editing)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("This edit was already applied or closed"), DocUITags::Error_UI_Setting);
	}
	if (E->Pending.IsEmpty())
	{
		return FDocSystemResult::MakeNoChange(TEXT("Nothing to apply"));
	}
	// 1. Revalidate everything before any side effect.
	for (const TPair<FName, FDocSettingValue>& Pair : E->Pending)
	{
		const FDocSettingDescriptor* D = FindDescriptor(Pair.Key);
		const FDocOwnerScope Scope = D ? ScopeFor(*D, E->Profile) : FDocOwnerScope();
		if (!D)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Setting disappeared"), DocUITags::Error_UI_Setting);
		}
		if (FieldRevision(*D, Scope) != E->BaseRevisions.FindRef(Pair.Key))
		{
			// Another editor changed this field since the snapshot; independent fields would merge.
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, FString::Printf(TEXT("%s changed since this edit began"), *Pair.Key.ToString()), DocUITags::Error_UI_SettingConflict);
		}
		FText Reason;
		if (!IsAvailable(*D, Reason))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, Reason.ToString(), DocUITags::Error_UI_SettingUnavailable); // device removed mid-edit
		}
		if (!IsEnabledByDependency(*D, Scope, &E->Pending))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Disabled by a dependency"), DocUITags::Error_UI_SettingUnavailable);
		}
	}

	// 2. Immediate settings: apply and verify; roll back this batch on the first failure.
	TArray<TPair<const FDocSettingDescriptor*, FDocSettingValue>> Applied; // descriptor, previous value
	TSet<IDocSettingsProvider*> Persist;
	for (const TPair<FName, FDocSettingValue>& Pair : E->Pending)
	{
		const FDocSettingDescriptor* D = FindDescriptor(Pair.Key);
		if (D->ApplyPolicy != EDocSettingApplyPolicy::Immediate)
		{
			continue;
		}
		FDocSettingValue Previous;
		GetValue(Pair.Key, ScopeFor(*D, E->Profile), Previous);
		const FDocSystemResult R = ApplyAndVerify(*D, Pair.Value, false);
		if (!R.IsSuccess())
		{
			ApplyAndVerify(*D, Previous, false); // best effort: the consumer may have taken part of the failed value
			for (const TPair<const FDocSettingDescriptor*, FDocSettingValue>& Undo : Applied) { ApplyAndVerify(*Undo.Key, Undo.Value, false); }
			E->Diagnostic = R.Diagnostic;
			BroadcastEdit(EditHandle, *E);
			return R;
		}
		Applied.Add(TPair<const FDocSettingDescriptor*, FDocSettingValue>(D, Previous));
		Persist.Add(FindProvider(*D));
	}
	// Commit from copies: change listeners may call back into this subsystem.
	const FDocOwnerScope Profile = E->Profile;
	const TMap<FName, FDocSettingValue> PendingCopy = E->Pending;
	for (const TPair<const FDocSettingDescriptor*, FDocSettingValue>& Done : Applied)
	{
		Commit(*Done.Key, ScopeFor(*Done.Key, Profile), PendingCopy[Done.Key->SettingId]);
	}
	// 3. Restart settings: stored now, applied by the provider on next launch.
	for (const TPair<FName, FDocSettingValue>& Pair : PendingCopy)
	{
		const FDocSettingDescriptor* D = FindDescriptor(Pair.Key);
		if (D->ApplyPolicy == EDocSettingApplyPolicy::RequiresRestart)
		{
			const FDocOwnerScope Scope = ScopeFor(*D, Profile);
			RestartPending.Add(StoreKey(*D, Scope));
			Commit(*D, Scope, Pair.Value);
		}
	}
	for (IDocSettingsProvider* P : Persist) { if (P) { P->Persist(); } }
	E = Edits.Find(EditHandle, this);
	if (!E)
	{
		SaveStore();
		return FDocSystemResult::MakeSuccess();
	}

	// 4. Preview settings: record recovery metadata first, then preview with a real-time deadline.
	TArray<FName> PreviewIds;
	for (const TPair<FName, FDocSettingValue>& Pair : E->Pending)
	{
		const FDocSettingDescriptor* D = FindDescriptor(Pair.Key);
		if (D->ApplyPolicy == EDocSettingApplyPolicy::PreviewConfirm) { PreviewIds.Add(Pair.Key); }
	}
	if (PreviewIds.IsEmpty())
	{
		SaveStore();
		E->State = EDocSettingsEditState::Applied;
		BroadcastEdit(EditHandle, *E);
		return FDocSystemResult::MakeSuccess();
	}
	for (FName Id : PreviewIds)
	{
		const FDocSettingDescriptor* D = FindDescriptor(Id);
		const FDocOwnerScope Scope = ScopeFor(*D, E->Profile);
		FDocSettingValue Good;
		GetValue(Id, Scope, Good);
		E->LastKnownGood.Add(Id, Good);
		PendingPreviewKeys.AddUnique(StoreKey(*D, Scope));
	}
	SaveStore(); // abnormal termination during the preview recovers from this marker
	E->State = EDocSettingsEditState::Previewing;
	for (FName Id : PreviewIds)
	{
		const FDocSystemResult R = ApplyAndVerify(*FindDescriptor(Id), E->Pending[Id], true);
		E->Previewed.Add(Id); // even a failed preview may have changed the display: it is reverted below
		if (!R.IsSuccess())
		{
			const FDocSystemResult Reverted = RevertPreview(*E, FString::Printf(TEXT("Preview failed: %s"), *R.Diagnostic));
			BroadcastEdit(EditHandle, *E);
			return Reverted.IsSuccess() ? R : Reverted;
		}
	}
	E->ConfirmRemaining = GetDefault<UDocUISettings>()->PreviewConfirmSeconds;
	E->Diagnostic = TEXT("AwaitingConfirmation");
	BroadcastEdit(EditHandle, *E);
	FDocSystemResult Result = FDocSystemResult::MakeSuccess();
	Result.Diagnostic = TEXT("Preview applied; confirm before the deadline");
	return Result;
}

FDocSystemResult UDocSettingsSubsystem::RevertPreview(FEdit& Edit, const FString& Why)
{
	bool bOk = true;
	FString Failures;
	for (FName Id : Edit.Previewed)
	{
		const FDocSettingDescriptor* D = FindDescriptor(Id);
		const FDocSettingValue* Good = Edit.LastKnownGood.Find(Id);
		if (!D || !Good)
		{
			continue;
		}
		const FDocSystemResult R = ApplyAndVerify(*D, *Good, false);
		if (!R.IsSuccess())
		{
			bOk = false;
			Failures += R.Diagnostic + TEXT("; ");
		}
	}
	if (!bOk)
	{
		// Keep the recovery marker: the next launch retries the safe values.
		Edit.State = EDocSettingsEditState::RecoveryRequired;
		Edit.Diagnostic = FString::Printf(TEXT("%s; revert failed: %s"), *Why, *Failures);
		UE_LOG(LogDocGameFrameworkUI, Warning, TEXT("Settings revert failed: %s"), *Edit.Diagnostic);
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, Edit.Diagnostic, DocUITags::Error_UI_RevertFailed);
	}
	for (FName Id : Edit.Previewed)
	{
		if (const FDocSettingDescriptor* D = FindDescriptor(Id)) { PendingPreviewKeys.Remove(StoreKey(*D, ScopeFor(*D, Edit.Profile))); }
	}
	SaveStore();
	Edit.State = EDocSettingsEditState::Reverted;
	Edit.Diagnostic = Why;
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSettingsSubsystem::ConfirmEdit(const FDocRequestHandle& EditHandle)
{
	FEdit* E = Edits.Find(EditHandle, this);
	if (!E)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Unknown edit"), DocUITags::Error_UI_Setting);
	}
	if (E->State != EDocSettingsEditState::Previewing)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("No preview is waiting for confirmation"), DocUITags::Error_UI_Setting);
	}
	TSet<IDocSettingsProvider*> Persist;
	for (FName Id : E->Previewed)
	{
		const FDocSettingDescriptor* D = FindDescriptor(Id);
		IDocSettingsProvider* Provider = D ? FindProvider(*D) : nullptr;
		const FDocSystemResult R = Provider ? Provider->Confirm(*D) : FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Provider missing"));
		if (!R.IsSuccess())
		{
			const FDocSystemResult Reverted = RevertPreview(*E, FString::Printf(TEXT("Confirm failed: %s"), *R.Diagnostic));
			BroadcastEdit(EditHandle, *E);
			return Reverted.IsSuccess() ? R : Reverted;
		}
		Persist.Add(Provider);
	}
	const TArray<FName> Previewed = E->Previewed;
	const TMap<FName, FDocSettingValue> PendingCopy = E->Pending;
	const FDocOwnerScope Profile = E->Profile;
	E->State = EDocSettingsEditState::Applied;
	for (FName Id : Previewed)
	{
		const FDocSettingDescriptor* D = FindDescriptor(Id);
		const FDocOwnerScope Scope = ScopeFor(*D, Profile);
		PendingPreviewKeys.Remove(StoreKey(*D, Scope));
		Commit(*D, Scope, PendingCopy[Id]);
	}
	for (IDocSettingsProvider* P : Persist) { if (P) { P->Persist(); } }
	SaveStore();
	E = Edits.Find(EditHandle, this);
	if (!E)
	{
		return FDocSystemResult::MakeSuccess();
	}
	E->Diagnostic.Reset();
	BroadcastEdit(EditHandle, *E);
	return FDocSystemResult::MakeSuccess();
}

FDocSystemResult UDocSettingsSubsystem::RevertEdit(const FDocRequestHandle& EditHandle)
{
	FEdit* E = Edits.Find(EditHandle, this);
	if (!E || E->State != EDocSettingsEditState::Previewing)
	{
		return FDocSystemResult::MakeNoChange(TEXT("No preview to revert"));
	}
	const FDocSystemResult R = RevertPreview(*E, TEXT("Reverted"));
	BroadcastEdit(EditHandle, *E);
	return R;
}

FDocSystemResult UDocSettingsSubsystem::CancelEdit(const FDocRequestHandle& EditHandle)
{
	FEdit* E = Edits.Find(EditHandle, this);
	if (!E)
	{
		return FDocSystemResult::MakeNoChange(TEXT("Unknown edit"));
	}
	if (E->State == EDocSettingsEditState::Editing)
	{
		E->Pending.Reset(); // only this edit's values; newer unrelated updates stay
		E->State = EDocSettingsEditState::Cancelled;
		BroadcastEdit(EditHandle, *E);
		return FDocSystemResult::MakeSuccess();
	}
	if (E->State == EDocSettingsEditState::Previewing)
	{
		const FDocSystemResult R = RevertPreview(*E, TEXT("Cancelled"));
		if (R.IsSuccess()) { E->State = EDocSettingsEditState::Cancelled; }
		BroadcastEdit(EditHandle, *E);
		return R;
	}
	return FDocSystemResult::MakeNoChange(TEXT("Edit already finished"));
}

void UDocSettingsSubsystem::AcknowledgeSetting(FName SettingId, const FDocOwnerScope& Scope, const FDocSettingValue& Value)
{
	if (const FDocSettingDescriptor* D = FindDescriptor(SettingId))
	{
		Acknowledged.Add(StoreKey(*D, ScopeFor(*D, Scope)), Value);
	}
}

void UDocSettingsSubsystem::AdvanceClock(double Seconds)
{
	if (!(Seconds > 0.0))
	{
		return; // monotonic: backward or invalid steps never extend or shorten a deadline
	}
	TArray<FDocRequestHandle> Previewing;
	Edits.ForEach([&Previewing](const FDocRequestHandle& H, const FEdit& E) { if (E.State == EDocSettingsEditState::Previewing) { Previewing.Add(H); } });
	for (const FDocRequestHandle& H : Previewing)
	{
		FEdit* E = Edits.Find(H, this);
		if (!E || E->State != EDocSettingsEditState::Previewing)
		{
			continue;
		}
		if (E->bOwnerWasSet && !E->Owner.IsValid())
		{
			RevertPreview(*E, TEXT("The editing screen was lost"));
			BroadcastEdit(H, *E);
			continue;
		}
		E->ConfirmRemaining -= Seconds;
		if (E->ConfirmRemaining <= 0.0)
		{
			RevertPreview(*E, TEXT("Confirmation timed out"));
			if (FEdit* After = Edits.Find(H, this)) { BroadcastEdit(H, *After); }
		}
	}
}

// ---------------------------------------------------------------------------
// Recovery and standard settings
// ---------------------------------------------------------------------------

FDocSystemResult UDocSettingsSubsystem::RecoverOnStartup()
{
	if (!Store.IsValid())
	{
		return FDocSystemResult::MakeNoChange(TEXT("No store"));
	}
	FDocSettingsStoreData Data;
	if (!Store->Load(Data))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Settings store could not be read"), DocUITags::Error_UI_Setting);
	}
	for (const TPair<FString, FString>& Pair : Data.Values)
	{
		FString Id;
		bool bMachine = false;
		if (Pair.Key.StartsWith(TEXT("M:")))
		{
			Id = Pair.Key.Mid(2);
			bMachine = true;
		}
		else
		{
			int32 Colon = INDEX_NONE;
			Pair.Key.FindLastChar(TEXT(':'), Colon);
			Id = Colon != INDEX_NONE ? Pair.Key.Mid(Colon + 1) : FString();
		}
		const FDocSettingDescriptor* D = Id.IsEmpty() ? nullptr : FindDescriptor(FName(*Id));
		FDocSettingValue Value;
		const bool bUsable = D && (D->Scope == EDocSettingScope::Machine) == bMachine
			&& FDocSettingValue::FromString(D->Type, Pair.Value, Value) && ValidateValue(*D, Value).IsSuccess();
		if (bUsable)
		{
			Confirmed.Add(Pair.Key, Value);
		}
		else
		{
			Preserved.Add(Pair.Key, Pair.Value); // unsupported preference: kept, never blindly applied
		}
	}
	// Undo an unconfirmed preview left by abnormal termination.
	TArray<FString> StillPending;
	int32 Recovered = 0;
	for (const FString& Key : Data.PendingPreview)
	{
		const FString Id = Key.StartsWith(TEXT("M:")) ? Key.Mid(2) : FString();
		const FDocSettingDescriptor* D = Id.IsEmpty() ? nullptr : FindDescriptor(FName(*Id));
		if (!D)
		{
			StillPending.Add(Key);
			continue;
		}
		FDocSettingValue Safe;
		GetValue(D->SettingId, FDocOwnerScope(), Safe);
		if (ApplyAndVerify(*D, Safe, false).IsSuccess())
		{
			++Recovered;
		}
		else
		{
			StillPending.Add(Key);
		}
	}
	PendingPreviewKeys = StillPending;
	SaveStore();
	if (!StillPending.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Could not restore confirmed settings after an interrupted preview"), DocUITags::Error_UI_RevertFailed);
	}
	return Recovered > 0 ? FDocSystemResult::MakeSuccess() : FDocSystemResult::MakeNoChange(TEXT("No interrupted preview"));
}

void UDocSettingsSubsystem::RegisterStandardSettings(bool bInstallNativeVideoProvider)
{
	if (!Providers.Contains(TEXT("Preference"))) { RegisterProvider(TEXT("Preference"), MakeShared<FDocPreferenceSettingsProvider>()); }
	if (!Providers.Contains(TEXT("Culture"))) { RegisterProvider(TEXT("Culture"), MakeShared<FDocCultureSettingsProvider>()); }
	if (bInstallNativeVideoProvider && !Providers.Contains(TEXT("Video")) && GEngine && GEngine->GetGameUserSettings())
	{
		RegisterProvider(TEXT("Video"), MakeShared<FDocGameUserSettingsProvider>());
	}

	auto Add = [this](FName Id, FText Label, EDocSettingCategory Category, const FDocSettingValue& Default, EDocSettingScope Scope, EDocSettingApplyPolicy Policy,
		FName Provider, double Min = 0.0, double Max = -1.0, TArray<FString> Options = TArray<FString>(), FName DependsOn = NAME_None, FDocSettingValue DependsValue = FDocSettingValue())
	{
		FDocSettingDescriptor D;
		D.SettingId = Id;
		D.Label = Label;
		D.Category = Category;
		D.Type = Default.Type;
		D.Default = Default;
		D.Scope = Scope;
		D.ApplyPolicy = Policy;
		D.ProviderId = Provider;
		D.Min = Min;
		D.Max = Max;
		D.Options = MoveTemp(Options);
		D.DependsOnSetting = DependsOn;
		D.DependsOnValue = DependsValue;
		const FDocSystemResult R = RegisterSetting(D);
		if (!R.IsSuccess()) { UE_LOG(LogDocGameFrameworkUI, Warning, TEXT("Standard setting %s: %s"), *Id.ToString(), *R.Diagnostic); }
	};
	using V = FDocSettingValue;
	const EDocSettingScope M = EDocSettingScope::Machine;
	const EDocSettingScope P = EDocSettingScope::Profile;
	const EDocSettingApplyPolicy Now = EDocSettingApplyPolicy::Immediate;
	const EDocSettingApplyPolicy Preview = EDocSettingApplyPolicy::PreviewConfirm;

	// Video (machine authority; UGameUserSettings provider).
	Add(TEXT("Video.Resolution"), LOCTEXT("Resolution", "Resolution"), EDocSettingCategory::Video, V::MakeString(TEXT("1920x1080")), M, Preview, TEXT("Video"));
	Add(TEXT("Video.WindowMode"), LOCTEXT("WindowMode", "Window Mode"), EDocSettingCategory::Video, V::MakeEnum(TEXT("WindowedFullscreen")), M, Preview, TEXT("Video"),
		0.0, -1.0, { TEXT("Fullscreen"), TEXT("WindowedFullscreen"), TEXT("Windowed") });
	Add(TEXT("Video.VSync"), LOCTEXT("VSync", "VSync"), EDocSettingCategory::Video, V::MakeBool(false), M, Now, TEXT("Video"));
	Add(TEXT("Video.FrameLimit"), LOCTEXT("FrameLimit", "Frame Limit"), EDocSettingCategory::Video, V::MakeFloat(0.0), M, Now, TEXT("Video"), 0.0, 1000.0);
	Add(TEXT("Video.Scalability"), LOCTEXT("Scalability", "Overall Quality"), EDocSettingCategory::Video, V::MakeInt(3), M, Now, TEXT("Video"), 0.0, 4.0);
	Add(TEXT("Video.AntiAliasing"), LOCTEXT("AntiAliasing", "Anti-Aliasing"), EDocSettingCategory::Video, V::MakeInt(3), M, Now, TEXT("Video"), 0.0, 4.0);
	Add(TEXT("Video.Shadows"), LOCTEXT("Shadows", "Shadow Quality"), EDocSettingCategory::Video, V::MakeInt(3), M, Now, TEXT("Video"), 0.0, 4.0);
	Add(TEXT("Video.Textures"), LOCTEXT("Textures", "Texture Quality"), EDocSettingCategory::Video, V::MakeInt(3), M, Now, TEXT("Video"), 0.0, 4.0);
	Add(TEXT("Video.Effects"), LOCTEXT("Effects", "Effects Quality"), EDocSettingCategory::Video, V::MakeInt(3), M, Now, TEXT("Video"), 0.0, 4.0);
	Add(TEXT("Video.PostProcess"), LOCTEXT("PostProcess", "Post-Process Quality"), EDocSettingCategory::Video, V::MakeInt(3), M, Now, TEXT("Video"), 0.0, 4.0);
	Add(TEXT("Video.ViewDistance"), LOCTEXT("ViewDistance", "View Distance"), EDocSettingCategory::Video, V::MakeInt(3), M, Now, TEXT("Video"), 0.0, 4.0);
	Add(TEXT("Video.ResolutionScale"), LOCTEXT("ResolutionScale", "Resolution Scale"), EDocSettingCategory::Video, V::MakeFloat(1.0), M, Now, TEXT("Video"), 0.0, 1.0);
	// Capability-provided: unavailable (with a reason) until a project registers an "Upscaling" provider.
	Add(TEXT("Video.UpscalingMode"), LOCTEXT("Upscaling", "Upscaling"), EDocSettingCategory::Video, V::MakeEnum(TEXT("Off")), M, Now, TEXT("Upscaling"), 0.0, -1.0, { TEXT("Off") });

	// Audio channels (machine; routed by a project Sound Class/Submix provider registered as "Audio").
	for (const TCHAR* Channel : { TEXT("Master"), TEXT("Music"), TEXT("SFX"), TEXT("Dialogue"), TEXT("Ambience"), TEXT("UI") })
	{
		Add(FName(*FString::Printf(TEXT("Audio.%s"), Channel)), FText::Format(LOCTEXT("AudioChannel", "{0} Volume"), FText::FromString(Channel)),
			EDocSettingCategory::Audio, V::MakeFloat(1.0), M, Now, TEXT("Audio"), 0.0, 1.0);
	}

	// Input preferences (per profile).
	Add(TEXT("Input.MouseSensitivity"), LOCTEXT("MouseSensitivity", "Mouse Sensitivity"), EDocSettingCategory::Input, V::MakeFloat(1.0), P, Now, TEXT("Preference"), 0.1, 10.0);
	Add(TEXT("Input.ControllerSensitivity"), LOCTEXT("ControllerSensitivity", "Controller Sensitivity"), EDocSettingCategory::Input, V::MakeFloat(1.0), P, Now, TEXT("Preference"), 0.1, 10.0);
	Add(TEXT("Input.InvertX"), LOCTEXT("InvertX", "Invert X"), EDocSettingCategory::Input, V::MakeBool(false), P, Now, TEXT("Preference"));
	Add(TEXT("Input.InvertY"), LOCTEXT("InvertY", "Invert Y"), EDocSettingCategory::Input, V::MakeBool(false), P, Now, TEXT("Preference"));
	Add(TEXT("Input.DeadZone"), LOCTEXT("DeadZone", "Stick Dead Zone"), EDocSettingCategory::Input, V::MakeFloat(0.15), P, Now, TEXT("Preference"), 0.0, 0.9);
	Add(TEXT("Input.HoldToToggle"), LOCTEXT("HoldToToggle", "Toggle Instead of Hold"), EDocSettingCategory::Input, V::MakeBool(false), P, Now, TEXT("Preference"));

	// Accessibility (per profile; consumers acknowledge application).
	Add(TEXT("Accessibility.Subtitles"), LOCTEXT("Subtitles", "Subtitles"), EDocSettingCategory::Accessibility, V::MakeBool(true), P, Now, TEXT("Preference"));
	Add(TEXT("Accessibility.SubtitleSize"), LOCTEXT("SubtitleSize", "Subtitle Size"), EDocSettingCategory::Accessibility, V::MakeEnum(TEXT("Medium")), P, Now, TEXT("Preference"),
		0.0, -1.0, { TEXT("Small"), TEXT("Medium"), TEXT("Large"), TEXT("ExtraLarge") }, TEXT("Accessibility.Subtitles"), V::MakeBool(true));
	Add(TEXT("Accessibility.TextScale"), LOCTEXT("TextScale", "Text Scale"), EDocSettingCategory::Accessibility, V::MakeFloat(1.0), P, Now, TEXT("Preference"), 0.75, 2.0);
	Add(TEXT("Accessibility.HighContrast"), LOCTEXT("HighContrast", "High Contrast"), EDocSettingCategory::Accessibility, V::MakeBool(false), P, Now, TEXT("Preference"));
	Add(TEXT("Accessibility.ColorFilter"), LOCTEXT("ColorFilter", "Color Filter"), EDocSettingCategory::Accessibility, V::MakeEnum(TEXT("None")), P, Now, TEXT("Preference"),
		0.0, -1.0, { TEXT("None"), TEXT("Protanopia"), TEXT("Deuteranopia"), TEXT("Tritanopia") });
	Add(TEXT("Accessibility.ReduceMotion"), LOCTEXT("ReduceMotion", "Reduce Motion"), EDocSettingCategory::Accessibility, V::MakeBool(false), P, Now, TEXT("Preference"));
	Add(TEXT("Accessibility.CameraShakeStrength"), LOCTEXT("CameraShake", "Camera Shake"), EDocSettingCategory::Accessibility, V::MakeFloat(1.0), P, Now, TEXT("Preference"), 0.0, 1.0);
	Add(TEXT("Accessibility.HoldToToggleAlternatives"), LOCTEXT("HoldAlternatives", "Hold Alternatives"), EDocSettingCategory::Accessibility, V::MakeBool(false), P, Now, TEXT("Preference"));
	Add(TEXT("Accessibility.InputAssistance"), LOCTEXT("InputAssistance", "Input Assistance"), EDocSettingCategory::Accessibility, V::MakeBool(false), P, Now, TEXT("Preference"));

	// Language (machine; validated against the cultures FInternationalization knows).
	const FString CurrentCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	Add(TEXT("Language.Culture"), LOCTEXT("Language", "Language"), EDocSettingCategory::Language, V::MakeString(CurrentCulture), M, Now, TEXT("Culture"));
}

#undef LOCTEXT_NAMESPACE
