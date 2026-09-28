#include "DocAcousticSpaceComponent.h"
#include "DocAcousticSpaceSubsystem.h"
#include "Engine/World.h"

UDocAcousticSpaceComponent::UDocAcousticSpaceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UDocAcousticSpaceComponent::ContainsLocation(const FVector& Location) const
{
	if (!BoundsBox.IsValid)
	{
		return false;
	}
	return BoundsBox.IsInsideOrOn(Location);
}

bool UDocAcousticSpaceComponent::ContainsLocationWithHysteresis(const FVector& Location, float HysteresisMargin) const
{
	if (!BoundsBox.IsValid)
	{
		return false;
	}

	FBox ExpandedBounds = BoundsBox.ExpandBy(FMath::Max(0.0f, HysteresisMargin));
	return ExpandedBounds.IsInsideOrOn(Location);
}

void UDocAcousticSpaceComponent::OnRegister()
{
	Super::OnRegister();
	if (UWorld* World = GetWorld())
	{
		if (UDocAcousticSpaceSubsystem* Subsystem = World->GetSubsystem<UDocAcousticSpaceSubsystem>())
		{
			const FDocSystemResult Result = Subsystem->RegisterSpace(this);
			LastRegistrationError = Result.IsSuccess() ? FString() : Result.ToString();
		}
	}
}

void UDocAcousticSpaceComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDocAcousticSpaceSubsystem* Subsystem = World->GetSubsystem<UDocAcousticSpaceSubsystem>())
		{
			Subsystem->UnregisterSpace(this);
		}
	}
	Super::OnUnregister();
}
