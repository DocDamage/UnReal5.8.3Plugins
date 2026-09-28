#include "DocControlClaimArbiter.h"

FDocRequestHandle FDocControlClaimArbiter::Add(const FDocControlClaimRequest& Request, const UObject* Scope)
{
	FClaim Claim;
	Claim.Request = Request;
	Claim.Sequence = NextSequence++;
	Claim.bOwnerWasSet = !Request.Owner.IsExplicitlyNull();
	return Claims.Add(Scope, MoveTemp(Claim));
}

bool FDocControlClaimArbiter::Remove(const FDocRequestHandle& Claim, const UObject* Scope)
{
	return Claims.Remove(Claim, Scope);
}

bool FDocControlClaimArbiter::IsActive(const FDocRequestHandle& Claim, const UObject* Scope) const
{
	return Claims.Validate(Claim, Scope) == EDocHandleStatus::Active;
}

bool FDocControlClaimArbiter::Covers(const FGameplayTagContainer& ClaimCapabilities, const FGameplayTag& Capability)
{
	// Capability.MatchesTag(T) is true when Capability == T or Capability is a child of T.
	return Capability.IsValid() && Capability.MatchesAny(ClaimCapabilities);
}

bool FDocControlClaimArbiter::IsCapabilityClaimed(const FGameplayTag& Capability) const
{
	bool bFound = false;
	Claims.ForEach([&](const FDocRequestHandle&, const FClaim& Claim)
	{
		bFound = bFound || Covers(Claim.Request.Capabilities, Capability);
	});
	return bFound;
}

FDocRequestHandle FDocControlClaimArbiter::GetEffectiveClaim(const FGameplayTag& Capability) const
{
	FDocRequestHandle Best;
	int32 BestPriority = MIN_int32;
	uint64 BestSequence = 0;
	bool bHaveBest = false;

	Claims.ForEach([&](const FDocRequestHandle& Handle, const FClaim& Claim)
	{
		if (!Covers(Claim.Request.Capabilities, Capability))
		{
			return;
		}
		const bool bBetter = !bHaveBest
			|| Claim.Request.Priority > BestPriority
			|| (Claim.Request.Priority == BestPriority && Claim.Sequence > BestSequence);
		if (bBetter)
		{
			bHaveBest = true;
			Best = Handle;
			BestPriority = Claim.Request.Priority;
			BestSequence = Claim.Sequence;
		}
	});
	return Best;
}

int32 FDocControlClaimArbiter::RemoveClaimsWithDeadOwners()
{
	return Claims.RemoveIf([](const FDocRequestHandle&, const FClaim& Claim)
	{
		return Claim.bOwnerWasSet && !Claim.Request.Owner.IsValid();
	});
}

bool FDocControlClaimArbiter::GetClaimRequest(const FDocRequestHandle& Claim, const UObject* Scope, FDocControlClaimRequest& OutRequest) const
{
	if (const FClaim* Found = Claims.Find(Claim, Scope))
	{
		OutRequest = Found->Request;
		return true;
	}
	return false;
}

TArray<FString> FDocControlClaimArbiter::DescribeClaims() const
{
	TArray<FString> Lines;
	Claims.ForEach([&](const FDocRequestHandle& Handle, const FClaim& Claim)
	{
		const UObject* Owner = Claim.Request.Owner.Get();
		Lines.Add(FString::Printf(TEXT("%s prio=%d seq=%llu caps=%s owner=%s reason=%s"),
			*Handle.ToString(),
			Claim.Request.Priority,
			static_cast<unsigned long long>(Claim.Sequence),
			*Claim.Request.Capabilities.ToStringSimple(),
			Owner ? *Owner->GetName() : TEXT("<none>"),
			*Claim.Request.DebugReason));
	});
	Lines.Sort();
	return Lines;
}
