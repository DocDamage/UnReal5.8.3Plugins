// DocGameFrameworkUI automation tests (UI-01..11 base logic). No CommonUI, UMG, Enhanced
// Input or Streaming: presentation, pause, providers and stores are native test doubles.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocUIManagerSubsystem.h"
#include "DocNotificationSubsystem.h"
#include "DocInputPresentationSubsystem.h"
#include "DocSettingsSubsystem.h"
#include "DocCoreTags.h"
#include "DocReferencePlayerControlProvider.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "NativeGameplayTags.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DocUITests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_QuestNote, "UI.Notification.Test.Quest");

	const FGuid Campaign(3, 3, 3, 3);
	FDocOwnerScope Profile(int32 N) { return FDocOwnerScope(EDocOwnerScopeKind::PlayerProfile, FGuid(0x0E0E, 1, 1, static_cast<uint32>(N)), Campaign); }

	UDocUIScreenDefinition* MakeScreen(FName Id, const FGameplayTag& Layer, EDocUIDuplicatePolicy Duplicate = EDocUIDuplicatePolicy::Reject,
		EDocUIInputMode Input = EDocUIInputMode::UIOnly, FName DefaultFocus = TEXT("First"))
	{
		UDocUIScreenDefinition* D = NewObject<UDocUIScreenDefinition>(GetTransientPackage());
		D->ScreenId = Id;
		D->LayerTag = Layer;
		D->DuplicatePolicy = Duplicate;
		D->InputMode = Input;
		D->CursorPolicy = Input == EDocUIInputMode::Game ? EDocUICursorPolicy::Unchanged : EDocUICursorPolicy::Show;
		D->DefaultFocusTarget = DefaultFocus;
		D->PresentationKey = Id;
		return D;
	}

	FDocUIScreenRequest Req(FName Id)
	{
		FDocUIScreenRequest R;
		R.ScreenId = Id;
		return R;
	}

	class FTestPresenter final : public IDocUIPresenter
	{
	public:
		bool bAsync = false;
		TArray<TPair<FDocUIScreenInfo, TFunction<void(bool)>>> PendingLoads;
		TArray<FName> Shown;
		TArray<FName> Hidden;
		TArray<FName> Focused;
		TSet<FName> Unfocusable;
		int32 Cancelled = 0;
		TFunction<void(const FDocUIScreenInfo&)> OnShow;

		virtual void LoadPresentation(const FDocUIScreenInfo& Screen, TFunction<void(bool)> OnLoaded) override
		{
			if (bAsync) { PendingLoads.Add(TPair<FDocUIScreenInfo, TFunction<void(bool)>>(Screen, MoveTemp(OnLoaded))); }
			else { OnLoaded(true); }
		}
		virtual void CancelLoad(const FDocUIScreenInfo&) override { ++Cancelled; }
		virtual void ShowScreen(const FDocUIScreenInfo& Screen) override
		{
			Shown.Add(Screen.ScreenId);
			if (OnShow) { OnShow(Screen); }
		}
		virtual void HideScreen(const FDocUIScreenInfo& Screen) override { Hidden.Add(Screen.ScreenId); }
		virtual bool IsFocusable(const FDocUIScreenInfo&, FName Target) const override { return !Target.IsNone() && !Unfocusable.Contains(Target); }
		virtual void ApplyFocus(const FDocUIScreenInfo&, FName Target) override { Focused.Add(Target); }

		void Complete(int32 Index, bool bSuccess)
		{
			if (PendingLoads.IsValidIndex(Index))
			{
				TFunction<void(bool)> Callback = PendingLoads[Index].Value;
				Callback(bSuccess);
			}
		}
	};

	/** One local player: UI manager, control provider, notifications, input presentation. */
	struct FPlayer
	{
		TStrongObjectPtr<ULocalPlayer> Player;
		TStrongObjectPtr<UDocUIManagerSubsystem> UI;
		TStrongObjectPtr<UDocReferencePlayerControlProvider> Control;
		TStrongObjectPtr<UDocNotificationSubsystem> Notes;
		TStrongObjectPtr<UDocInputPresentationSubsystem> Input;
		TSharedPtr<FTestPresenter> Presenter;

		explicit FPlayer(bool bWithPresenter = true)
		{
			Player.Reset(NewObject<ULocalPlayer>(GEngine));
			UI.Reset(NewObject<UDocUIManagerSubsystem>(Player.Get()));
			Notes.Reset(NewObject<UDocNotificationSubsystem>(Player.Get()));
			Input.Reset(NewObject<UDocInputPresentationSubsystem>(Player.Get()));
			Control.Reset(NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage()));
			Control->Bind(Player.Get());
			UI->SetControlProviderOverride(Control.Get());
			if (bWithPresenter)
			{
				Presenter = MakeShared<FTestPresenter>();
				UI->SetPresenter(Presenter);
			}
		}
		~FPlayer()
		{
			UI->OnScreenStateChangedNative.Clear();
			UI->ShutdownForTesting();
			Control->ShutdownProvider();
		}

		bool IsFocusOwner(const FDocRequestHandle& Screen) const
		{
			const FDocRequestHandle Claim = UI->GetControlClaimForTesting(Screen);
			return Claim.IsSet() && IDocPlayerControlProvider::Execute_IsDocEffectiveControlOwner(Control.Get(), Claim, DocCoreTags::Control_Focus);
		}
	};

	/** Shared pause authority with a counting applier. */
	struct FPauseAuthority
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocUIPauseSubsystem> Pause;
		bool bPaused = false;
		int32 PauseCalls = 0;
		int32 UnpauseCalls = 0;

		FPauseAuthority()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			Pause.Reset(NewObject<UDocUIPauseSubsystem>(GameInstance.Get()));
			Pause->SetPolicyOverrideForTesting(true);
			Pause->SetPauseApplierForTesting([this](bool bPause) { bPaused = bPause; (bPause ? PauseCalls : UnpauseCalls)++; return true; });
			UDocUIPauseSubsystem::SetSubsystemOverrideForTesting(Pause.Get());
		}
		~FPauseAuthority()
		{
			UDocUIPauseSubsystem::SetSubsystemOverrideForTesting(nullptr);
		}
	};

	struct FSettingsFixture
	{
		TStrongObjectPtr<UGameInstance> GameInstance;
		TStrongObjectPtr<UDocSettingsSubsystem> S;
		TSharedPtr<FDocMemorySettingsStore> Store;
		TMap<FName, FDocSettingValue> Live; // what the "consumer" currently shows
		TMap<FName, FDocSettingValue> Reported; // override for ReadActual (monitor refused etc.)
		TSet<FName> FailApply;
		TFunction<bool(FName, const FDocSettingValue&)> RefuseValue;
		int32 Confirms = 0;
		TSharedPtr<FDocCallbackSettingsProvider> Provider;

		FSettingsFixture()
		{
			GameInstance.Reset(NewObject<UGameInstance>(GEngine));
			S.Reset(NewObject<UDocSettingsSubsystem>(GameInstance.Get()));
			Store = MakeShared<FDocMemorySettingsStore>();
			S->SetStore(Store);
			Provider = MakeShared<FDocCallbackSettingsProvider>();
			Provider->ApplyFn = [this](const FDocSettingDescriptor& D, const FDocSettingValue& V, bool)
			{
				if (FailApply.Contains(D.SettingId) || (RefuseValue && RefuseValue(D.SettingId, V)))
				{
					return FDocSystemResult::MakeFailure(EDocResultOutcome::Failed, TEXT("Consumer refused"));
				}
				Live.Add(D.SettingId, V);
				return FDocSystemResult::MakeSuccess();
			};
			Provider->ReadFn = [this](const FDocSettingDescriptor& D, FDocSettingValue& Out)
			{
				if (const FDocSettingValue* R = Reported.Find(D.SettingId)) { Out = *R; return true; }
				if (const FDocSettingValue* L = Live.Find(D.SettingId)) { Out = *L; return true; }
				return false;
			};
			Provider->ConfirmFn = [this](const FDocSettingDescriptor&) { ++Confirms; return FDocSystemResult::MakeSuccess(); };
			S->RegisterProvider(TEXT("Test"), Provider);
			S->RegisterProvider(TEXT("Preference"), MakeShared<FDocPreferenceSettingsProvider>());
		}

		FDocSettingDescriptor Setting(FName Id, const FDocSettingValue& Default, EDocSettingScope Scope = EDocSettingScope::Machine,
			EDocSettingApplyPolicy Policy = EDocSettingApplyPolicy::Immediate, FName ProviderId = TEXT("Test"))
		{
			FDocSettingDescriptor D;
			D.SettingId = Id;
			D.Type = Default.Type;
			D.Default = Default;
			D.Scope = Scope;
			D.ApplyPolicy = Policy;
			D.ProviderId = ProviderId;
			return D;
		}

		FDocSettingValue Value(FName Id, const FDocOwnerScope& Scope = FDocOwnerScope()) const
		{
			FDocSettingValue V;
			S->GetValue(Id, Scope, V);
			return V;
		}
	};
}

