#include "DocWorldTerminalSubsystem.h"
#include "Engine/World.h"

bool UDocWorldTerminalSubsystem::CanAccessFile(const FDocTerminalSession& Session, const FDocVirtualFile& File)
{
	return !File.RequiredPermission.IsValid() || Session.UserPermissions.HasTag(File.RequiredPermission);
}

uint32 UDocWorldTerminalSubsystem::HashCommand(const FDocTerminalCommandRequest& Request)
{
	TArray<FString> Keys;
	Request.Arguments.GetKeys(Keys);
	Keys.Sort();
	FString Canonical = Request.Verb.ToString();
	for (const FString& Key : Keys)
	{
		Canonical += FString::Printf(TEXT("|%s=%s"), *Key, *Request.Arguments[Key]);
	}
	Canonical += FString::Printf(TEXT("|rev=%d"), Request.ExpectedStateRevision);
	return GetTypeHash(Canonical);
}

void UDocWorldTerminalSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Devices.Empty();
	Sessions.Empty();
	CachedReceipts.Empty();
}

void UDocWorldTerminalSubsystem::Deinitialize()
{
	Devices.Empty();
	Sessions.Empty();
	CachedReceipts.Empty();
	Super::Deinitialize();
}

bool UDocWorldTerminalSubsystem::RegisterDevice(FName DeviceId, UDocTerminalDefinition* Definition)
{
	if (DeviceId.IsNone() || Devices.Contains(DeviceId))
	{
		return false;
	}

	FDocTerminalDeviceState State;
	State.DeviceId = DeviceId;
	State.StateRevision = 1;
	State.bHasPower = true;
	State.Exclusivity = Definition ? Definition->DefaultExclusivity : EDocTerminalExclusivity::SingleWriter;

	if (Definition)
	{
		State.DeviceSettings = Definition->InitialSettings;
		for (const FDocVirtualFile& File : Definition->InitialFiles)
		{
			FDocVirtualFile NormalizedFile = File;
			FString NormPath;
			if (!DocTerminalUtils::NormalizeVirtualPath(File.Path, NormPath))
			{
				UE_LOG(LogTemp, Warning, TEXT("Terminal %s: initial file '%s' has an invalid path and was skipped"), *DeviceId.ToString(), *File.Path);
				continue;
			}
			if (State.VirtualFiles.ContainsByPredicate([&NormPath](const FDocVirtualFile& F) { return F.Path == NormPath; }))
			{
				UE_LOG(LogTemp, Warning, TEXT("Terminal %s: duplicate initial file path '%s' was skipped"), *DeviceId.ToString(), *NormPath);
				continue;
			}
			if (File.Content.Len() > MaxFileContentLength || State.VirtualFiles.Num() >= MaxFilesPerDevice)
			{
				UE_LOG(LogTemp, Warning, TEXT("Terminal %s: initial file '%s' exceeds content or file-count limits and was skipped"), *DeviceId.ToString(), *NormPath);
				continue;
			}
			NormalizedFile.Path = NormPath;
			State.VirtualFiles.Add(NormalizedFile);
		}
	}

	Devices.Add(DeviceId, State);
	return true;
}

bool UDocWorldTerminalSubsystem::UnregisterDevice(FName DeviceId)
{
	RevokeSessionsForDevice(DeviceId);
	// Receipt results are session-scoped memory; the persisted ProcessedReceiptKeys still block re-execution.
	for (auto It = CachedReceipts.CreateIterator(); It; ++It)
	{
		if (It.Value().DeviceId == DeviceId) { It.RemoveCurrent(); }
	}
	return Devices.Remove(DeviceId) > 0;
}

void UDocWorldTerminalSubsystem::RegisterTerminalComponent(UDocTerminalComponent* Comp)
{
	if (!Comp || Comp->DeviceId.IsNone())
	{
		return;
	}

	if (!Devices.Contains(Comp->DeviceId))
	{
		RegisterDevice(Comp->DeviceId, Comp->Definition);
	}

	SetDevicePower(Comp->DeviceId, Comp->bHasPower);
}

void UDocWorldTerminalSubsystem::UnregisterTerminalComponent(UDocTerminalComponent* Comp)
{
	if (!Comp || Comp->DeviceId.IsNone())
	{
		return;
	}

	RevokeSessionsForDevice(Comp->DeviceId);
}

