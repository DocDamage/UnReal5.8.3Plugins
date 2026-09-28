#include "DocMaterialProfile.h"

bool UDocMaterialProfile::ValidateProfile(FString& OutError) const
{
	if (MaterialId.IsNone())
	{
		OutError = TEXT("MaterialProfile requires a non-None MaterialId.");
		return false;
	}

	if (InitialFuel < 0.0f || MaxFuel < 0.0f || InitialFuel > MaxFuel)
	{
		OutError = FString::Printf(TEXT("Invalid fuel configuration: Initial=%f, Max=%f"), InitialFuel, MaxFuel);
		return false;
	}

	if (InitialMoisture < 0.0f || InitialMoisture > 1.0f)
	{
		OutError = FString::Printf(TEXT("InitialMoisture must be in [0, 1], got %f"), InitialMoisture);
		return false;
	}

	if (InitialPhaseFraction < 0.0f || InitialPhaseFraction > 1.0f)
	{
		OutError = FString::Printf(TEXT("InitialPhaseFraction must be in [0, 1], got %f"), InitialPhaseFraction);
		return false;
	}

	if (DefaultChannelMaxValue < 0.0f)
	{
		OutError = TEXT("DefaultChannelMaxValue must be >= 0.");
		return false;
	}

	TSet<FGameplayTag> SpecChannels;
	for (const FDocExposureChannelSpec& Spec : ChannelSpecs)
	{
		if (!Spec.Channel.IsValid())
		{
			OutError = TEXT("ChannelSpecs contains an invalid channel tag.");
			return false;
		}
		if (SpecChannels.Contains(Spec.Channel))
		{
			OutError = FString::Printf(TEXT("Channel %s is declared twice."), *Spec.Channel.ToString());
			return false;
		}
		if (Spec.MaxValue < 0.0f)
		{
			OutError = FString::Printf(TEXT("Channel %s has a negative MaxValue."), *Spec.Channel.ToString());
			return false;
		}
		if (Spec.Units == EDocExposureUnits::Physical && Spec.UnitLabel.IsNone())
		{
			OutError = FString::Printf(TEXT("Physical channel %s must name its unit."), *Spec.Channel.ToString());
			return false;
		}
		SpecChannels.Add(Spec.Channel);
	}

	TSet<FName> SeenIds;
	for (const TObjectPtr<UDocMaterialReactionDefinition>& Reaction : AllowedReactions)
	{
		if (!Reaction)
		{
			OutError = TEXT("AllowedReactions contains a null entry.");
			return false;
		}
		const FString Id = Reaction->ReactionId.ToString();
		if (Reaction->ReactionId.IsNone())
		{
			OutError = TEXT("Reaction entry has NAME_None ReactionId.");
			return false;
		}
		if (SeenIds.Contains(Reaction->ReactionId))
		{
			OutError = FString::Printf(TEXT("Reaction %s appears twice."), *Id);
			return false;
		}
		SeenIds.Add(Reaction->ReactionId);

		if (!Reaction->ExposureChannel.IsValid())
		{
			OutError = FString::Printf(TEXT("Reaction %s has no exposure channel."), *Id);
			return false;
		}
		if (Reaction->ActivationThreshold < Reaction->DeactivationThreshold)
		{
			OutError = FString::Printf(TEXT("Reaction %s has ActivationThreshold < DeactivationThreshold (inverted hysteresis)."), *Id);
			return false;
		}
		if (Reaction->DwellDuration < 0.0f)
		{
			OutError = FString::Printf(TEXT("Reaction %s has a negative dwell."), *Id);
			return false;
		}
		if (Reaction->FuelConsumptionRate < 0.0f || Reaction->MoistureConsumptionRate < 0.0f
			|| Reaction->MoistureGainRate < 0.0f || Reaction->CharRate < 0.0f)
		{
			OutError = FString::Printf(TEXT("Reaction %s has a negative rate; use MoistureGainRate for wetting."), *Id);
			return false;
		}
		if (Reaction->MoistureGainRate > 0.0f && Reaction->MoistureConsumptionRate > 0.0f)
		{
			OutError = FString::Printf(TEXT("Reaction %s both adds and consumes moisture."), *Id);
			return false;
		}
		if (Reaction->bCanPropagate)
		{
			if (!Reaction->PropagationExposureChannel.IsValid())
			{
				OutError = FString::Printf(TEXT("Reaction %s propagates without a propagation channel."), *Id);
				return false;
			}
			if (Reaction->PropagationRadius <= 0.0f || Reaction->PropagationIntensity < 0.0f)
			{
				OutError = FString::Printf(TEXT("Reaction %s has an invalid propagation radius or intensity."), *Id);
				return false;
			}
		}
	}

	return true;
}

void UDocMaterialProfile::ResolveChannel(const FGameplayTag& Channel, EDocChannelCombinationRule& OutRule, float& OutMaxValue) const
{
	for (const FDocExposureChannelSpec& Spec : ChannelSpecs)
	{
		if (Spec.Channel == Channel)
		{
			OutRule = Spec.Rule;
			OutMaxValue = Spec.MaxValue;
			return;
		}
	}
	OutRule = ChannelRule;
	OutMaxValue = DefaultChannelMaxValue;
}

const UDocMaterialReactionDefinition* UDocMaterialProfile::FindReaction(FName ReactionId) const
{
	for (const TObjectPtr<UDocMaterialReactionDefinition>& Def : AllowedReactions)
	{
		if (Def && Def->ReactionId == ReactionId)
		{
			return Def.Get();
		}
	}
	return nullptr;
}

TArray<const UDocMaterialReactionDefinition*> UDocMaterialProfile::GetEvaluationOrder() const
{
	TArray<const UDocMaterialReactionDefinition*> Result;
	for (const TObjectPtr<UDocMaterialReactionDefinition>& Def : AllowedReactions)
	{
		if (Def)
		{
			Result.Add(Def.Get());
		}
	}
	Result.Sort([](const UDocMaterialReactionDefinition& A, const UDocMaterialReactionDefinition& B)
	{
		if (A.IsWettingClass() != B.IsWettingClass())
		{
			return A.IsWettingClass();
		}
		if (A.Priority != B.Priority)
		{
			return A.Priority > B.Priority;
		}
		return A.ReactionId.LexicalLess(B.ReactionId);
	});
	return Result;
}