using namespace DocUITests;

// UI-01 (and UI-12 base: headless operation without any presenter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUIPushPopTest, "Doc.UI.PushPop", DocUITests::Flags)
bool FDocUIPushPopTest::RunTest(const FString& Parameters)
{
	{
		FPlayer Headless(false);
		Headless.UI->RegisterScreenDefinition(MakeScreen(TEXT("Main"), DocUITags::Layer_Menu));
		FDocRequestHandle H;
		TestTrue(TEXT("Headless push"), Headless.UI->PushScreen(Req(TEXT("Main")), H).IsSuccess());
		TestEqual(TEXT("Headless active"), Headless.UI->GetScreenState(H), EDocUIScreenState::Active);
		TestTrue(TEXT("Headless pop"), Headless.UI->PopScreen(H).IsSuccess());
	}

	FPlayer P;
	TArray<FDocUIScreenStateChange> Changes;
	P.UI->OnScreenStateChangedNative.AddLambda([&Changes](const FDocUIScreenStateChange& C) { Changes.Add(C); });
	TestEqual(TEXT("Unknown layer policy rejected"), P.UI->RegisterScreenDefinition(MakeScreen(TEXT("Bad"), FGameplayTag())).Outcome, EDocResultOutcome::InvalidConfiguration);
	P.UI->RegisterScreenDefinition(MakeScreen(TEXT("Main"), DocUITags::Layer_Menu));
	P.UI->RegisterScreenDefinition(MakeScreen(TEXT("Options"), DocUITags::Layer_Menu));
	P.UI->RegisterScreenDefinition(MakeScreen(TEXT("Inventory"), DocUITags::Layer_Menu, EDocUIDuplicatePolicy::FocusExisting));
	P.UI->RegisterScreenDefinition(MakeScreen(TEXT("Toast"), DocUITags::Layer_Popup, EDocUIDuplicatePolicy::AllowDistinctInstances, EDocUIInputMode::Game));

	FDocRequestHandle Main, Options, Unknown;
	TestEqual(TEXT("Unknown screen"), P.UI->PushScreen(Req(TEXT("Nope")), Unknown).Outcome, EDocResultOutcome::NotFound);
	TestTrue(TEXT("Push main"), P.UI->PushScreen(Req(TEXT("Main")), Main).IsSuccess());
	TestTrue(TEXT("Push options"), P.UI->PushScreen(Req(TEXT("Options")), Options).IsSuccess());
	TestEqual(TEXT("Exclusive layer suspends below"), P.UI->GetScreenState(Main), EDocUIScreenState::Suspended);
	TestEqual(TEXT("Top active"), P.UI->GetActiveScreen(DocUITags::Layer_Menu), Options);
	TestTrue(TEXT("State events after commit"), Changes.ContainsByPredicate([&Main](const FDocUIScreenStateChange& C) { return C.Handle == Main && C.NewState == EDocUIScreenState::Suspended; }));
	FDocRequestHandle Dup;
	TestEqual(TEXT("Reject duplicate"), P.UI->PushScreen(Req(TEXT("Main")), Dup).Outcome, EDocResultOutcome::Conflict);

	FDocRequestHandle ToastA, ToastB;
	P.UI->PushScreen(Req(TEXT("Toast")), ToastA);
	P.UI->PushScreen(Req(TEXT("Toast")), ToastB);
	FDocUIScreenInfo InfoA, InfoB;
	P.UI->GetScreenInfo(ToastA, InfoA);
	P.UI->GetScreenInfo(ToastB, InfoB);
	TestTrue(TEXT("Distinct instances"), ToastA != ToastB && InfoA.InstanceId != InfoB.InstanceId);
	TestTrue(TEXT("Non-exclusive layer keeps both active"), InfoA.State == EDocUIScreenState::Active && InfoB.State == EDocUIScreenState::Active);

	FDocRequestHandle Inv1, Inv2;
	P.UI->PushScreen(Req(TEXT("Inventory")), Inv1);
	TestEqual(TEXT("FocusExisting returns the existing instance"), P.UI->PushScreen(Req(TEXT("Inventory")), Inv2).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Same handle"), Inv2, Inv1);
	P.UI->PopScreen(Inv1);

	FDocUIScreenRequest Stale = Req(TEXT("Inventory"));
	Stale.ExpectedRevision = P.UI->GetRevision() - 1;
	TestEqual(TEXT("Stale revision"), P.UI->PushScreen(Stale, Dup).Outcome, EDocResultOutcome::Conflict);

	TestTrue(TEXT("Pop options"), P.UI->PopScreen(Options).IsSuccess());
	TestEqual(TEXT("Main reactivated"), P.UI->GetScreenState(Main), EDocUIScreenState::Active);
	TestEqual(TEXT("Closed"), P.UI->GetScreenState(Options), EDocUIScreenState::Closed);
	TestEqual(TEXT("Pop is idempotent"), P.UI->PopScreen(Options).Outcome, EDocResultOutcome::NoChange);

	// Async cancellation: a late load callback never resurrects the screen.
	P.Presenter->bAsync = true;
	FDocRequestHandle Pending;
	P.UI->PushScreen(Req(TEXT("Options")), Pending);
	TestEqual(TEXT("Loading"), P.UI->GetScreenState(Pending), EDocUIScreenState::LoadingPresentation);
	TestTrue(TEXT("Cancel"), P.UI->CancelScreenRequest(Pending).IsSuccess());
	TestEqual(TEXT("Presenter told to cancel"), P.Presenter->Cancelled, 1);
	const int32 ShownBefore = P.Presenter->Shown.Num();
	P.Presenter->Complete(0, true);
	TestEqual(TEXT("Stale callback rejected"), P.UI->GetStaleCallbackRejections(), 1);
	TestEqual(TEXT("Still cancelled"), P.UI->GetScreenState(Pending), EDocUIScreenState::Cancelled);
	TestEqual(TEXT("Never shown"), P.Presenter->Shown.Num(), ShownBefore);

	// Replace: failure leaves the old screen usable; success closes it after the load.
	P.Presenter->PendingLoads.Reset();
	FDocRequestHandle Replacement;
	TestTrue(TEXT("Replace staged"), P.UI->ReplaceScreen(Main, Req(TEXT("Options")), Replacement).IsSuccess());
	TestEqual(TEXT("Old stays active while loading"), P.UI->GetScreenState(Main), EDocUIScreenState::Active);
	P.Presenter->Complete(0, false);
	TestEqual(TEXT("Replacement failed"), P.UI->GetScreenState(Replacement), EDocUIScreenState::Failed);
	TestEqual(TEXT("Old screen usable"), P.UI->GetScreenState(Main), EDocUIScreenState::Active);
	P.Presenter->PendingLoads.Reset();
	P.UI->ReplaceScreen(Main, Req(TEXT("Options")), Replacement);
	P.Presenter->Complete(0, true);
	TestEqual(TEXT("Replacement active"), P.UI->GetScreenState(Replacement), EDocUIScreenState::Active);
	TestEqual(TEXT("Old closed after load"), P.UI->GetScreenState(Main), EDocUIScreenState::Closed);
	P.Presenter->bAsync = false;

	// Reentrant push from a presenter callback is queued until the mutation completes.
	FDocRequestHandle Reentrant;
	EDocUIScreenState StateInsideCallback = EDocUIScreenState::Closed;
	P.Presenter->OnShow = [&P, &Reentrant, &StateInsideCallback](const FDocUIScreenInfo& Info)
	{
		if (Info.ScreenId == FName(TEXT("Main")) && !Reentrant.IsSet())
		{
			P.UI->PushScreen(Req(TEXT("Toast")), Reentrant);
			StateInsideCallback = P.UI->GetScreenState(Reentrant);
		}
	};
	FDocRequestHandle Main2;
	P.UI->PushScreen(Req(TEXT("Main")), Main2);
	P.Presenter->OnShow = nullptr;
	TestEqual(TEXT("Queued inside the mutation"), StateInsideCallback, EDocUIScreenState::Requested);
	TestEqual(TEXT("Ran after the mutation"), P.UI->GetScreenState(Reentrant), EDocUIScreenState::Active);

	TestTrue(TEXT("Clear popups"), P.UI->ClearLayer(DocUITags::Layer_Popup).IsSuccess());
	TestTrue(TEXT("Popups closed"), P.UI->GetLayerScreens(DocUITags::Layer_Popup).IsEmpty());
	TestFalse(TEXT("Menu layer untouched"), P.UI->GetLayerScreens(DocUITags::Layer_Menu).IsEmpty());
	return true;
}

// UI-02
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUIModalBlockingTest, "Doc.UI.ModalBlocking", DocUITests::Flags)
bool FDocUIModalBlockingTest::RunTest(const FString& Parameters)
{
	FPauseAuthority Pause;
	FPlayer P;
	UDocUIScreenDefinition* PauseMenu = MakeScreen(TEXT("PauseMenu"), DocUITags::Layer_Menu, EDocUIDuplicatePolicy::Reject, EDocUIInputMode::UIOnly, TEXT("Resume"));
	PauseMenu->PausePolicy = EDocUIPausePolicy::GlobalPause;
	P.UI->RegisterScreenDefinition(PauseMenu);

	FDocRequestHandle Menu;
	P.UI->PushScreen(Req(TEXT("PauseMenu")), Menu);
	TestTrue(TEXT("World paused through the lease"), Pause.bPaused);
	TestTrue(TEXT("Menu owns focus"), P.IsFocusOwner(Menu));
	TestTrue(TEXT("Default focus"), P.Presenter->Focused.Num() > 0 && P.Presenter->Focused.Last() == FName(TEXT("Resume")));
	TestTrue(TEXT("Record focus"), P.UI->SetFocusTarget(Menu, TEXT("Options")).IsSuccess());

	int32 ResultsA = 0, ResultsB = 0;
	FDocDialogRequest Ask;
	Ask.Title = FText::FromString(TEXT("Quit?"));
	FDocRequestHandle DialogA, DialogB;
	P.UI->ShowDialog(Ask, nullptr, [&ResultsA](EDocDialogResult) { ++ResultsA; }, DialogA);
	TestTrue(TEXT("Dialog outranks the menu"), P.IsFocusOwner(DialogA) && !P.IsFocusOwner(Menu));
	P.UI->ShowDialog(Ask, nullptr, [&ResultsB](EDocDialogResult) { ++ResultsB; }, DialogB);
	TestEqual(TEXT("Lower modal suspended"), P.UI->GetScreenState(DialogA), EDocUIScreenState::Suspended);
	TestTrue(TEXT("Top modal owns focus"), P.IsFocusOwner(DialogB));

	P.UI->SubmitDialogInput(DialogB, EDocDialogInput::Confirm);
	TestEqual(TEXT("B resolved once"), ResultsB, 1);
	TestEqual(TEXT("A active again"), P.UI->GetScreenState(DialogA), EDocUIScreenState::Active);
	TestTrue(TEXT("Closing B did not release A or the menu"), P.IsFocusOwner(DialogA) && P.UI->GetControlClaimForTesting(Menu).IsSet());
	P.UI->SubmitDialogInput(DialogA, EDocDialogInput::Cancel);
	TestTrue(TEXT("Menu owns focus again"), P.IsFocusOwner(Menu));
	TestEqual(TEXT("Saved focus restored"), P.Presenter->Focused.Last(), FName(TEXT("Options")));
	TestTrue(TEXT("Still paused"), Pause.bPaused);

	// A saved target that no longer exists falls back inside the surviving screen.
	P.Presenter->Unfocusable.Add(TEXT("Options"));
	FDocRequestHandle DialogC;
	P.UI->ShowDialog(Ask, nullptr, nullptr, DialogC);
	P.UI->SubmitDialogInput(DialogC, EDocDialogInput::Back);
	TestEqual(TEXT("Fallback focus"), P.Presenter->Focused.Last(), FName(TEXT("Resume")));

	const FDocRequestHandle MenuClaim = P.UI->GetControlClaimForTesting(Menu);
	P.UI->PopScreen(Menu);
	TestFalse(TEXT("Menu claim released"), IDocPlayerControlProvider::Execute_IsDocControlClaimActive(P.Control.Get(), MenuClaim));
	TestFalse(TEXT("Unpaused with the last lease"), Pause.bPaused);
	TestEqual(TEXT("One pause, one unpause"), Pause.PauseCalls + Pause.UnpauseCalls, 2);
	return true;
}

