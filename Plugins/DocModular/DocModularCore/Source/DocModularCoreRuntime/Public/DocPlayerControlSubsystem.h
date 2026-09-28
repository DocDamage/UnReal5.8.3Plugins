#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "DocSystemResult.h"
#include "DocRequestHandle.h"
#include "Interfaces/DocPlayerControlProvider.h"
#include "DocPlayerControlSubsystem.generated.h"

/**
 * Per-local-player registry for the single shared IDocPlayerControlProvider
 * (handoff 3.10, expansion 13.4). Inspection, Sequences, Dialogue presentation and
 * Framework UI all acquire control through this subsystem, so they share one
 * provider per local player instead of each keeping snapshot/restore stacks.
 *
 * There is no provider by default. Without one, every Acquire returns Unavailable;
 * features decide whether that is fatal or whether they run in a declared
 * no-control-mutation mode. Call UseReferenceProvider() for the packaged adapter,
 * or SetControlProvider() with a project adapter.
 */
UCLASS()
class DOCMODULARCORERUNTIME_API UDocPlayerControlSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** Install a provider object implementing IDocPlayerControlProvider. Existing claims on the old provider are not migrated. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Control")
	FDocSystemResult SetControlProvider(UObject* ProviderObject);

	/** Install (or return) the packaged UDocReferencePlayerControlProvider for this local player. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Control")
	UObject* UseReferenceProvider();

	UFUNCTION(BlueprintCallable, Category = "Doc|Control")
	void ClearControlProvider();

	UFUNCTION(BlueprintPure, Category = "Doc|Control")
	UObject* GetControlProvider() const { return Provider; }

	UFUNCTION(BlueprintPure, Category = "Doc|Control")
	bool HasControlProvider() const { return Provider != nullptr; }

	/** Fills Request.LocalPlayer when unset. Returns Unavailable when no provider is installed. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Control")
	FDocSystemResult AcquireControl(FDocControlClaimRequest Request, FDocRequestHandle& OutClaim);

	/** Idempotent. Releasing an unknown or stale claim changes nothing. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Control")
	FDocSystemResult ReleaseControl(const FDocRequestHandle& Claim);

	UFUNCTION(BlueprintPure, Category = "Doc|Control")
	bool IsClaimActive(const FDocRequestHandle& Claim) const;

	UFUNCTION(BlueprintPure, Category = "Doc|Control")
	bool IsEffectiveOwner(const FDocRequestHandle& Claim, FGameplayTag Capability) const;

	/** Convenience: resolve the subsystem for a local player (null-safe). */
	static UDocPlayerControlSubsystem* Get(const ULocalPlayer* LocalPlayer);

	//~ ULocalPlayerSubsystem
	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UObject> Provider;
};
