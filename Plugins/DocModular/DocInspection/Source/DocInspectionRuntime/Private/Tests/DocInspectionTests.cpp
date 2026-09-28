// DocInspection automation tests (INS-01..05, INS-07 logic). Subsystems are created
// on bare ULocalPlayers (no controller, no Enhanced Input subsystem), so input
// context ownership and real rendering are not exercised here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocInspectionSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace DocInspectionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FPlayer
	{
		TStrongObjectPtr<ULocalPlayer> Player;
		TStrongObjectPtr<UDocInspectionSubsystem> S;
		TArray<EDocInspectionState> Ended;

		explicit FPlayer(UWorld* World)
		{
			Player.Reset(NewObject<ULocalPlayer>(GEngine));
			S.Reset(NewObject<UDocInspectionSubsystem>(Player.Get()));
			S->InitializeForTesting(World);
		}
		~FPlayer() { if (S.IsValid()) { S->CloseInspection(S->GetViewModel().Session); } }
	};

	UDocInspectionDefinition* MakeDef(FName Id, const FGameplayTag& Type, EDocInspectionMode Mode = EDocInspectionMode::World)
	{
		UDocInspectionDefinition* Def = NewObject<UDocInspectionDefinition>(GetTransientPackage());
		Def->InspectionId = Id;
		Def->ContentType = Type;
		Def->Title = FText::FromString(Id.ToString());
		Def->DefaultMode = Mode;
		return Def;
	}

	UDocInspectableComponent* SpawnInspectable(FDocScopedTestWorld& TW, UDocInspectionDefinition* Def, const FVector& Location)
	{
		AActor* Actor = TW.Spawn<AActor>(Location);
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		UDocInspectableComponent* Inspectable = NewObject<UDocInspectableComponent>(Actor);
		Inspectable->Definition = Def;
		Inspectable->SetupAttachment(Root);
		Inspectable->RegisterComponent();
		return Inspectable;
	}

	FSoftObjectPath MissingPath() { return FSoftObjectPath(TEXT("/Game/DocInspectionTest/Missing.Missing")); }
}

using namespace DocInspectionTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInspectionWorldPreviewTest, "Doc.Inspection.WorldAndPreview", Flags)
bool FDocInspectionWorldPreviewTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	FPlayer P(TW.World);
	UDocInspectionDefinition* Def = MakeDef(TEXT("Vase"), DocInspectionTags::Object3D, EDocInspectionMode::World);
	Def->Limits.MinPitch = -30.f;
	Def->Limits.MaxPitch = 30.f;
	UDocInspectableComponent* Inspectable = SpawnInspectable(TW, Def, FVector(500, 0, 0));
	AActor* Live = Inspectable->GetOwner();
	const FTransform Before = Live->GetActorTransform();

	FDocRequestHandle Session;
	TestTrue(TEXT("Open world"), P.S->OpenInspection(Inspectable, Session).IsSuccess());
	TestTrue(TEXT("Active"), P.S->IsInspecting());
	P.S->Rotate(FVector2D(45.f, 90.f));
	P.S->Zoom(1000.f);
	const FDocInspectionViewModel VM = P.S->GetViewModel();
	TestEqual(TEXT("Pitch clamped"), VM.ViewRotation.Pitch, 30.0);
	TestEqual(TEXT("Zoom clamped"), VM.Distance, Def->Limits.MinDistance);
	TestFalse(TEXT("World view computed"), VM.WorldViewTransform.Equals(FTransform::Identity));
	TestTrue(TEXT("Live actor never moved"), Live->GetActorTransform().Equals(Before));
	P.S->CloseInspection(Session);
	TestFalse(TEXT("Closed"), P.S->IsInspecting());

	// Preview: presentation copy on an isolated stage.
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
	UDocInspectionDefinition* PreviewDef = MakeDef(TEXT("Statue"), DocInspectionTags::Object3D, EDocInspectionMode::Preview);
	PreviewDef->PreviewMesh = Mesh;
	UDocInspectableComponent* Statue = SpawnInspectable(TW, PreviewDef, FVector(0, 800, 0));
	const FTransform StatueBefore = Statue->GetOwner()->GetActorTransform();
	TestTrue(TEXT("Open preview"), P.S->OpenInspection(Statue, Session).IsSuccess());
	ADocInspectionPreviewStage* Stage = P.S->GetPreviewStageForTesting();
	if (!TestNotNull(TEXT("Stage spawned"), Stage)) { return false; }
	TestNotNull(TEXT("Session-owned render target"), P.S->GetViewModel().PreviewTarget.Get());
	TestEqual(TEXT("Initial capture"), P.S->GetCaptureCountForTesting(), 1);
	for (int32 i = 0; i < 10; ++i) { P.S->Rotate(FVector2D(5.f, 0.f)); }
	TestEqual(TEXT("Captures throttled"), P.S->GetCaptureCountForTesting(), 1);
	P.S->AdvanceForTesting(0.1f);
	TestEqual(TEXT("One capture after the change"), P.S->GetCaptureCountForTesting(), 2);
	P.S->AdvanceForTesting(0.1f);
	P.S->AdvanceForTesting(0.1f);
	TestEqual(TEXT("No capture while unchanged"), P.S->GetCaptureCountForTesting(), 2);
	TestTrue(TEXT("Live statue untouched"), Statue->GetOwner()->GetActorTransform().Equals(StatueBefore));
	P.S->CloseInspection(Session);
	TestTrue(TEXT("Stage destroyed on close"), !IsValid(Stage) || Stage->IsActorBeingDestroyed());

	// Inspected object destroyed during a session.
	TestTrue(TEXT("Reopen"), P.S->OpenInspection(Inspectable, Session).IsSuccess());
	Live->Destroy();
	P.S->AdvanceForTesting(0.1f);
	TestFalse(TEXT("Session ends when the object is gone"), P.S->IsInspecting());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInspectionPagesTest, "Doc.Inspection.DocumentsAndPages", Flags)
