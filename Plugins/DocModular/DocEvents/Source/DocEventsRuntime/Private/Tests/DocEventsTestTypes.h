#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocEventsTestTypes.generated.h"

/** Test-only payloads. Compiled in all configurations (UHT cannot see preprocessor test guards) but unused outside tests. */
USTRUCT()
struct FDocEventsTestPayload
{
	GENERATED_BODY()

	UPROPERTY() int32 Value = 0;
	UPROPERTY() TWeakObjectPtr<UObject> WeakRef;
};

USTRUCT()
struct FDocEventsTestStrongPayload
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UObject> StrongRef = nullptr;
};
