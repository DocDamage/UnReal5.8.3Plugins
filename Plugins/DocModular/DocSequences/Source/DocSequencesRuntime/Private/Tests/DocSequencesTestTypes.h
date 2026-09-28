#pragma once

// Test-only types for DocSequences automation tests (always compiled; hidden).

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocSequenceTypes.h"
#include "DocSequencesTestTypes.generated.h"

/** Scripted prerequisite provider: records acquire/release, readiness completed by the test. */
UCLASS(NotBlueprintable, HideDropdown)
class UDocSequencesTestPrerequisiteProvider : public UObject, public IDocSequencePrerequisiteProvider
{
	GENERATED_BODY()

public:
	FGameplayTag Handled;
	TArray<TFunction<void(bool)>> Pending;
	TArray<int64> Acquired;
	TArray<int64> Released;
	bool bReadyImmediately = false;

	virtual bool HandlesDocSequencePrerequisite(const FGameplayTag& Prerequisite) const override { return Prerequisite == Handled; }
	virtual void AcquireDocSequencePrerequisite(const FGameplayTag& Prerequisite, int64 SessionId, TFunction<void(bool)> OnReady) override
	{
		Acquired.Add(SessionId);
		if (bReadyImmediately) { OnReady(true); }
		else { Pending.Add(MoveTemp(OnReady)); }
	}
	virtual void ReleaseDocSequencePrerequisites(int64 SessionId) override { Released.AddUnique(SessionId); }
};