bool FDocInspectionPagesTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	FPlayer P(TW.World);
	UDocInspectionDefinition* Book = MakeDef(TEXT("Diary"), DocInspectionTags::Book);
	for (const TCHAR* Text : { TEXT("One"), TEXT("Two"), TEXT("Three") })
	{
		FDocInspectionPage Page;
		Page.Text = FText::FromString(Text);
		Book->Pages.Add(Page);
	}
	FDocRequestHandle Session;
	TestTrue(TEXT("Open definition"), P.S->OpenDefinition(Book, Session).IsSuccess());
	TestEqual(TEXT("Definition-only sessions use preview mode"), P.S->GetViewModel().Mode, EDocInspectionMode::Preview);
	TestEqual(TEXT("Page count"), P.S->GetViewModel().PageCount, 3);
	TestFalse(TEXT("No page before first"), P.S->PreviousPage());
	TestTrue(TEXT("Next"), P.S->NextPage());
	P.S->Accept(); // advances on multi-page content
	TestEqual(TEXT("Third page text"), P.S->GetViewModel().PageText.ToString(), FString(TEXT("Three")));
	TestFalse(TEXT("No page after last"), P.S->NextPage());
	TestTrue(TEXT("Previous"), P.S->PreviousPage());
	TestEqual(TEXT("Accessible text available"), P.S->GetViewModel().PageText.ToString(), FString(TEXT("Two")));
	P.S->Back();
	TestFalse(TEXT("Back closes"), P.S->IsInspecting());

	UDocInspectionDefinition* Video = MakeDef(TEXT("Tape"), DocInspectionTags::Video);
	TestEqual(TEXT("Video without the media bridge is Unsupported"), P.S->OpenDefinition(Video, Session).Outcome, EDocResultOutcome::Unsupported);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInspectionPlayersTest, "Doc.Inspection.IndependentPlayers", Flags)
