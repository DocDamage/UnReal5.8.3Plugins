#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DocRequestHandle.h"
#include "Interfaces/DocPlayerControlProvider.h"

/**
 * Pure arbitration of overlapping control claims (handoff 3.10 / expansion 13.4).
 *
 * - A claim covers capability C when one of its tags is C or an ancestor of C
 *   (a Doc.Control.Input claim covers Doc.Control.Input.Movement).
 * - Effective owner of C = covering claim with the highest Priority; ties go to
 *   the most recently acquired claim.
 * - Removing a claim only removes that claim; the effective state is recomputed
 *   from what remains. Nothing is snapshotted or restored.
 *
 * Game-thread only. Used by UDocReferencePlayerControlProvider and available to
 * project providers that want the same semantics.
 */
class DOCMODULARCORERUNTIME_API FDocControlClaimArbiter
{
public:
	FDocRequestHandle Add(const FDocControlClaimRequest& Request, const UObject* Scope);
	bool Remove(const FDocRequestHandle& Claim, const UObject* Scope);
	bool IsActive(const FDocRequestHandle& Claim, const UObject* Scope) const;

	/** True if any active claim covers Capability. */
	bool IsCapabilityClaimed(const FGameplayTag& Capability) const;

	/** Effective claim for Capability, or an unset handle if none. */
	FDocRequestHandle GetEffectiveClaim(const FGameplayTag& Capability) const;

	/** Drop claims whose Owner was set and has since been destroyed. Returns count removed. */
	int32 RemoveClaimsWithDeadOwners();

	/** Remove everything (e.g. provider teardown). Outstanding handles become stale. */
	void Reset() { Claims.Reset(); }

	int32 Num() const { return Claims.Num(); }

	/** Copy of an active claim's request, for diagnostics. */
	bool GetClaimRequest(const FDocRequestHandle& Claim, const UObject* Scope, FDocControlClaimRequest& OutRequest) const;

	/** Stable debug listing: "Op<id> prio=<p> caps=<tags> owner=<name> reason=<text>". */
	TArray<FString> DescribeClaims() const;

private:
	struct FClaim
	{
		FDocControlClaimRequest Request;
		uint64 Sequence = 0;
		bool bOwnerWasSet = false;
	};

	static bool Covers(const FGameplayTagContainer& ClaimCapabilities, const FGameplayTag& Capability);

	TDocHandleTable<FClaim> Claims;
	uint64 NextSequence = 1;
};