// UI-03
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUIInputDeviceTest, "Doc.UI.InputDeviceSwitch", DocUITests::Flags)
bool FDocUIInputDeviceTest::RunTest(const FString& Parameters)
{
	FPlayer P1(false);
	FPlayer P2(false);
	TArray<EDocInputDeviceCategory> Switches;
	P1.Input->OnInputDeviceChangedNative.AddLambda([&Switches](EDocInputDeviceCategory, EDocInputDeviceCategory New) { Switches.Add(New); });
	auto Sample = [](const FKey& Key, float Value = 1.f, EDocInputDeviceCategory Hardware = EDocInputDeviceCategory::Unknown)
	{
		FDocInputSample S;
		S.Key = Key;
		S.AnalogValue = Value;
		S.VerifiedHardware = Hardware;
		return S;
	};
	TestEqual(TEXT("Starts on keyboard/mouse"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::KeyboardMouse);
	P1.Input->ReportInput(Sample(EKeys::Gamepad_LeftX, 0.1f));
	TestEqual(TEXT("Stick drift ignored"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::KeyboardMouse);
	P1.Input->ReportInput(Sample(EKeys::Gamepad_LeftX, 0.8f));
	TestEqual(TEXT("Unverified pad is generic"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::GenericGamepad);
	P1.Input->ReportInput(Sample(EKeys::Gamepad_FaceButton_Bottom, 1.f, EDocInputDeviceCategory::XboxController));
	TestEqual(TEXT("Verified metadata"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::XboxController);
	P1.Input->ReportInput(Sample(EKeys::Gamepad_FaceButton_Right));
	TestEqual(TEXT("No brand downgrade without metadata"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::XboxController);
	P1.Input->ReportInput(Sample(EKeys::MouseX, 1.f));
	TestEqual(TEXT("Insignificant mouse movement ignored"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::XboxController);
	P1.Input->ReportInput(Sample(FKey()));
	TestEqual(TEXT("Unknown key ignored"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::XboxController);
	P1.Input->ReportInput(Sample(EKeys::MouseX, 25.f));
	TestEqual(TEXT("Deliberate mouse"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::KeyboardMouse);
	P1.Input->NotifyDeviceDisconnected(EDocInputDeviceCategory::XboxController);
	TestEqual(TEXT("Disconnect of an inactive device changes nothing"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::KeyboardMouse);
	P1.Input->ReportInput(Sample(EKeys::Gamepad_FaceButton_Bottom, 1.f, EDocInputDeviceCategory::PlayStationController));
	P1.Input->NotifyDeviceDisconnected(EDocInputDeviceCategory::PlayStationController);
	TestEqual(TEXT("Disconnect recovery"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::KeyboardMouse);
	P1.Input->SetGamepadPresentationOverride(EDocInputDeviceCategory::PlayStationController);
	P1.Input->ReportInput(Sample(EKeys::Gamepad_FaceButton_Bottom));
	TestEqual(TEXT("Configured presentation override"), P1.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::PlayStationController);
	P1.Input->ReportInput(Sample(EKeys::SpaceBar));
	TestEqual(TEXT("Every switch was announced once"), Switches.Num(), 7);
	TestEqual(TEXT("Other player unaffected"), P2.Input->GetCurrentInputDevice(), EDocInputDeviceCategory::KeyboardMouse);
	P1.Input->OnInputDeviceChangedNative.Clear();
	return true;
}

// UI-04
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUINotificationQueueTest, "Doc.UI.NotificationQueue", DocUITests::Flags)
bool FDocUINotificationQueueTest::RunTest(const FString& Parameters)
{
	UDocUISettings* Settings = GetMutableDefault<UDocUISettings>();
	const int32 SavedVisible = Settings->MaxVisibleNotifications;
	const int32 SavedQueued = Settings->MaxQueuedNotifications;
	const float SavedAging = Settings->NotificationAgingSeconds;
	Settings->MaxVisibleNotifications = 2;
	Settings->MaxQueuedNotifications = 3;
	Settings->NotificationAgingSeconds = 10.f;
	{
		FPlayer P(false);
		UDocNotificationSubsystem* N = P.Notes.Get();
		auto Note = [](const TCHAR* Title, int32 Priority = 0, EDocNotificationPolicy Policy = EDocNotificationPolicy::Timed, float Duration = 5.f)
		{
			FDocNotificationRequest R;
			R.Title = FText::FromString(Title);
			R.Priority = Priority;
			R.Policy = Policy;
			R.Duration = Duration;
			return R;
		};
		FGuid A, B, C, D;
		N->Enqueue(Note(TEXT("A")), A);
		N->Enqueue(Note(TEXT("B")), B);
		N->Enqueue(Note(TEXT("C"), 0), C);
		N->Enqueue(Note(TEXT("D"), 5), D);
		TestEqual(TEXT("Visible bound"), N->GetVisible().Num(), 2);
		TestEqual(TEXT("Priority first in queue"), N->GetQueued()[0].Request.RequestId, D);
		FDocNotificationRequest Dup = Note(TEXT("A again"));
		Dup.RequestId = A;
		FGuid DupId;
		TestEqual(TEXT("Duplicate identity"), N->Enqueue(Dup, DupId).Outcome, EDocResultOutcome::NoChange);

		// Merge: presentation count only; the action runs once.
		int32 Grants = 0;
		class FGrant final : public IDocNotificationActionHandler
		{
		public:
			explicit FGrant(int32& InCount) : Count(InCount) {}
			virtual FDocSystemResult Execute(const FDocNotificationEntry&) override { ++Count; return FDocSystemResult::MakeSuccess(); }
			int32& Count;
		};
		N->RegisterActionHandler(TEXT("Claim"), MakeShared<FGrant>(Grants));
		FDocNotificationRequest Gold = Note(TEXT("+10 gold"), 9, EDocNotificationPolicy::Merge);
		Gold.MergeKey = TEXT("Gold");
		Gold.ActionHandlerKey = TEXT("Claim");
		FGuid Gold1, Gold2;
		N->Enqueue(Gold, Gold1);
		Gold.Title = FText::FromString(TEXT("+20 gold"));
		TestEqual(TEXT("Merged"), N->Enqueue(Gold, Gold2).Outcome, EDocResultOutcome::NoChange);
		TestEqual(TEXT("Merge returns the existing entry"), Gold2, Gold1);
		FDocNotificationEntry Merged;
		TestTrue(TEXT("Merge count"), N->FindEntry(Gold1, Merged) && Merged.MergeCount == 2);
		TestTrue(TEXT("Action"), N->InvokeAction(Gold1).IsSuccess());
		TestEqual(TEXT("Action once"), N->InvokeAction(Gold1).Outcome, EDocResultOutcome::NotFound);
		TestEqual(TEXT("Merged grants never repeat"), Grants, 1);

		// Replace by tag and owner.
		FDocNotificationRequest Quest = Note(TEXT("Objective 1"), 0, EDocNotificationPolicy::Replace);
		Quest.NotificationTag = TAG_QuestNote;
		FGuid Q1, Q2;
		N->Enqueue(Quest, Q1);
		Quest.Title = FText::FromString(TEXT("Objective 2"));
		N->Enqueue(Quest, Q2);
		FDocNotificationEntry Tmp;
		TestTrue(TEXT("Replaced"), !N->FindEntry(Q1, Tmp) && N->FindEntry(Q2, Tmp));

		// Overflow never drops persistent entries; the dropped count is reported.
		N->ClearAll();
		FGuid P1, P2, V1, V2, X;
		N->Enqueue(Note(TEXT("V1")), V1);
		N->Enqueue(Note(TEXT("V2")), V2);
		N->Enqueue(Note(TEXT("Persist1"), 0, EDocNotificationPolicy::Persistent), P1);
		N->Enqueue(Note(TEXT("Persist2"), 0, EDocNotificationPolicy::Persistent), P2);
		N->Enqueue(Note(TEXT("Low"), -1), X);
		FGuid Y;
		N->Enqueue(Note(TEXT("Normal"), 0), Y);
		TestEqual(TEXT("One dropped"), N->GetDroppedCount(), 1);
		TestTrue(TEXT("Lowest priority dropped"), !N->FindEntry(X, Tmp));
		TestTrue(TEXT("Persistent kept"), N->FindEntry(P1, Tmp) && N->FindEntry(P2, Tmp));

		// Priority interrupt: the displaced entry keeps its remaining time.
		FGuid Alarm;
		N->Enqueue(Note(TEXT("Alarm"), 50, EDocNotificationPolicy::PriorityInterrupt), Alarm);
		TestTrue(TEXT("Interrupt visible"), N->FindEntry(Alarm, Tmp) && Tmp.bVisible);
		TestEqual(TEXT("Still two visible"), N->GetVisible().Num(), 2);

		// Real-time expiry, TTL while hidden, and aging promotion.
		N->ClearAll();
		FGuid Short, Hidden1, Hidden2;
		N->Enqueue(Note(TEXT("Short"), 0, EDocNotificationPolicy::Timed, 1.f), Short);
		N->Enqueue(Note(TEXT("Busy"), 100, EDocNotificationPolicy::Persistent), Hidden1);
		FDocNotificationRequest Ttl = Note(TEXT("Ttl"));
		Ttl.ExpireSeconds = 2.f;
		N->Enqueue(Ttl, Hidden2);
		N->Enqueue(Note(TEXT("Busy2"), 100, EDocNotificationPolicy::Persistent), Y);
		TestTrue(TEXT("TTL entry queued"), N->FindEntry(Hidden2, Tmp) && !Tmp.bVisible);
		N->AdvanceClock(3.0);
		TestFalse(TEXT("Timed entry expired on real time"), N->FindEntry(Short, Tmp));
		TestFalse(TEXT("Expired while hidden"), N->FindEntry(Hidden2, Tmp));
	}
	Settings->MaxVisibleNotifications = SavedVisible;
	Settings->MaxQueuedNotifications = SavedQueued;
	Settings->NotificationAgingSeconds = SavedAging;
	return true;
}

// UI-05
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUISettingsApplyTest, "Doc.UI.SettingsApply", DocUITests::Flags)
bool FDocUISettingsApplyTest::RunTest(const FString& Parameters)
{
	FSettingsFixture F;
	FDocSettingDescriptor Volume = F.Setting(TEXT("Test.Volume"), FDocSettingValue::MakeFloat(0.5));
	Volume.Min = 0.0;
	Volume.Max = 1.0;
	FDocSettingDescriptor Mode = F.Setting(TEXT("Test.Mode"), FDocSettingValue::MakeEnum(TEXT("A")));
	Mode.Options = { TEXT("A"), TEXT("B") };
	FDocSettingDescriptor Sensitivity = F.Setting(TEXT("Test.Sensitivity"), FDocSettingValue::MakeFloat(1.0), EDocSettingScope::Profile, EDocSettingApplyPolicy::Immediate, TEXT("Preference"));
	Sensitivity.Min = 0.1;
	Sensitivity.Max = 10.0;
	FDocSettingDescriptor Subs = F.Setting(TEXT("Test.Subs"), FDocSettingValue::MakeBool(false));
	FDocSettingDescriptor SubSize = F.Setting(TEXT("Test.SubSize"), FDocSettingValue::MakeInt(1));
	SubSize.DependsOnSetting = TEXT("Test.Subs");
	SubSize.DependsOnValue = FDocSettingValue::MakeBool(true);
	FDocSettingDescriptor Upscaler = F.Setting(TEXT("Test.Upscaler"), FDocSettingValue::MakeBool(false), EDocSettingScope::Machine, EDocSettingApplyPolicy::Immediate, TEXT("Vendor"));
	for (const FDocSettingDescriptor& D : { Volume, Mode, Sensitivity, Subs, SubSize, Upscaler })
	{
		TestTrue(*FString::Printf(TEXT("Register %s"), *D.SettingId.ToString()), F.S->RegisterSetting(D).IsSuccess());
	}
	FDocSettingDescriptor BadDefault = Volume;
	BadDefault.SettingId = TEXT("Test.Bad");
	BadDefault.Default = FDocSettingValue::MakeFloat(3.0);
	TestEqual(TEXT("Invalid default rejected"), F.S->RegisterSetting(BadDefault).Outcome, EDocResultOutcome::InvalidConfiguration);

	FDocRequestHandle Edit;
	F.S->BeginEdit(nullptr, Profile(1), Edit);
	TestEqual(TEXT("Wrong type"), F.S->SetPending(Edit, TEXT("Test.Volume"), FDocSettingValue::MakeBool(true)).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Out of range"), F.S->SetPending(Edit, TEXT("Test.Volume"), FDocSettingValue::MakeFloat(1.5)).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Non-finite"), F.S->SetPending(Edit, TEXT("Test.Volume"), FDocSettingValue::MakeFloat(std::numeric_limits<double>::quiet_NaN())).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Unknown option"), F.S->SetPending(Edit, TEXT("Test.Mode"), FDocSettingValue::MakeEnum(TEXT("C"))).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Missing capability"), F.S->SetPending(Edit, TEXT("Test.Upscaler"), FDocSettingValue::MakeBool(true)).Outcome, EDocResultOutcome::Unavailable);
	FDocSettingState UpscalerState;
	F.S->GetSettingState(TEXT("Test.Upscaler"), FDocOwnerScope(), UpscalerState);
	TestTrue(TEXT("Unavailable with a reason"), !UpscalerState.bAvailable && !UpscalerState.UnavailableReason.IsEmpty());
	TestEqual(TEXT("Disabled by dependency"), F.S->SetPending(Edit, TEXT("Test.SubSize"), FDocSettingValue::MakeInt(2)).Outcome, EDocResultOutcome::Unsupported);
	F.S->SetPending(Edit, TEXT("Test.Subs"), FDocSettingValue::MakeBool(true));
	TestTrue(TEXT("Dependency satisfied in the same edit"), F.S->SetPending(Edit, TEXT("Test.SubSize"), FDocSettingValue::MakeInt(2)).IsSuccess());
	F.S->SetPending(Edit, TEXT("Test.Volume"), FDocSettingValue::MakeFloat(0.8));
	F.S->SetPending(Edit, TEXT("Test.Sensitivity"), FDocSettingValue::MakeFloat(3.0));
	TestTrue(TEXT("Apply"), F.S->ApplyEdit(Edit).IsSuccess());
	TestTrue(TEXT("Consumer received it"), F.Live.Contains(TEXT("Test.Volume")) && F.Live[TEXT("Test.Volume")] == FDocSettingValue::MakeFloat(0.8));
	TestTrue(TEXT("Confirmed"), F.Value(TEXT("Test.Volume")) == FDocSettingValue::MakeFloat(0.8));
	TestTrue(TEXT("Persisted"), F.Store->Data.Values.Contains(TEXT("M:Test.Volume")));
	TestTrue(TEXT("Profile value is per profile"), F.Value(TEXT("Test.Sensitivity"), Profile(1)) == FDocSettingValue::MakeFloat(3.0)
		&& F.Value(TEXT("Test.Sensitivity"), Profile(2)) == FDocSettingValue::MakeFloat(1.0));

	// Actual consumer effect: a value the consumer does not report back is not applied.
	FDocRequestHandle Clamped;
	F.S->BeginEdit(nullptr, Profile(1), Clamped);
	F.S->SetPending(Clamped, TEXT("Test.Volume"), FDocSettingValue::MakeFloat(1.0));
	F.Reported.Add(TEXT("Test.Volume"), FDocSettingValue::MakeFloat(0.9));
	const FDocSystemResult NotApplied = F.S->ApplyEdit(Clamped);
	TestEqual(TEXT("Consumer disagreed"), NotApplied.ErrorTag, DocUITags::Error_UI_SettingNotApplied.GetTag());
	TestTrue(TEXT("Confirmed value unchanged"), F.Value(TEXT("Test.Volume")) == FDocSettingValue::MakeFloat(0.8));
	F.Reported.Reset();

	// Two local players editing machine settings: same field conflicts, independent fields merge.
	FDocRequestHandle A, B, C;
	F.S->BeginEdit(nullptr, Profile(1), A);
	F.S->BeginEdit(nullptr, Profile(2), B);
	F.S->BeginEdit(nullptr, Profile(2), C);
	F.S->SetPending(A, TEXT("Test.Volume"), FDocSettingValue::MakeFloat(0.2));
	F.S->SetPending(B, TEXT("Test.Volume"), FDocSettingValue::MakeFloat(0.4));
	F.S->SetPending(C, TEXT("Test.Mode"), FDocSettingValue::MakeEnum(TEXT("B")));
	TestTrue(TEXT("First writer"), F.S->ApplyEdit(A).IsSuccess());
	TestEqual(TEXT("Second writer conflicts"), F.S->ApplyEdit(B).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Independent field merges"), F.S->ApplyEdit(C).IsSuccess());
	TestTrue(TEXT("Winner kept"), F.Value(TEXT("Test.Volume")) == FDocSettingValue::MakeFloat(0.2));

	// Batch rollback: a failing second field undoes the first.
	FDocRequestHandle Batch;
	F.S->BeginEdit(nullptr, Profile(1), Batch);
	F.S->SetPending(Batch, TEXT("Test.Volume"), FDocSettingValue::MakeFloat(0.6));
	F.S->SetPending(Batch, TEXT("Test.Mode"), FDocSettingValue::MakeEnum(TEXT("A")));
	F.FailApply.Add(TEXT("Test.Mode"));
	TestFalse(TEXT("Batch fails"), F.S->ApplyEdit(Batch).IsSuccess());
	F.FailApply.Reset();
	TestTrue(TEXT("Nothing committed"), F.Value(TEXT("Test.Volume")) == FDocSettingValue::MakeFloat(0.2));

	// Consumer acknowledgement is separate from the stored value.
	FDocSettingState State;
	F.S->GetSettingState(TEXT("Test.Sensitivity"), Profile(1), State);
	TestFalse(TEXT("Not yet acknowledged"), State.bConsumerAcknowledged);
	F.S->AcknowledgeSetting(TEXT("Test.Sensitivity"), Profile(1), FDocSettingValue::MakeFloat(3.0));
	F.S->GetSettingState(TEXT("Test.Sensitivity"), Profile(1), State);
	TestTrue(TEXT("Acknowledged"), State.bConsumerAcknowledged);
	return true;
}

// UI-06
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUISettingsRevertTest, "Doc.UI.SettingsRevert", DocUITests::Flags)
bool FDocUISettingsRevertTest::RunTest(const FString& Parameters)
{
	FSettingsFixture F;
	const FDocSettingDescriptor Resolution = F.Setting(TEXT("Test.Resolution"), FDocSettingValue::MakeString(TEXT("1280x720")),
		EDocSettingScope::Machine, EDocSettingApplyPolicy::PreviewConfirm);
	F.S->RegisterSetting(Resolution);
	F.Live.Add(TEXT("Test.Resolution"), FDocSettingValue::MakeString(TEXT("1280x720")));
	const double Deadline = GetDefault<UDocUISettings>()->PreviewConfirmSeconds;
	bool bMarkerBeforePreview = false;
	TFunction<FDocSystemResult(const FDocSettingDescriptor&, const FDocSettingValue&, bool)> BaseApply = F.Provider->ApplyFn;
	F.Provider->ApplyFn = [&F, &bMarkerBeforePreview, BaseApply](const FDocSettingDescriptor& D, const FDocSettingValue& V, bool bPreview)
	{
		if (bPreview) { bMarkerBeforePreview = F.Store->Data.PendingPreview.Contains(TEXT("M:Test.Resolution")); }
		return BaseApply(D, V, bPreview);
	};

	auto Preview = [&F](const TCHAR* Value, UObject* Owner = nullptr)
	{
		FDocRequestHandle Edit;
		F.S->BeginEdit(Owner, Profile(1), Edit);
		F.S->SetPending(Edit, TEXT("Test.Resolution"), FDocSettingValue::MakeString(Value));
		F.S->ApplyEdit(Edit);
		return Edit;
	};
	auto State = [&F](const FDocRequestHandle& Edit)
	{
		FDocSettingsEditView View;
		F.S->GetEditView(Edit, View);
		return View.State;
	};

	// Timeout on the real-time clock (keeps running while the world is paused).
	FDocRequestHandle E1 = Preview(TEXT("1920x1080"));
	TestEqual(TEXT("Previewing"), State(E1), EDocSettingsEditState::Previewing);
	TestTrue(TEXT("Recovery marker written before the preview"), bMarkerBeforePreview);
	F.S->AdvanceClock(-5.0);
	TestEqual(TEXT("Backward step ignored"), State(E1), EDocSettingsEditState::Previewing);
	F.S->AdvanceClock(Deadline + 1.0);
	TestEqual(TEXT("Timed out"), State(E1), EDocSettingsEditState::Reverted);
	TestTrue(TEXT("Display reverted"), F.Live[TEXT("Test.Resolution")] == FDocSettingValue::MakeString(TEXT("1280x720")));
	TestTrue(TEXT("Confirmed unchanged"), F.Value(TEXT("Test.Resolution")) == FDocSettingValue::MakeString(TEXT("1280x720")));
	TestTrue(TEXT("Marker cleared"), F.Store->Data.PendingPreview.IsEmpty());

	// Losing the editing screen reverts.
	UObject* Screen = NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage());
	FDocRequestHandle E2 = Preview(TEXT("2560x1440"), Screen);
	Screen->MarkAsGarbage();
	F.S->AdvanceClock(0.1);
	TestEqual(TEXT("Owner lost"), State(E2), EDocSettingsEditState::Reverted);

	// Keep: confirm persists and calls the provider's confirmation.
	FDocRequestHandle E3 = Preview(TEXT("1920x1080"));
	TestTrue(TEXT("Confirm"), F.S->ConfirmEdit(E3).IsSuccess());
	TestEqual(TEXT("Provider confirmed"), F.Confirms, 1);
	TestTrue(TEXT("Confirmed value"), F.Value(TEXT("Test.Resolution")) == FDocSettingValue::MakeString(TEXT("1920x1080")));
	TestTrue(TEXT("Stored"), F.Store->Data.Values.FindRef(TEXT("M:Test.Resolution")) == TEXT("1920x1080"));

	// The monitor refused the mode: reported state differs, so the preview is reverted at once.
	F.Reported.Add(TEXT("Test.Resolution"), FDocSettingValue::MakeString(TEXT("1920x1080")));
	FDocRequestHandle E4 = Preview(TEXT("3840x2160"));
	TestEqual(TEXT("Failed mode apply reverted"), State(E4), EDocSettingsEditState::Reverted);
	F.Reported.Reset();

	// A revert that fails is a visible recovery state, never success.
	FDocRequestHandle E5 = Preview(TEXT("800x600"));
	F.RefuseValue = [](FName, const FDocSettingValue& V) { return V.StringValue == TEXT("1920x1080"); };
	const FDocSystemResult Revert = F.S->RevertEdit(E5);
	TestEqual(TEXT("Revert failure reported"), Revert.ErrorTag, DocUITags::Error_UI_RevertFailed.GetTag());
	TestEqual(TEXT("Recovery required"), State(E5), EDocSettingsEditState::RecoveryRequired);
	TestTrue(TEXT("Marker kept for next launch"), F.Store->Data.PendingPreview.Contains(TEXT("M:Test.Resolution")));
	F.RefuseValue = nullptr;

	// Restart recovery: a new authority with the same store restores the confirmed mode.
	F.Store->Data.Values.Add(TEXT("M:Unknown.Future"), TEXT("keep-me"));
	TStrongObjectPtr<UDocSettingsSubsystem> Next(NewObject<UDocSettingsSubsystem>(F.GameInstance.Get()));
	Next->SetStore(F.Store);
	Next->RegisterProvider(TEXT("Test"), F.Provider);
	Next->RegisterSetting(Resolution);
	F.Live.Add(TEXT("Test.Resolution"), FDocSettingValue::MakeString(TEXT("800x600"))); // the display the crash left behind
	TestTrue(TEXT("Recovered"), Next->RecoverOnStartup().IsSuccess());
	TestTrue(TEXT("Safe mode applied"), F.Live[TEXT("Test.Resolution")] == FDocSettingValue::MakeString(TEXT("1920x1080")));
	TestTrue(TEXT("Marker cleared after recovery"), F.Store->Data.PendingPreview.IsEmpty());
	TestTrue(TEXT("Unsupported preference preserved"), F.Store->Data.Values.FindRef(TEXT("M:Unknown.Future")) == TEXT("keep-me"));
	return true;
}

// UI-07
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUIGlyphRemapTest, "Doc.UI.GlyphAndRemap", DocUITests::Flags)
bool FDocUIGlyphRemapTest::RunTest(const FString& Parameters)
{
	FPlayer P(false);
	UDocInputPresentationSubsystem* In = P.Input.Get();
	class FGlyphs final : public IDocGlyphProvider
	{
	public:
		virtual FName GetGlyph(const FKey& Key, EDocInputDeviceCategory) const override
		{
			return Key == EKeys::LeftShift ? NAME_None : FName(*FString::Printf(TEXT("Glyph_%s"), *Key.ToString()));
		}
	};
	In->SetGlyphProvider(MakeShared<FGlyphs>());
	auto Binding = [](std::initializer_list<FKey> Keys, bool bHold = false)
	{
		FDocInputBinding B;
		B.Keys = Keys;
		B.bHold = bHold;
		return B;
	};
	auto Action = [](FName Id, TArray<FDocInputBinding> Kbm, TArray<FDocInputBinding> Pad, bool bReserved = false)
	{
		FDocActionBindings A;
		A.ActionId = Id;
		A.KeyboardMouse = Kbm;
		A.Gamepad = Pad;
		A.bReservedNavigation = bReserved;
		return A;
	};
	TestEqual(TEXT("Reserved needs both scopes"), In->RegisterAction(Action(TEXT("Broken"), { Binding({ EKeys::Enter }) }, {}, true)).Outcome, EDocResultOutcome::InvalidConfiguration);
	In->RegisterAction(Action(TEXT("Confirm"), { Binding({ EKeys::Enter }) }, { Binding({ EKeys::Gamepad_FaceButton_Bottom }) }, true));
	In->RegisterAction(Action(TEXT("Back"), { Binding({ EKeys::Escape }) }, { Binding({ EKeys::Gamepad_FaceButton_Right }) }, true));
	In->RegisterAction(Action(TEXT("Jump"), { Binding({ EKeys::SpaceBar }) }, { Binding({ EKeys::Gamepad_FaceButton_Top }) }));
	In->RegisterAction(Action(TEXT("Sprint"), { Binding({ EKeys::LeftShift }, true), Binding({ EKeys::LeftControl, EKeys::W }) }, {}));

	FDocActionPresentation Jump = In->GetActionPresentation(TEXT("Jump"));
	TestTrue(TEXT("Default glyph"), !Jump.bUnbound && Jump.Alternates[0].Glyphs[0] == FName(TEXT("Glyph_SpaceBar")));
	FDocActionPresentation Sprint = In->GetActionPresentation(TEXT("Sprint"));
	TestTrue(TEXT("Hold and missing art fall back to text"), Sprint.Alternates.Num() == 2 && Sprint.Alternates[0].bHold && Sprint.Alternates[0].bMissingGlyph
		&& !Sprint.Alternates[0].Labels[0].IsEmpty());
	TestTrue(TEXT("Chord"), Sprint.Alternates[1].bChord && Sprint.Alternates[1].Keys.Num() == 2);
	TestTrue(TEXT("Gamepad presentation uses the gamepad scope"), In->GetActionPresentation(TEXT("Sprint"), EDocInputDeviceCategory::GenericGamepad).bUnbound);

	TArray<FDocRemapConflict> Conflicts;
	TestEqual(TEXT("Remap needs a transaction"), In->RemapAction(TEXT("Jump"), false, 0, Binding({ EKeys::J }), false, Conflicts).Outcome, EDocResultOutcome::NotReady);
	In->BeginRemap();
	TestTrue(TEXT("Remap Jump"), In->RemapAction(TEXT("Jump"), false, 0, Binding({ EKeys::J }), false, Conflicts).IsSuccess());
	TestEqual(TEXT("Pending edits are not presented yet"), In->GetActionPresentation(TEXT("Jump")).Alternates[0].Keys[0], EKeys::SpaceBar);
	TestEqual(TEXT("Wrong scope"), In->RemapAction(TEXT("Jump"), true, 0, Binding({ EKeys::K }), false, Conflicts).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Conflict declared"), In->RemapAction(TEXT("Sprint"), false, 2, Binding({ EKeys::J }), false, Conflicts).Outcome, EDocResultOutcome::Conflict);
	TestTrue(TEXT("Conflict names the action"), Conflicts.Num() == 1 && Conflicts[0].ActionId == FName(TEXT("Jump")));
	TestEqual(TEXT("Navigation keys are reserved"), In->RemapAction(TEXT("Jump"), false, 1, Binding({ EKeys::Escape }), true, Conflicts).Outcome, EDocResultOutcome::Conflict);
	TestEqual(TEXT("Confirm keeps a working binding"), In->RemapAction(TEXT("Confirm"), false, 0, FDocInputBinding(), false, Conflicts).Outcome, EDocResultOutcome::Conflict);
	const int64 Before = In->GetRevision();
	TestTrue(TEXT("Apply"), In->ApplyRemap().IsSuccess());
	TestTrue(TEXT("Rebinding invalidates cached glyphs"), In->GetRevision() > Before);
	Jump = In->GetActionPresentation(TEXT("Jump"));
	TestTrue(TEXT("Effective binding presented, not the default"), Jump.Alternates.Num() == 1 && Jump.Alternates[0].Keys[0] == EKeys::J
		&& Jump.Alternates[0].Glyphs[0] == FName(TEXT("Glyph_J")));

	In->BeginRemap();
	In->RemapAction(TEXT("Sprint"), false, 2, Binding({ EKeys::J }), true, Conflicts);
	In->CancelRemap();
	TestEqual(TEXT("Cancel keeps the effective mapping"), In->GetActionPresentation(TEXT("Jump")).Alternates[0].Keys[0], EKeys::J);

	FPlayer Fresh(false);
	Fresh.Input->RegisterAction(Action(TEXT("Confirm"), { Binding({ EKeys::Enter }) }, { Binding({ EKeys::Gamepad_FaceButton_Bottom }) }, true));
	Fresh.Input->RegisterAction(Action(TEXT("Back"), { Binding({ EKeys::Escape }) }, { Binding({ EKeys::Gamepad_FaceButton_Right }) }, true));
	Fresh.Input->RegisterAction(Action(TEXT("Jump"), { Binding({ EKeys::SpaceBar }) }, { Binding({ EKeys::Gamepad_FaceButton_Top }) }));
	const FDocInputRemapSaveData Saved = In->CaptureRemaps();
	TestEqual(TEXT("Only changed actions saved"), Saved.Overrides.Num(), 1);
	TestTrue(TEXT("Restore"), Fresh.Input->RestoreRemaps(Saved).IsSuccess());
	TestEqual(TEXT("Restored remap presented"), Fresh.Input->GetActionPresentation(TEXT("Jump")).Alternates[0].Keys[0], EKeys::J);

	FDocInputRemapSaveData NoEscape;
	NoEscape.Overrides.Add(Action(TEXT("Back"), {}, { Binding({ EKeys::Gamepad_FaceButton_Right }) }, true));
	TestEqual(TEXT("A save without a back path is refused"), Fresh.Input->RestoreRemaps(NoEscape).Outcome, EDocResultOutcome::Conflict);
	return true;
}

// UI-08
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUIDialogOnceTest, "Doc.UI.DialogExactlyOnce", DocUITests::Flags)
bool FDocUIDialogOnceTest::RunTest(const FString& Parameters)
{
	FPlayer P;
	TArray<EDocDialogResult> Results;
	auto Record = [&Results](EDocDialogResult R) { Results.Add(R); };
	FDocDialogRequest Plain;
	Plain.Title = FText::FromString(TEXT("Save?"));
	FDocDialogRequest Destructive = Plain;
	Destructive.bDestructive = true;
	Destructive.TimeoutSeconds = 5.f;
	Destructive.TimeoutResult = EDocDialogResult::Confirmed; // must never apply to a destructive prompt

	FDocRequestHandle D1;
	P.UI->ShowDialog(Plain, nullptr, Record, D1);
	TestTrue(TEXT("Confirm"), P.UI->SubmitDialogInput(D1, EDocDialogInput::Confirm).IsSuccess());
	TestEqual(TEXT("Double confirm"), P.UI->SubmitDialogInput(D1, EDocDialogInput::Confirm).Outcome, EDocResultOutcome::NoChange);
	TestEqual(TEXT("Back after resolve"), P.UI->SubmitDialogInput(D1, EDocDialogInput::Back).Outcome, EDocResultOutcome::NoChange);
	TestTrue(TEXT("One confirmed result"), Results.Num() == 1 && Results[0] == EDocDialogResult::Confirmed);

	Results.Reset();
	FDocRequestHandle D2;
	P.UI->ShowDialog(Destructive, nullptr, Record, D2);
	FDocUIScreenInfo Info;
	P.UI->GetScreenInfo(D2, Info);
	TestEqual(TEXT("No affirmative default focus"), Info.FocusTarget, FName(TEXT("Cancel")));
	P.UI->AdvanceClock(6.0);
	TestTrue(TEXT("Destructive timeout cancels"), Results.Num() == 1 && Results[0] == EDocDialogResult::Cancelled);

	Results.Reset();
	FDocRequestHandle D3;
	FDocDialogRequest Three = Plain;
	P.UI->ShowDialog(Three, nullptr, Record, D3);
	TestEqual(TEXT("No third action"), P.UI->SubmitDialogInput(D3, EDocDialogInput::ThirdAction).Outcome, EDocResultOutcome::InvalidInput);
	P.UI->PopScreen(D3);
	Three.ThirdActionText = FText::FromString(TEXT("Don't save"));
	FDocRequestHandle D4;
	P.UI->ShowDialog(Three, nullptr, Record, D4);
	P.UI->SubmitDialogInput(D4, EDocDialogInput::ThirdAction);
	TestTrue(TEXT("Closed by pop, then third action"), Results.Num() == 2 && Results[0] == EDocDialogResult::Cancelled && Results[1] == EDocDialogResult::ThirdAction);

	// A callback that answers again never produces a second terminal result.
	Results.Reset();
	FDocRequestHandle D5;
	P.UI->ShowDialog(Plain, nullptr, [&Results, &P, &D5](EDocDialogResult R)
	{
		Results.Add(R);
		P.UI->SubmitDialogInput(D5, EDocDialogInput::Confirm);
	}, D5);
	P.UI->HandleBack();
	TestTrue(TEXT("Back resolves once"), Results.Num() == 1 && Results[0] == EDocDialogResult::Cancelled);

	// Not yet visible: confirm is refused, cancel is allowed.
	Results.Reset();
	P.Presenter->bAsync = true;
	FDocRequestHandle D6;
	P.UI->ShowDialog(Plain, nullptr, Record, D6);
	TestEqual(TEXT("Confirm before visible"), P.UI->SubmitDialogInput(D6, EDocDialogInput::Confirm).Outcome, EDocResultOutcome::NotReady);
	P.UI->SubmitDialogInput(D6, EDocDialogInput::Cancel);
	P.Presenter->Complete(0, true);
	TestTrue(TEXT("Cancelled once; late load ignored"), Results.Num() == 1 && Results[0] == EDocDialogResult::Cancelled);
	P.Presenter->bAsync = false;

	// Parent teardown and player removal.
	Results.Reset();
	UObject* Parent = NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage());
	FDocRequestHandle D7, D8;
	P.UI->ShowDialog(Plain, Parent, Record, D7);
	Parent->MarkAsGarbage();
	P.UI->AdvanceClock(0.1);
	TestTrue(TEXT("Owner destroyed"), Results.Num() == 1 && Results[0] == EDocDialogResult::OwnerDestroyed);
	P.UI->ShowDialog(Plain, nullptr, Record, D8);
	P.UI->ShutdownForTesting();
	TestTrue(TEXT("Player removal resolves once"), Results.Num() == 2 && Results[1] == EDocDialogResult::OwnerDestroyed);
	P.UI->ShutdownForTesting();
	TestEqual(TEXT("Second teardown adds nothing"), Results.Num(), 2);
	return true;
}

// UI-09
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUILoadingLeasesTest, "Doc.UI.LoadingLeases", DocUITests::Flags)
bool FDocUILoadingLeasesTest::RunTest(const FString& Parameters)
{
	FPlayer P(false);
	UDocUIManagerSubsystem* UI = P.UI.Get();
	UObject* Streamer = NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage());
	TStrongObjectPtr<UObject> SaverKeep(NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage()));
	FDocRequestHandle Stream, Save;
	UI->AcquireLoading(Streamer, FText::FromString(TEXT("Streaming")), Stream);
	UI->AcquireLoading(SaverKeep.Get(), FText::FromString(TEXT("Saving")), Save);
	FDocLoadingView View = UI->GetLoadingView();
	TestTrue(TEXT("Visible, unknown progress"), View.bVisible && View.bIndeterminate && View.ActiveLeases == 2);
	TestEqual(TEXT("NaN progress"), UI->SetLoadingLeaseProgress(Stream, std::numeric_limits<float>::quiet_NaN()).Outcome, EDocResultOutcome::InvalidInput);
	TestEqual(TEXT("Out of range progress"), UI->SetLoadingLeaseProgress(Stream, 1.5f).Outcome, EDocResultOutcome::InvalidInput);
	UI->SetLoadingLeaseProgress(Stream, 0.5f);
	TestTrue(TEXT("Still indeterminate while one lease has no denominator"), UI->GetLoadingView().bIndeterminate);
	UI->SetLoadingLeaseProgress(Save, 0.2f);
	View = UI->GetLoadingView();
	TestTrue(TEXT("Conservative progress"), !View.bIndeterminate && FMath::IsNearlyEqual(View.Progress, 0.2f));

	// Legacy Show/Hide only touches the legacy lease.
	UI->ShowLoading(FText::FromString(TEXT("Legacy")));
	UI->HideLoading();
	TestEqual(TEXT("No premature hide"), UI->GetLoadingView().ActiveLeases, 2);

	UI->ReleaseLoading(Stream, EDocLoadingOutcome::Completed, FText::GetEmpty());
	TestTrue(TEXT("Other operation keeps it visible"), UI->GetLoadingView().bVisible);
	TestEqual(TEXT("Double release"), UI->ReleaseLoading(Stream, EDocLoadingOutcome::Completed, FText::GetEmpty()).Outcome, EDocResultOutcome::NoChange);
	UI->ReleaseLoading(Save, EDocLoadingOutcome::Failed, FText::FromString(TEXT("Disk full")));
	View = UI->GetLoadingView();
	TestTrue(TEXT("Failure is visible and distinct"), View.bVisible && View.bHasError && View.ErrorOutcome == EDocLoadingOutcome::Failed && View.ActiveLeases == 0);
	UI->DismissLoadingError();
	TestFalse(TEXT("Dismissed"), UI->GetLoadingView().bVisible);

	// Travel: leases of destroyed old-world owners end; others stay.
	UObject* OldWorldOwner = NewObject<UDocReferencePlayerControlProvider>(GetTransientPackage());
	FDocRequestHandle OldLease, Kept;
	UI->AcquireLoading(OldWorldOwner, FText::GetEmpty(), OldLease);
	UI->AcquireLoading(SaverKeep.Get(), FText::GetEmpty(), Kept);
	OldWorldOwner->MarkAsGarbage();
	UI->NotifyTravel();
	TestEqual(TEXT("Only the live owner's lease remains"), UI->GetLoadingView().ActiveLeases, 1);
	return true;
}

