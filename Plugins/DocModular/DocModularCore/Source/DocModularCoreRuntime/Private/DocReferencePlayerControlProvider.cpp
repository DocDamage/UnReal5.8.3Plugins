#include "DocReferencePlayerControlProvider.h"
#include "DocCoreTags.h"
#include "DocCoreLog.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocReferencePlayerControlProvider)

void UDocReferencePlayerControlProvider::Bind(ULocalPlayer* InLocalPlayer)
{
	LocalPlayer = InLocalPlayer;
}

APlayerController* UDocReferencePlayerControlProvider::GetController() const
{
	const ULocalPlayer* Player = LocalPlayer.Get();
	return Player ? Player->GetPlayerController(Player->GetWorld()) : nullptr;
}

FDocSystemResult UDocReferencePlayerControlProvider::AcquireDocControl_Implementation(const FDocControlClaimRequest& Request, FDocRequestHandle& OutClaim)
{
	OutClaim.Invalidate();

	if (!LocalPlayer.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("Reference control provider is not bound to a local player"));
	}
	if (Request.LocalPlayer.Get() != LocalPlayer.Get())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			TEXT("Control claim targets a different (or no) local player; this provider serves exactly one"));
	}
	if (Request.Owner.IsExplicitlyNull() || !Request.Owner.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Control claim requires a live Owner"));
	}
	if (Request.Capabilities.IsEmpty())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Control claim requests no capabilities"));
	}

	Arbiter.RemoveClaimsWithDeadOwners();
	OutClaim = Arbiter.Add(Request, this);
	ReapplyEffectiveState();
	return FDocSystemResult::MakeSuccess(OutClaim.GetOperationId());
}

FDocSystemResult UDocReferencePlayerControlProvider::ReleaseDocControl_Implementation(const FDocRequestHandle& Claim)
{
	if (!Arbiter.Remove(Claim, this))
	{
		return FDocSystemResult::MakeNoChange(TEXT("Claim already released or unknown"), Claim.GetOperationId());
	}
	Arbiter.RemoveClaimsWithDeadOwners();
	ReapplyEffectiveState();
	return FDocSystemResult::MakeSuccess(Claim.GetOperationId());
}

bool UDocReferencePlayerControlProvider::IsDocControlClaimActive_Implementation(const FDocRequestHandle& Claim) const
{
	return Arbiter.IsActive(Claim, this);
}

bool UDocReferencePlayerControlProvider::IsDocEffectiveControlOwner_Implementation(const FDocRequestHandle& Claim, FGameplayTag Capability) const
{
	return Claim.IsSet() && Arbiter.IsActive(Claim, this) && Arbiter.GetEffectiveClaim(Capability) == Claim;
}

void UDocReferencePlayerControlProvider::ReapplyEffectiveState()
{
	APlayerController* PC = GetController();

	// If the controller changed, drop effects we held on the old one first.
	APlayerController* OldPC = AppliedController.Get();
	if (OldPC && OldPC != PC)
	{
		if (bAppliedMoveBlock) { OldPC->SetIgnoreMoveInput(false); }
		if (bAppliedLookBlock) { OldPC->SetIgnoreLookInput(false); }
		if (bAppliedPause) { OldPC->SetPause(false); }
		OldPC->bShowMouseCursor = BaselineShowMouseCursor;
		bAppliedMoveBlock = bAppliedLookBlock = bAppliedPause = false;
	}
	if (!OldPC && AppliedController.IsStale())
	{
		// Old controller destroyed: its counters died with it.
		bAppliedMoveBlock = bAppliedLookBlock = bAppliedPause = false;
	}
	AppliedController = PC;
	if (!PC)
	{
		return;
	}

	const bool bWantMoveBlock = Arbiter.IsCapabilityClaimed(DocCoreTags::Control_Input_Movement);
	const bool bWantLookBlock = Arbiter.IsCapabilityClaimed(DocCoreTags::Control_Input_Look);
	const bool bWantCursor = Arbiter.IsCapabilityClaimed(DocCoreTags::Control_Cursor);
	const bool bWantPause = Arbiter.IsCapabilityClaimed(DocCoreTags::Control_Pause);
	const bool bHasCamera = Arbiter.IsCapabilityClaimed(DocCoreTags::Control_Camera);

	if (bWantMoveBlock != bAppliedMoveBlock)
	{
		PC->SetIgnoreMoveInput(bWantMoveBlock);
		bAppliedMoveBlock = bWantMoveBlock;
	}
	if (bWantLookBlock != bAppliedLookBlock)
	{
		PC->SetIgnoreLookInput(bWantLookBlock);
		bAppliedLookBlock = bWantLookBlock;
	}

	PC->bShowMouseCursor = bWantCursor ? true : BaselineShowMouseCursor;

	if (bWantPause && !bAppliedPause)
	{
		bAppliedPause = PC->SetPause(true);
	}
	else if (!bWantPause && bAppliedPause)
	{
		PC->SetPause(false);
		bAppliedPause = false;
	}

	if (bHadCameraClaim && !bHasCamera && bRestoreViewTargetToPawn)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, RestoreViewTargetBlendTime);
		}
	}
	bHadCameraClaim = bHasCamera;
}

void UDocReferencePlayerControlProvider::ShutdownProvider()
{
	Arbiter.Reset();
	ReapplyEffectiveState();
}
