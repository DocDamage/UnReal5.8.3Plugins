#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DocControlClaimArbiter.h"
#include "Interfaces/DocPlayerControlProvider.h"
#include "DocReferencePlayerControlProvider.generated.h"

class ULocalPlayer;
class APlayerController;

/**
 * Packaged reference implementation of IDocPlayerControlProvider for one local player.
 *
 * Effects on the local player's current APlayerController (recomputed after every
 * acquire/release; nothing is snapshotted):
 * - Doc.Control.Input / Input.Movement: holds one SetIgnoreMoveInput(true) while claimed
 *   (the engine counts ignore requests, so other systems' requests compose).
 * - Doc.Control.Input / Input.Look: holds one SetIgnoreLookInput(true) while claimed.
 * - Doc.Control.Cursor: bShowMouseCursor = true while claimed, else BaselineShowMouseCursor.
 * - Doc.Control.Pause: SetPause(true) while claimed; SetPause(false) only if this provider paused.
 * - Doc.Control.Camera: arbitration only. The effective owner sets its own view
 *   target. When the last camera claim is released, the view target returns to the
 *   controller's *current* pawn (if bRestoreViewTargetToPawn), not to a snapshot.
 * - Doc.Control.HUD / Focus: arbitration only; presenters query IsDocEffectiveControlOwner.
 */
UCLASS(BlueprintType)
class DOCMODULARCORERUNTIME_API UDocReferencePlayerControlProvider : public UObject, public IDocPlayerControlProvider
{
	GENERATED_BODY()

public:
	void Bind(ULocalPlayer* InLocalPlayer);

	/** Re-apply effective state (e.g. after the player controller changed). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Control")
	void ReapplyEffectiveState();

	/** Release every claim and undo this provider's applied effects. */
	void ShutdownProvider();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Control")
	bool BaselineShowMouseCursor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Control")
	bool bRestoreViewTargetToPawn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Doc|Control", meta = (ClampMin = "0.0"))
	float RestoreViewTargetBlendTime = 0.25f;

	UFUNCTION(BlueprintPure, Category = "Doc|Control")
	TArray<FString> DescribeClaims() const { return Arbiter.DescribeClaims(); }

	//~ IDocPlayerControlProvider
	virtual FDocSystemResult AcquireDocControl_Implementation(const FDocControlClaimRequest& Request, FDocRequestHandle& OutClaim) override;
	virtual FDocSystemResult ReleaseDocControl_Implementation(const FDocRequestHandle& Claim) override;
	virtual bool IsDocControlClaimActive_Implementation(const FDocRequestHandle& Claim) const override;
	virtual bool IsDocEffectiveControlOwner_Implementation(const FDocRequestHandle& Claim, FGameplayTag Capability) const override;

private:
	APlayerController* GetController() const;

	TWeakObjectPtr<ULocalPlayer> LocalPlayer;
	FDocControlClaimArbiter Arbiter;

	// Effects this provider currently holds on AppliedController.
	TWeakObjectPtr<APlayerController> AppliedController;
	bool bAppliedMoveBlock = false;
	bool bAppliedLookBlock = false;
	bool bAppliedPause = false;
	bool bHadCameraClaim = false;
};