// UI-10 (base part: navigation/focus, accessibility preferences with consumer acknowledgement, culture)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUIAccessibilityCultureTest, "Doc.UI.AccessibilityAndCulture", DocUITests::Flags)
bool FDocUIAccessibilityCultureTest::RunTest(const FString& Parameters)
{
	FPlayer P;
	P.UI->RegisterScreenDefinition(MakeScreen(TEXT("Settings"), DocUITags::Layer_Menu, EDocUIDuplicatePolicy::Reject, EDocUIInputMode::UIOnly, TEXT("Video")));
	P.UI->RegisterScreenDefinition(MakeScreen(TEXT("Audio"), DocUITags::Layer_Menu, EDocUIDuplicatePolicy::Reject, EDocUIInputMode::UIOnly, TEXT("Master")));
	FDocRequestHandle SettingsScreen, AudioScreen;
	P.UI->PushScreen(Req(TEXT("Settings")), SettingsScreen);
	P.UI->SetFocusTarget(SettingsScreen, TEXT("Accessibility"));
	TestEqual(TEXT("Unfocusable target refused"), P.UI->SetFocusTarget(SettingsScreen, NAME_None).Outcome, EDocResultOutcome::InvalidInput);
	P.UI->PushScreen(Req(TEXT("Audio")), AudioScreen);
	TestTrue(TEXT("Keyboard/gamepad back pops"), P.UI->HandleBack().IsSuccess());
	TestEqual(TEXT("Focus restored after back"), P.Presenter->Focused.Last(), FName(TEXT("Accessibility")));
	TestEqual(TEXT("Nothing left to go back from the root game"), P.UI->GetScreenState(AudioScreen), EDocUIScreenState::Closed);

	FSettingsFixture F;
	F.S->RegisterStandardSettings(false);
	FDocRequestHandle Edit;
	F.S->BeginEdit(nullptr, Profile(1), Edit);
	TestTrue(TEXT("Text scale"), F.S->SetPending(Edit, TEXT("Accessibility.TextScale"), FDocSettingValue::MakeFloat(1.5)).IsSuccess());
	TestTrue(TEXT("Reduce motion"), F.S->SetPending(Edit, TEXT("Accessibility.ReduceMotion"), FDocSettingValue::MakeBool(true)).IsSuccess());
	TestEqual(TEXT("Text scale bounds"), F.S->SetPending(Edit, TEXT("Accessibility.TextScale"), FDocSettingValue::MakeFloat(5.0)).Outcome, EDocResultOutcome::InvalidInput);
	TestTrue(TEXT("Apply"), F.S->ApplyEdit(Edit).IsSuccess());
	FDocSettingState Motion;
	F.S->GetSettingState(TEXT("Accessibility.ReduceMotion"), Profile(1), Motion);
	TestTrue(TEXT("Stored but not proven applied"), Motion.Confirmed.BoolValue && !Motion.bConsumerAcknowledged);
	F.S->AcknowledgeSetting(TEXT("Accessibility.ReduceMotion"), Profile(1), FDocSettingValue::MakeBool(false));
	F.S->GetSettingState(TEXT("Accessibility.ReduceMotion"), Profile(1), Motion);
	TestFalse(TEXT("Stale acknowledgement does not count"), Motion.bConsumerAcknowledged);
	F.S->AcknowledgeSetting(TEXT("Accessibility.ReduceMotion"), Profile(1), FDocSettingValue::MakeBool(true));
	F.S->GetSettingState(TEXT("Accessibility.ReduceMotion"), Profile(1), Motion);
	TestTrue(TEXT("Consumer acknowledged"), Motion.bConsumerAcknowledged);
	TestTrue(TEXT("Other profile keeps defaults"), F.Value(TEXT("Accessibility.TextScale"), Profile(2)) == FDocSettingValue::MakeFloat(1.0));

	FDocSettingState Upscaling;
	F.S->GetSettingState(TEXT("Video.UpscalingMode"), FDocOwnerScope(), Upscaling);
	TestFalse(TEXT("Capability-provided option unavailable without a provider"), Upscaling.bAvailable);

	// Culture: an unknown culture is refused; a valid one applies and reports back.
	const FString Original = FInternationalization::Get().GetCurrentCulture()->GetName();
	FDocRequestHandle Lang;
	F.S->BeginEdit(nullptr, Profile(1), Lang);
	F.S->SetPending(Lang, TEXT("Language.Culture"), FDocSettingValue::MakeString(TEXT("zz-NOT-A-CULTURE")));
	TestFalse(TEXT("Unknown culture refused"), F.S->ApplyEdit(Lang).IsSuccess());
	TestTrue(TEXT("Culture unchanged"), FInternationalization::Get().GetCurrentCulture()->GetName() == Original);
	FDocRequestHandle Lang2;
	F.S->BeginEdit(nullptr, Profile(1), Lang2);
	F.S->SetPending(Lang2, TEXT("Language.Culture"), FDocSettingValue::MakeString(TEXT("en")));
	TestTrue(TEXT("Valid culture applies"), F.S->ApplyEdit(Lang2).IsSuccess());
	FInternationalization::Get().SetCurrentCulture(Original);
	return true;
}