bool FDocInspectionPlayersTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	FPlayer P1(TW.World);
	FPlayer P2(TW.World);
	UDocInspectionDefinition* Def = MakeDef(TEXT("Map"), DocInspectionTags::Document);
	FDocInspectionFocusPoint Point;
	Point.FocusId = TEXT("Stamp");
	Def->FocusPoints.Add(Point);

	FDocRequestHandle S1, S2;
	P1.S->OpenDefinition(Def, S1);
	P2.S->OpenDefinition(Def, S2);
	TestNotEqual(TEXT("Separate sessions"), S1, S2);
	P1.S->NextPage();
	TestTrue(TEXT("P1 discovers"), P1.S->ActivateFocusPoint(TEXT("Stamp")).IsSuccess());
	TestFalse(TEXT("P2 discoveries independent"), P2.S->IsFocusDiscovered(TEXT("Map"), TEXT("Stamp")));
	P1.S->CloseInspection(S1);
	TestTrue(TEXT("Closing P1 leaves P2 open"), P2.S->IsInspecting());
	TestNull(TEXT("Missing player is explicit (no fallback to player 0)"), UDocInspectionSubsystem::Get(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInspectionCleanupTest, "Doc.Inspection.LoadFailureAndCleanup", Flags)
bool FDocInspectionCleanupTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	FPlayer P(TW.World);
	TArray<EDocInspectionState> States;
	P.S->OnViewModelChangedNative.AddLambda([&States](const FDocInspectionViewModel& VM) { States.Add(VM.State); });
	TArray<TFunction<void()>> Loads;
	P.S->SetLoaderForTesting([&Loads](const TArray<FSoftObjectPath>&, TFunction<void()> Done) { Loads.Add(MoveTemp(Done)); });

	UDocInspectionDefinition* Slow = MakeDef(TEXT("Photo"), DocInspectionTags::Image);
	Slow->Image = TSoftObjectPtr<UTexture2D>(MissingPath());
	FDocRequestHandle Session;
	P.S->OpenDefinition(Slow, Session);
	TestEqual(TEXT("Loading"), P.S->GetViewModel().State, EDocInspectionState::Loading);
	P.S->CloseInspection(Session);
	TestTrue(TEXT("Closing during load cancels"), States.Contains(EDocInspectionState::Cancelled));
	if (Loads.Num() > 0) { Loads[0](); }
	TestFalse(TEXT("Late load activates nothing"), P.S->IsInspecting());

	P.S->OpenDefinition(Slow, Session);
	if (Loads.Num() > 1) { Loads[1](); } // asset never exists
	TestTrue(TEXT("Missing asset fails"), States.Contains(EDocInspectionState::Failed));
	TestFalse(TEXT("Not inspecting after failure"), P.S->IsInspecting());

	P.S->OpenDefinition(Slow, Session);
	P.S->AdvanceForTesting(GetDefault<UDocInspectionSettings>()->LoadTimeoutSeconds + 1.f);
	TestFalse(TEXT("Load timeout ends the session"), P.S->IsInspecting());
	P.S->SetLoaderForTesting(nullptr);

	// Replace vs reject.
	UDocInspectionDefinition* A = MakeDef(TEXT("A"), DocInspectionTags::Document);
	UDocInspectionDefinition* B = MakeDef(TEXT("B"), DocInspectionTags::Document);
	P.S->OpenDefinition(A, Session);
	FDocRequestHandle Second;
	TestTrue(TEXT("Replace by default"), P.S->OpenDefinition(B, Second).IsSuccess());
	TestEqual(TEXT("B is open"), P.S->GetViewModel().InspectionId, FName(TEXT("B")));
	UDocInspectionSettings* Settings = GetMutableDefault<UDocInspectionSettings>();
	const EDocInspectionOpenPolicy Saved = Settings->OpenPolicy;
	Settings->OpenPolicy = EDocInspectionOpenPolicy::RejectWhileActive;
	TestEqual(TEXT("Reject policy"), P.S->OpenDefinition(A, Session).Outcome, EDocResultOutcome::Conflict);
	Settings->OpenPolicy = Saved;

	// Player removal (subsystem deinitialize path) releases the session.
	P.S->Deinitialize();
	TestFalse(TEXT("Player removal closes"), P.S->IsInspecting());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDocInspectionFocusTest, "Doc.Inspection.FocusPoints", Flags)
bool FDocInspectionFocusTest::RunTest(const FString& Parameters)
{
	FDocScopedTestWorld TW;
	FPlayer P(TW.World);
	UDocInspectionDefinition* Def = MakeDef(TEXT("Chest"), DocInspectionTags::Document);
	FDocInspectionFocusPoint Lock;
	Lock.FocusId = TEXT("Lock");
	Lock.RevealText = FText::FromString(TEXT("A worn lock"));
	FDocInspectionFocusPoint Key;
	Key.FocusId = TEXT("Keyhole");
	Key.RequiresDiscovered = { TEXT("Lock") };
	Key.GrantTags.AddTag(DocInspectionTags::Custom);
	Def->FocusPoints = { Lock, Key };

	int32 Discoveries = 0;
	P.S->OnFocusDiscoveredNative.AddLambda([&Discoveries](FName, FName) { ++Discoveries; });
	FDocRequestHandle Session;
	P.S->OpenDefinition(Def, Session);
	TestEqual(TEXT("Dependency enforced"), P.S->ActivateFocusPoint(TEXT("Keyhole")).Outcome, EDocResultOutcome::PermissionDenied);
	TestTrue(TEXT("Discover lock"), P.S->ActivateFocusPoint(TEXT("Lock")).IsSuccess());
	TestEqual(TEXT("Idempotent"), P.S->ActivateFocusPoint(TEXT("Lock")).Outcome, EDocResultOutcome::NoChange);
	const FDocSystemResult Keyhole = P.S->ActivateFocusPoint(TEXT("Keyhole"));
	TestTrue(TEXT("Unlocked"), Keyhole.IsSuccess());
	TestTrue(TEXT("Grant needs an authorized provider"), Keyhole.Diagnostic.Contains(TEXT("no reward provider")));
	TestEqual(TEXT("One event per discovery"), Discoveries, 2);
	TestEqual(TEXT("Unknown focus"), P.S->ActivateFocusPoint(TEXT("Nope")).Outcome, EDocResultOutcome::NotFound);
	TestTrue(TEXT("Reveal text shown once discovered"), P.S->GetViewModel().FocusPoints[0].RevealText.ToString() == TEXT("A worn lock"));

	// Discoveries are per player and persist through Get/Restore, not in the shared definition.
	const TMap<FName, TArray<FName>> Saved = P.S->GetDiscoveries();
	P.S->RestoreDiscoveries({});
	TestFalse(TEXT("Cleared"), P.S->IsFocusDiscovered(TEXT("Chest"), TEXT("Lock")));
	P.S->RestoreDiscoveries(Saved);
	TestTrue(TEXT("Restored"), P.S->IsFocusDiscovered(TEXT("Chest"), TEXT("Keyhole")));
	TestEqual(TEXT("Shared definition unchanged"), Def->FocusPoints.Num(), 2);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
