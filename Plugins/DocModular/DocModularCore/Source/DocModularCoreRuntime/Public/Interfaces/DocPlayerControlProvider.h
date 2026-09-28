#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/WeakObjectPtr.h"
#include "GameplayTagContainer.h"
#include "DocSystemResult.h"
#include "DocRequestHandle.h"
#include "DocPlayerControlProvider.generated.h"

class ULocalPlayer;

/** A request to take temporary ownership of some local-player control capabilities. */
USTRUCT(BlueprintType)
struct DOCMODULARCORERUNTIME_API FDocControlClaimRequest
{
	GENERATED_BODY()

	/** Explicit local player. Never implied as player index 0. Required. */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Control")
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;

	/** Who owns the claim (a session object, component...). Claims of a destroyed owner may be dropped by the provider. Required. */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Control")
	TWeakObjectPtr<UObject> Owner;

	/** Doc.Control.* capabilities requested (Camera, Input, Input.Movement, Input.Look, Pause, HUD, or project children). */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Control")
	FGameplayTagContainer Capabilities;

	/** Higher wins per capability; equal priority -> most recent claim wins. */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Control")
	int32 Priority = 0;

	/** Debug-only reason shown in diagnostics. */
	UPROPERTY(BlueprintReadWrite, Category = "Doc|Control")
	FString DebugReason;
};

UINTERFACE(MinimalAPI, BlueprintType)
class UDocPlayerControlProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Per-local-player control lease contract (handoff 3.10).
 *
 * Implemented by the consuming project's adapter (or a packaged shared adapter),
 * never by a hard-coded Character subclass. Inspection and Sequences must use the
 * same provider instance per local player.
 *
 * Required semantics:
 * - AcquireDocControl issues an owner-scoped claim; overlapping claims coexist.
 * - The effective state of each capability is computed from the remaining active
 *   claims (plus the project's own baseline). Releasing a claim recomputes; it must
 *   never restore a stale snapshot over a newer owner's camera/input/pause choice.
 * - ReleaseDocControl is idempotent; releasing an unknown/stale claim returns a
 *   non-success result and changes nothing.
 * - Missing LocalPlayer is InvalidConfiguration, not a fallback to player 0.
 */
class DOCMODULARCORERUNTIME_API IDocPlayerControlProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Control")
	FDocSystemResult AcquireDocControl(const FDocControlClaimRequest& Request, FDocRequestHandle& OutClaim);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Control")
	FDocSystemResult ReleaseDocControl(const FDocRequestHandle& Claim);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Control")
	bool IsDocControlClaimActive(const FDocRequestHandle& Claim) const;

	/**
	 * True when Claim is the effective owner of Capability right now (highest
	 * priority, then most recent, among active claims covering Capability).
	 * Consumers use this before applying camera/view-target or focus changes, so
	 * an outranked claimant never overwrites a newer owner's choice.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Doc|Control")
	bool IsDocEffectiveControlOwner(const FDocRequestHandle& Claim, FGameplayTag Capability) const;
};
