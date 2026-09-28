#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocTerminalTypes.h"
#include "DocTerminalDefinition.h"
#include "DocTerminalComponent.h"
#include "DocWorldTerminalSubsystem.generated.h"

UCLASS()
class DOCWORLDTERMINALSRUNTIME_API UDocWorldTerminalSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Device Registration
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool RegisterDevice(FName DeviceId, UDocTerminalDefinition* Definition);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool UnregisterDevice(FName DeviceId);

	void RegisterTerminalComponent(UDocTerminalComponent* Comp);
	void UnregisterTerminalComponent(UDocTerminalComponent* Comp);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool SetDevicePower(FName DeviceId, bool bHasPower);

	/**
	 * Public device summary: power, revision, settings and writer state. File list contains only
	 * files without a RequiredPermission, and never file content or receipt keys. Read content
	 * through a session (ListFiles/ReadFile) so permissions apply.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool QueryDeviceState(FName DeviceId, FDocTerminalDeviceState& OutState);

	// Session Management
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	FGuid OpenSession(FName DeviceId, const FDocOwnerScope& OwnerScope, const FGameplayTagContainer& UserPermissions, EDocTerminalSessionState& OutInitialState);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool CloseSession(const FGuid& SessionId);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool AcquireWriteLease(const FGuid& SessionId);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool ReleaseWriteLease(const FGuid& SessionId);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool QuerySession(const FGuid& SessionId, FDocTerminalSession& OutSession);

	// Virtual Filesystem Operations
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool ListFiles(const FGuid& SessionId, const FString& DirectoryPath, TArray<FDocVirtualFile>& OutFiles);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool ReadFile(const FGuid& SessionId, const FString& FilePath, FDocVirtualFile& OutFile);

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool WriteFile(const FGuid& SessionId, const FString& FilePath, const FString& Content, const FText& Title = FText::GetEmpty(), const FGameplayTag& RequiredPermission = FGameplayTag());

	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool DeleteFile(const FGuid& SessionId, const FString& FilePath);

	// Typed Command Execution
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	FDocTerminalCommandResult ExecuteCommand(const FGuid& SessionId, const FDocTerminalCommandRequest& Request);

	// Persistence / Restore. Trusted save code only (full, unfiltered state), so not exposed to Blueprint.
	FDocTerminalDeviceState CaptureDeviceState(FName DeviceId) const;
	bool StageRestoreDeviceState(const FDocTerminalDeviceState& InState);

	/** Limits for virtual content (bounded memory and save size). */
	static constexpr int32 MaxFileContentLength = 64 * 1024;
	static constexpr int32 MaxFilesPerDevice = 512;

private:
	struct FTerminalReceipt
	{
		FName DeviceId;
		FDocOwnerScope Owner;
		uint32 PayloadHash = 0;
		FDocTerminalCommandResult Result;
	};

	static uint32 HashCommand(const FDocTerminalCommandRequest& Request);
	static bool CanAccessFile(const FDocTerminalSession& Session, const FDocVirtualFile& File);

	TMap<FName, FDocTerminalDeviceState> Devices;
	TMap<FGuid, FDocTerminalSession> Sessions;
	/** Receipts are bound to the device, owner and payload that created them. */
	TMap<FGuid, FTerminalReceipt> CachedReceipts;

	void RevokeSessionsForDevice(FName DeviceId);
};
