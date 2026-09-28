#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocSharedTypes.h"
#include "DocEffectKey.h"
#include "DocTerminalTypes.generated.h"

UENUM(BlueprintType)
enum class EDocTerminalSessionState : uint8
{
	Opening,
	Authorized,
	Active,
	Closing,
	Closed,
	Failed,
	Revoked
};

UENUM(BlueprintType)
enum class EDocTerminalExclusivity : uint8
{
	SharedRead,
	SingleWriter,
	ExclusiveSession
};

USTRUCT(BlueprintType)
struct DOCWORLDTERMINALSRUNTIME_API FDocVirtualFile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FGuid FileId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FString Path;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FText DisplayTitle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FString Content;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FGameplayTag RequiredPermission;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	bool bIsTombstone = false;
};

USTRUCT(BlueprintType)
struct DOCWORLDTERMINALSRUNTIME_API FDocTerminalSession
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FGuid SessionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FName DeviceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FDocOwnerScope OwnerScope;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	EDocTerminalSessionState State = EDocTerminalSessionState::Active;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FGameplayTagContainer UserPermissions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FName CurrentApplicationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	int32 Generation = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	bool bHasWriteLease = false;
};

USTRUCT(BlueprintType)
struct DOCWORLDTERMINALSRUNTIME_API FDocTerminalCommandRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FName Verb = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	TMap<FString, FString> Arguments;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FGuid IdempotencyKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	int32 ExpectedStateRevision = 0;
};

USTRUCT(BlueprintType)
struct DOCWORLDTERMINALSRUNTIME_API FDocTerminalCommandResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	bool bSuccess = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FString OutputMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	int32 CommittedRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FDocEffectReceipt Receipt;
};

USTRUCT(BlueprintType)
struct DOCWORLDTERMINALSRUNTIME_API FDocTerminalDeviceState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FName DeviceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	int32 StateRevision = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	bool bHasPower = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	EDocTerminalExclusivity Exclusivity = EDocTerminalExclusivity::SingleWriter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	TMap<FString, FString> DeviceSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	TArray<FDocVirtualFile> VirtualFiles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	TSet<FGuid> ProcessedReceiptKeys;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FGuid ActiveWriterSessionId;
};

namespace DocTerminalUtils
{
	DOCWORLDTERMINALSRUNTIME_API bool NormalizeVirtualPath(const FString& InPath, FString& OutNormalizedPath);
}
