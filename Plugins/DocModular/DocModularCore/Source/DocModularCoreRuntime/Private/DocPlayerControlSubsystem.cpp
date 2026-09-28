#include "DocPlayerControlSubsystem.h"
#include "DocReferencePlayerControlProvider.h"
#include "DocCoreTags.h"
#include "DocCoreLog.h"
#include "Engine/LocalPlayer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocPlayerControlSubsystem)

UDocPlayerControlSubsystem* UDocPlayerControlSubsystem::Get(const ULocalPlayer* LocalPlayer)
{
	return LocalPlayer ? LocalPlayer->GetSubsystem<UDocPlayerControlSubsystem>() : nullptr;
}

FDocSystemResult UDocPlayerControlSubsystem::SetControlProvider(UObject* ProviderObject)
{
	if (!ProviderObject)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("SetControlProvider: null provider"));
	}
	if (!ProviderObject->Implements<UDocPlayerControlProvider>())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput,
			FString::Printf(TEXT("SetControlProvider: %s does not implement IDocPlayerControlProvider"), *ProviderObject->GetName()));
	}
	if (Provider && Provider != ProviderObject)
	{
		if (UDocReferencePlayerControlProvider* OldReference = Cast<UDocReferencePlayerControlProvider>(Provider))
		{
			OldReference->ShutdownProvider();
		}
		UE_LOG(LogDocCore, Warning, TEXT("Control provider for %s replaced; claims held by the previous provider are not migrated."),
			*GetNameSafe(GetLocalPlayer()));
	}
	Provider = ProviderObject;
	return FDocSystemResult::MakeSuccess();
}

UObject* UDocPlayerControlSubsystem::UseReferenceProvider()
{
	if (UDocReferencePlayerControlProvider* Existing = Cast<UDocReferencePlayerControlProvider>(Provider))
	{
		return Existing;
	}
	UDocReferencePlayerControlProvider* Reference = NewObject<UDocReferencePlayerControlProvider>(this);
	Reference->Bind(GetLocalPlayer());
	SetControlProvider(Reference);
	return Reference;
}

void UDocPlayerControlSubsystem::ClearControlProvider()
{
	if (UDocReferencePlayerControlProvider* Reference = Cast<UDocReferencePlayerControlProvider>(Provider))
	{
		Reference->ShutdownProvider();
	}
	Provider = nullptr;
}

FDocSystemResult UDocPlayerControlSubsystem::AcquireControl(FDocControlClaimRequest Request, FDocRequestHandle& OutClaim)
{
	OutClaim.Invalidate();
	if (!Provider)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable,
			TEXT("No IDocPlayerControlProvider installed for this local player"), DocCoreTags::Error_Unavailable);
	}
	if (Request.LocalPlayer.IsExplicitlyNull())
	{
		Request.LocalPlayer = GetLocalPlayer();
	}
	return IDocPlayerControlProvider::Execute_AcquireDocControl(Provider, Request, OutClaim);
}

FDocSystemResult UDocPlayerControlSubsystem::ReleaseControl(const FDocRequestHandle& Claim)
{
	if (!Provider)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable,
			TEXT("No IDocPlayerControlProvider installed for this local player"), DocCoreTags::Error_Unavailable);
	}
	return IDocPlayerControlProvider::Execute_ReleaseDocControl(Provider, Claim);
}

bool UDocPlayerControlSubsystem::IsClaimActive(const FDocRequestHandle& Claim) const
{
	return Provider && IDocPlayerControlProvider::Execute_IsDocControlClaimActive(Provider, Claim);
}

bool UDocPlayerControlSubsystem::IsEffectiveOwner(const FDocRequestHandle& Claim, FGameplayTag Capability) const
{
	return Provider && IDocPlayerControlProvider::Execute_IsDocEffectiveControlOwner(Provider, Claim, Capability);
}

void UDocPlayerControlSubsystem::Deinitialize()
{
	ClearControlProvider();
	Super::Deinitialize();
}

void UDocPlayerControlSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);
	if (UDocReferencePlayerControlProvider* Reference = Cast<UDocReferencePlayerControlProvider>(Provider))
	{
		Reference->ReapplyEffectiveState();
	}
}