// UI-11 (base part)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocUISplitScreenTest, "Doc.UI.SplitScreenAndControlInterop", DocUITests::Flags)
bool FDocUISplitScreenTest::RunTest(const FString& Parameters)
{
	FPauseAuthority Pause;
	FPlayer P1;
	FPlayer P2;
	for (FPlayer* P : { &P1, &P2 })
	{
		UDocUIScreenDefinition* Menu = MakeScreen(TEXT("PauseMenu"), DocUITags::Layer_Menu);
		Menu->PausePolicy = EDocUIPausePolicy::GlobalPause;
		P->UI->RegisterScreenDefinition(Menu);
		P->UI->RegisterScreenDefinition(MakeScreen(TEXT("Toast"), DocUITags::Layer_Popup, EDocUIDuplicatePolicy::AllowDistinctInstances, EDocUIInputMode::Game));
	}
	FDocRequestHandle M1, M2;
	P1.UI->PushScreen(Req(TEXT("PauseMenu")), M1);
	P2.UI->PushScreen(Req(TEXT("PauseMenu")), M2);
	TestTrue(TEXT("Separate roots"), P1.UI->IsScreenActive(TEXT("PauseMenu")) && P2.UI->IsScreenActive(TEXT("PauseMenu")));
	TestEqual(TEXT("One shared pause"), Pause.PauseCalls, 1);
	P1.UI->PopScreen(M1);
	TestTrue(TEXT("Player 1 closing does not unpause player 2"), Pause.bPaused);
	P2.UI->PopScreen(M2);
	TestFalse(TEXT("Last lease unpauses"), Pause.bPaused);

	FDocRequestHandle T2;
	P2.UI->PushScreen(Req(TEXT("Toast")), T2);
	P1.UI->ClearLayer(DocUITags::Layer_Popup);
	TestEqual(TEXT("Clearing player 1 never touches player 2"), P2.UI->GetScreenState(T2), EDocUIScreenState::Active);

	FGuid Note1;
	FDocNotificationRequest N;
	N.Title = FText::FromString(TEXT("Hello"));
	P1.Notes->Enqueue(N, Note1);
	TestTrue(TEXT("Separate notification queues"), P1.Notes->GetVisible().Num() == 1 && P2.Notes->GetVisible().IsEmpty());

	// Coexistence with a higher-priority sequence/inspection claim on the same provider.
	FDocControlClaimRequest Sequence;
	Sequence.LocalPlayer = P1.Player.Get();
	Sequence.Owner = P1.Control.Get();
	Sequence.Capabilities.AddTag(DocCoreTags::Control_Input);
	Sequence.Capabilities.AddTag(DocCoreTags::Control_Focus);
	Sequence.Priority = 500;
	FDocRequestHandle SequenceClaim;
	IDocPlayerControlProvider::Execute_AcquireDocControl(P1.Control.Get(), Sequence, SequenceClaim);
	const int32 FocusCalls = P1.Presenter->Focused.Num();
	FDocRequestHandle Menu;
	P1.UI->PushScreen(Req(TEXT("PauseMenu")), Menu);
	TestFalse(TEXT("UI does not steal focus from the sequence"), P1.IsFocusOwner(Menu));
	TestEqual(TEXT("Presenter focus untouched"), P1.Presenter->Focused.Num(), FocusCalls);
	P1.UI->PopScreen(Menu);
	TestTrue(TEXT("Closing the menu leaves the sequence claim"), IDocPlayerControlProvider::Execute_IsDocControlClaimActive(P1.Control.Get(), SequenceClaim));
	IDocPlayerControlProvider::Execute_ReleaseDocControl(P1.Control.Get(), SequenceClaim);

	// Networked world: no world pause, the menu still opens.
	Pause.Pause->SetPolicyOverrideForTesting(false);
	FDocRequestHandle NetMenu;
	TestTrue(TEXT("Menu opens"), P1.UI->PushScreen(Req(TEXT("PauseMenu")), NetMenu).IsSuccess());
	TestFalse(TEXT("MultiplayerNonPause"), Pause.bPaused);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