bool UDocWorldTerminalSubsystem::SetDevicePower(FName DeviceId, bool bHasPower)
{
	FDocTerminalDeviceState* DevPtr = Devices.Find(DeviceId);
	if (!DevPtr)
	{
		return false;
	}

	DevPtr->bHasPower = bHasPower;
	if (!bHasPower)
	{
		RevokeSessionsForDevice(DeviceId);
	}
	return true;
}

bool UDocWorldTerminalSubsystem::QueryDeviceState(FName DeviceId, FDocTerminalDeviceState& OutState)
{
	FDocTerminalDeviceState* DevPtr = Devices.Find(DeviceId);
	if (!DevPtr)
	{
		return false;
	}

	OutState = FDocTerminalDeviceState();
	OutState.DeviceId = DevPtr->DeviceId;
	OutState.StateRevision = DevPtr->StateRevision;
	OutState.bHasPower = DevPtr->bHasPower;
	OutState.Exclusivity = DevPtr->Exclusivity;
	OutState.DeviceSettings = DevPtr->DeviceSettings;
	OutState.ActiveWriterSessionId = DevPtr->ActiveWriterSessionId;
	for (const FDocVirtualFile& File : DevPtr->VirtualFiles)
	{
		if (!File.bIsTombstone && !File.RequiredPermission.IsValid())
		{
			FDocVirtualFile Summary = File;
			Summary.Content.Reset(); // content only through a session
			OutState.VirtualFiles.Add(Summary);
		}
	}
	return true;
}

FGuid UDocWorldTerminalSubsystem::OpenSession(FName DeviceId, const FDocOwnerScope& OwnerScope, const FGameplayTagContainer& UserPermissions, EDocTerminalSessionState& OutInitialState)
{
	OutInitialState = EDocTerminalSessionState::Failed;

	FDocTerminalDeviceState* DevPtr = Devices.Find(DeviceId);
	if (!DevPtr || !DevPtr->bHasPower)
	{
		return FGuid();
	}

	if (DevPtr->Exclusivity == EDocTerminalExclusivity::ExclusiveSession)
	{
		for (const auto& Pair : Sessions)
		{
			if (Pair.Value.DeviceId == DeviceId && Pair.Value.State == EDocTerminalSessionState::Active)
			{
				return FGuid();
			}
		}
	}

	FGuid NewSessionId = FGuid::NewGuid();
	FDocTerminalSession Session;
	Session.SessionId = NewSessionId;
	Session.DeviceId = DeviceId;
	Session.OwnerScope = OwnerScope;
	Session.UserPermissions = UserPermissions;
	Session.State = EDocTerminalSessionState::Active;
	Session.Generation = 1;
	Session.bHasWriteLease = false;

	Sessions.Add(NewSessionId, Session);
	OutInitialState = EDocTerminalSessionState::Active;
	return NewSessionId;
}

bool UDocWorldTerminalSubsystem::CloseSession(const FGuid& SessionId)
{
	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr || SessPtr->State == EDocTerminalSessionState::Closed)
	{
		return false;
	}

	ReleaseWriteLease(SessionId);
	SessPtr->State = EDocTerminalSessionState::Closed;
	return true;
}

bool UDocWorldTerminalSubsystem::AcquireWriteLease(const FGuid& SessionId)
{
	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr || SessPtr->State != EDocTerminalSessionState::Active)
	{
		return false;
	}

	FDocTerminalDeviceState* DevPtr = Devices.Find(SessPtr->DeviceId);
	if (!DevPtr || !DevPtr->bHasPower)
	{
		return false;
	}

	if (DevPtr->Exclusivity == EDocTerminalExclusivity::SingleWriter)
	{
		if (DevPtr->ActiveWriterSessionId.IsValid() && DevPtr->ActiveWriterSessionId != SessionId)
		{
			// Another session owns the write lease
			return false;
		}

		DevPtr->ActiveWriterSessionId = SessionId;
		SessPtr->bHasWriteLease = true;
		return true;
	}
	else if (DevPtr->Exclusivity == EDocTerminalExclusivity::SharedRead)
	{
		// SharedRead does not permit writes
		return false;
	}
	else // ExclusiveSession
	{
		DevPtr->ActiveWriterSessionId = SessionId;
		SessPtr->bHasWriteLease = true;
		return true;
	}
}

bool UDocWorldTerminalSubsystem::ReleaseWriteLease(const FGuid& SessionId)
{
	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr || !SessPtr->bHasWriteLease)
	{
		return false;
	}

	SessPtr->bHasWriteLease = false;

	FDocTerminalDeviceState* DevPtr = Devices.Find(SessPtr->DeviceId);
	if (DevPtr && DevPtr->ActiveWriterSessionId == SessionId)
	{
		DevPtr->ActiveWriterSessionId.Invalidate();
	}

	return true;
}

bool UDocWorldTerminalSubsystem::QuerySession(const FGuid& SessionId, FDocTerminalSession& OutSession)
{
	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr)
	{
		return false;
	}

	OutSession = *SessPtr;
	return true;
}

bool UDocWorldTerminalSubsystem::ListFiles(const FGuid& SessionId, const FString& DirectoryPath, TArray<FDocVirtualFile>& OutFiles)
{
	OutFiles.Reset();

	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr || SessPtr->State != EDocTerminalSessionState::Active)
	{
		return false;
	}

	FDocTerminalDeviceState* DevPtr = Devices.Find(SessPtr->DeviceId);
	if (!DevPtr || !DevPtr->bHasPower)
	{
		return false;
	}

	FString NormDir;
	if (!DocTerminalUtils::NormalizeVirtualPath(DirectoryPath, NormDir))
	{
		return false;
	}

	if (!NormDir.EndsWith(TEXT("/")))
	{
		NormDir += TEXT("/");
	}

	for (const FDocVirtualFile& File : DevPtr->VirtualFiles)
	{
		if (File.bIsTombstone)
		{
			continue;
		}

		// Security: Unauthorized files are completely hidden from directory views
		if (File.RequiredPermission.IsValid() && !SessPtr->UserPermissions.HasTag(File.RequiredPermission))
		{
			continue;
		}

		if (NormDir == TEXT("/") || File.Path.StartsWith(NormDir))
		{
			OutFiles.Add(File);
		}
	}

	return true;
}

bool UDocWorldTerminalSubsystem::ReadFile(const FGuid& SessionId, const FString& FilePath, FDocVirtualFile& OutFile)
{
	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr || SessPtr->State != EDocTerminalSessionState::Active)
	{
		return false;
	}

	FDocTerminalDeviceState* DevPtr = Devices.Find(SessPtr->DeviceId);
	if (!DevPtr || !DevPtr->bHasPower)
	{
		return false;
	}

	FString NormPath;
	if (!DocTerminalUtils::NormalizeVirtualPath(FilePath, NormPath))
	{
		return false;
	}

	for (const FDocVirtualFile& File : DevPtr->VirtualFiles)
	{
		if (File.Path == NormPath && !File.bIsTombstone)
		{
			// Check permission
			if (File.RequiredPermission.IsValid() && !SessPtr->UserPermissions.HasTag(File.RequiredPermission))
			{
				return false;
			}

			OutFile = File;
			return true;
		}
	}

	return false;
}

bool UDocWorldTerminalSubsystem::WriteFile(const FGuid& SessionId, const FString& FilePath, const FString& Content, const FText& Title, const FGameplayTag& RequiredPermission)
{
	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr || SessPtr->State != EDocTerminalSessionState::Active || !SessPtr->bHasWriteLease)
	{
		return false;
	}

	FDocTerminalDeviceState* DevPtr = Devices.Find(SessPtr->DeviceId);
	if (!DevPtr || !DevPtr->bHasPower)
	{
		return false;
	}

	FString NormPath;
	if (!DocTerminalUtils::NormalizeVirtualPath(FilePath, NormPath))
	{
		return false;
	}
	if (Content.Len() > MaxFileContentLength)
	{
		return false;
	}
	// A session may only assign a permission it holds itself.
	if (RequiredPermission.IsValid() && !SessPtr->UserPermissions.HasTag(RequiredPermission))
	{
		return false;
	}

	for (FDocVirtualFile& File : DevPtr->VirtualFiles)
	{
		if (File.Path == NormPath)
		{
			if (!CanAccessFile(*SessPtr, File))
			{
				return false; // protected file: same rule as ReadFile, including tombstones
			}
			File.Content = Content;
			File.bIsTombstone = false;
			File.Version++;
			if (!Title.IsEmpty())
			{
				File.DisplayTitle = Title;
			}
			if (RequiredPermission.IsValid())
			{
				File.RequiredPermission = RequiredPermission;
			}
			DevPtr->StateRevision++;
			return true;
		}
	}

	if (DevPtr->VirtualFiles.Num() >= MaxFilesPerDevice)
	{
		return false;
	}

	// Create new
	FDocVirtualFile NewFile;
	NewFile.FileId = FGuid::NewGuid();
	NewFile.Path = NormPath;
	NewFile.DisplayTitle = Title.IsEmpty() ? FText::FromString(NormPath) : Title;
	NewFile.Content = Content;
	NewFile.RequiredPermission = RequiredPermission;
	NewFile.Version = 1;
	NewFile.bIsTombstone = false;

	DevPtr->VirtualFiles.Add(NewFile);
	DevPtr->StateRevision++;
	return true;
}

bool UDocWorldTerminalSubsystem::DeleteFile(const FGuid& SessionId, const FString& FilePath)
{
	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr || SessPtr->State != EDocTerminalSessionState::Active || !SessPtr->bHasWriteLease)
	{
		return false;
	}

	FDocTerminalDeviceState* DevPtr = Devices.Find(SessPtr->DeviceId);
	if (!DevPtr || !DevPtr->bHasPower)
	{
		return false;
	}

	FString NormPath;
	if (!DocTerminalUtils::NormalizeVirtualPath(FilePath, NormPath))
	{
		return false;
	}

	for (FDocVirtualFile& File : DevPtr->VirtualFiles)
	{
		if (File.Path == NormPath && !File.bIsTombstone)
		{
			if (!CanAccessFile(*SessPtr, File))
			{
				return false;
			}
			File.bIsTombstone = true;
			DevPtr->StateRevision++;
			return true;
		}
	}

	return false;
}

FDocTerminalCommandResult UDocWorldTerminalSubsystem::ExecuteCommand(const FGuid& SessionId, const FDocTerminalCommandRequest& Request)
{
	FDocTerminalCommandResult Result;
	Result.bSuccess = false;

	// 1. The session and device are validated before any receipt is returned,
	//    so a revoked session or another user cannot read results by replaying a key.
	FDocTerminalSession* SessPtr = Sessions.Find(SessionId);
	if (!SessPtr || SessPtr->State != EDocTerminalSessionState::Active)
	{
		Result.OutputMessage = TEXT("Session not active or revoked.");
		return Result;
	}

	FDocTerminalDeviceState* DevPtr = Devices.Find(SessPtr->DeviceId);
	if (!DevPtr || !DevPtr->bHasPower)
	{
		Result.OutputMessage = TEXT("Device has no power.");
		return Result;
	}

	// 2. Idempotency: a receipt belongs to one device, owner and payload.
	const uint32 PayloadHash = HashCommand(Request);
	if (Request.IdempotencyKey.IsValid())
	{
		if (const FTerminalReceipt* Existing = CachedReceipts.Find(Request.IdempotencyKey))
		{
			if (Existing->DeviceId != DevPtr->DeviceId || Existing->Owner != SessPtr->OwnerScope || Existing->PayloadHash != PayloadHash)
			{
				Result.OutputMessage = TEXT("Idempotency key was already used for a different command, device or owner.");
				return Result;
			}
			return Existing->Result;
		}
		if (DevPtr->ProcessedReceiptKeys.Contains(Request.IdempotencyKey))
		{
			// Committed before a restore: the result is not cached, but the command must never run again.
			Result.bSuccess = true;
			Result.CommittedRevision = DevPtr->StateRevision;
			Result.OutputMessage = TEXT("Command already committed; not executed again.");
			return Result;
		}
	}

	// Revision check
	if (Request.ExpectedStateRevision > 0 && Request.ExpectedStateRevision != DevPtr->StateRevision)
	{
		Result.OutputMessage = FString::Printf(TEXT("State revision mismatch (expected %d, actual %d)"),
			Request.ExpectedStateRevision, DevPtr->StateRevision);
		return Result;
	}

	// 2. Command dispatch
	if (Request.Verb == TEXT("status"))
	{
		Result.bSuccess = true;
		Result.OutputMessage = FString::Printf(TEXT("Device=%s, Power=%d, Rev=%d"),
			*DevPtr->DeviceId.ToString(), DevPtr->bHasPower ? 1 : 0, DevPtr->StateRevision);
	}
	else if (Request.Verb == TEXT("get_setting"))
	{
		const FString* KeyPtr = Request.Arguments.Find(TEXT("key"));
		if (!KeyPtr)
		{
			Result.OutputMessage = TEXT("Missing 'key' argument.");
			return Result;
		}

		const FString* FoundVal = DevPtr->DeviceSettings.Find(*KeyPtr);
		if (FoundVal)
		{
			Result.bSuccess = true;
			Result.OutputMessage = *FoundVal;
		}
		else
		{
			Result.OutputMessage = FString::Printf(TEXT("Setting '%s' not found."), **KeyPtr);
		}
	}
	else if (Request.Verb == TEXT("set_setting"))
	{
		if (!SessPtr->bHasWriteLease)
		{
			Result.OutputMessage = TEXT("Write lease required to alter settings.");
			return Result;
		}

		const FString* KeyPtr = Request.Arguments.Find(TEXT("key"));
		const FString* ValPtr = Request.Arguments.Find(TEXT("value"));
		if (!KeyPtr || !ValPtr)
		{
			Result.OutputMessage = TEXT("Missing 'key' or 'value' argument.");
			return Result;
		}

		DevPtr->DeviceSettings.Add(*KeyPtr, *ValPtr);
		DevPtr->StateRevision++;
		Result.bSuccess = true;
		Result.OutputMessage = FString::Printf(TEXT("Setting '%s' set to '%s'"), **KeyPtr, **ValPtr);
	}
	else
	{
		// Unknown verb
		Result.OutputMessage = FString::Printf(TEXT("Unknown command verb '%s'"), *Request.Verb.ToString());
		return Result;
	}

	// 3. Receipt generation
	if (Result.bSuccess)
	{
		Result.CommittedRevision = DevPtr->StateRevision;
		Result.Receipt.Key.Owner = SessPtr->OwnerScope;
		Result.Receipt.Key.ProducerInstanceId = SessPtr->SessionId;
		Result.Receipt.Key.TransitionOrdinal = DevPtr->StateRevision;
		Result.Receipt.Key.ActionId = Request.Verb;
		Result.Receipt.Result = FDocSystemResult::MakeSuccess(DevPtr->StateRevision);
		Result.Receipt.CommittedRevision = DevPtr->StateRevision;

		if (Request.IdempotencyKey.IsValid())
		{
			DevPtr->ProcessedReceiptKeys.Add(Request.IdempotencyKey);
			FTerminalReceipt Receipt;
			Receipt.DeviceId = DevPtr->DeviceId;
			Receipt.Owner = SessPtr->OwnerScope;
			Receipt.PayloadHash = PayloadHash;
			Receipt.Result = Result;
			CachedReceipts.Add(Request.IdempotencyKey, Receipt);
		}
	}

	return Result;
}

FDocTerminalDeviceState UDocWorldTerminalSubsystem::CaptureDeviceState(FName DeviceId) const
{
	const FDocTerminalDeviceState* DevPtr = Devices.Find(DeviceId);
	if (!DevPtr)
	{
		return FDocTerminalDeviceState();
	}

	FDocTerminalDeviceState OutState = *DevPtr;
	// Do not persist transient writer session id
	OutState.ActiveWriterSessionId.Invalidate();
	return OutState;
}

bool UDocWorldTerminalSubsystem::StageRestoreDeviceState(const FDocTerminalDeviceState& InState)
{
	FDocTerminalDeviceState* DevPtr = Devices.Find(InState.DeviceId);
	if (!DevPtr)
	{
		return false;
	}

	DevPtr->StateRevision = InState.StateRevision;
	DevPtr->bHasPower = InState.bHasPower;
	DevPtr->Exclusivity = InState.Exclusivity;
	DevPtr->DeviceSettings = InState.DeviceSettings;
	DevPtr->VirtualFiles = InState.VirtualFiles;
	DevPtr->ProcessedReceiptKeys = InState.ProcessedReceiptKeys;
	DevPtr->ActiveWriterSessionId.Invalidate();

	RevokeSessionsForDevice(InState.DeviceId);
	return true;
}

void UDocWorldTerminalSubsystem::RevokeSessionsForDevice(FName DeviceId)
{
	for (auto& Pair : Sessions)
	{
		if (Pair.Value.DeviceId == DeviceId)
		{
			Pair.Value.State = EDocTerminalSessionState::Revoked;
			Pair.Value.bHasWriteLease = false;
		}
	}

	FDocTerminalDeviceState* DevPtr = Devices.Find(DeviceId);
	if (DevPtr)
	{
		DevPtr->ActiveWriterSessionId.Invalidate();
	}
}
