#include "DocWeatherSubsystem.h"
#include "DocWeatherLog.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocWeatherSubsystem)

namespace DocWeatherTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weather, "Weather", "DocWeather categories");
	UE_DEFINE_GAMEPLAY_TAG(Clear, "Weather.Clear");
	UE_DEFINE_GAMEPLAY_TAG(Cloudy, "Weather.Cloudy");
	UE_DEFINE_GAMEPLAY_TAG(Fog, "Weather.Fog");
	UE_DEFINE_GAMEPLAY_TAG(Rain, "Weather.Rain");
	UE_DEFINE_GAMEPLAY_TAG(Rain_Light, "Weather.Rain.Light");
	UE_DEFINE_GAMEPLAY_TAG(Rain_Heavy, "Weather.Rain.Heavy");
	UE_DEFINE_GAMEPLAY_TAG(Storm, "Weather.Storm");
	UE_DEFINE_GAMEPLAY_TAG(Thunderstorm, "Weather.Thunderstorm");
	UE_DEFINE_GAMEPLAY_TAG(Snow, "Weather.Snow");
	UE_DEFINE_GAMEPLAY_TAG(Snow_Light, "Weather.Snow.Light");
	UE_DEFINE_GAMEPLAY_TAG(Snow_Heavy, "Weather.Snow.Heavy");
	UE_DEFINE_GAMEPLAY_TAG(Wind, "Weather.Wind");
	UE_DEFINE_GAMEPLAY_TAG(Wind_High, "Weather.Wind.High");
	UE_DEFINE_GAMEPLAY_TAG(Custom, "Weather.Custom");
}

// ---------------------------------------------------------------------------
// State / assets
// ---------------------------------------------------------------------------

namespace DocWeatherPrivate
{
	void Check(TArray<FString>& Errors, bool bOk, const TCHAR* Field)
	{
		if (!bOk) { Errors.Add(FString::Printf(TEXT("%s out of range"), Field)); }
	}
	bool In(float V, float Min, float Max) { return FMath::IsFinite(V) && V >= Min && V <= Max; }
}

bool FDocWeatherState::Validate(TArray<FString>& OutErrors) const
{
	using namespace DocWeatherPrivate;
	const int32 Before = OutErrors.Num();
	Check(OutErrors, In(CloudCoverage, 0, 1), TEXT("CloudCoverage"));
	Check(OutErrors, In(CloudDensity, 0, 10), TEXT("CloudDensity"));
	Check(OutErrors, In(Precipitation, 0, 1), TEXT("Precipitation"));
	Check(OutErrors, In(FogDensity, 0, 10), TEXT("FogDensity"));
	Check(OutErrors, FMath::IsFinite(FogHeight), TEXT("FogHeight"));
	Check(OutErrors, In(WindSpeed, 0, 100), TEXT("WindSpeed"));
	Check(OutErrors, FMath::IsFinite(WindDirection.X) && FMath::IsFinite(WindDirection.Y), TEXT("WindDirection"));
	Check(OutErrors, In(Temperature, -80, 60), TEXT("Temperature"));
	Check(OutErrors, In(Humidity, 0, 1), TEXT("Humidity"));
	Check(OutErrors, In(Wetness, 0, 1), TEXT("Wetness"));
	Check(OutErrors, In(SnowAmount, 0, 1), TEXT("SnowAmount"));
	Check(OutErrors, In(LightningPerMinute, 0, 120), TEXT("LightningPerMinute"));
	Check(OutErrors, In(ThunderChancePerLightning, 0, 1), TEXT("ThunderChancePerLightning"));
	Check(OutErrors, FMath::IsFinite(Visibility) && Visibility > 0.f && Visibility <= UnlimitedVisibility, TEXT("Visibility"));
	Check(OutErrors, In(AmbientLightMultiplier, 0, 4), TEXT("AmbientLightMultiplier"));
	if (Precipitation > 0.f && PrecipitationType == EDocPrecipitationType::None)
	{
		OutErrors.Add(TEXT("Precipitation > 0 needs a PrecipitationType"));
	}
	return OutErrors.Num() == Before;
}

FVector2D FDocWeatherState::SafeWindDirection() const
{
	return WindDirection.IsNearlyZero() ? FVector2D(1, 0) : WindDirection.GetSafeNormal();
}

bool UDocWeatherProfile::ValidateProfile(TArray<FString>& OutErrors) const
{
	const int32 Before = OutErrors.Num();
	if (ProfileId.IsNone()) { OutErrors.Add(TEXT("ProfileId required")); }
	if (MinDuration < 0.f || MaxDuration < MinDuration) { OutErrors.Add(TEXT("Duration range invalid")); }
	State.Validate(OutErrors);
	return OutErrors.Num() == Before;
}

FDocSystemResult UDocWeatherSchedule::ValidateSchedule() const
{
	if (Entries.Num() == 0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Schedule has no entries"));
	}
	double Total = 0.0;
	for (const FDocWeatherScheduleEntry& Entry : Entries)
	{
		TArray<FString> Errors;
		if (!Entry.Profile || !Entry.Profile->ValidateProfile(Errors))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Invalid profile entry: %s"), *FString::Join(Errors, TEXT("; "))));
		}
		if (!FMath::IsFinite(Entry.Weight) || Entry.Weight < 0.f || Entry.Duration < 0.f)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Weights and durations must be finite and non-negative"));
		}
		Total += Entry.Weight;
	}
	if (Mode == EDocWeatherScheduleMode::WeightedRandom && Total <= 0.0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("All weights are zero (no selectable entry)"));
	}
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Blend
// ---------------------------------------------------------------------------

FDocWeatherState UDocWeatherSubsystem::Blend(const FDocWeatherState& From, const FDocWeatherState& To, float Alpha, float CategoricalSwitchAt)
{
	FDocWeatherState Out = From;
	auto L = [Alpha](float A, float B) { return FMath::Lerp(A, B, Alpha); };
	Out.CloudCoverage = L(From.CloudCoverage, To.CloudCoverage);
	Out.CloudDensity = L(From.CloudDensity, To.CloudDensity);
	Out.Precipitation = L(From.Precipitation, To.Precipitation);
	Out.FogDensity = L(From.FogDensity, To.FogDensity);
	Out.FogHeight = L(From.FogHeight, To.FogHeight);
	Out.WindSpeed = L(From.WindSpeed, To.WindSpeed);
	Out.Temperature = L(From.Temperature, To.Temperature);
	Out.Humidity = L(From.Humidity, To.Humidity);
	Out.LightningPerMinute = L(From.LightningPerMinute, To.LightningPerMinute);
	Out.ThunderChancePerLightning = L(From.ThunderChancePerLightning, To.ThunderChancePerLightning);
	Out.AmbientLightMultiplier = L(From.AmbientLightMultiplier, To.AmbientLightMultiplier);
	// Visibility spans orders of magnitude: interpolate in log space.
	Out.Visibility = FMath::Exp(FMath::Lerp(FMath::Loge(FMath::Max(From.Visibility, 1.f)), FMath::Loge(FMath::Max(To.Visibility, 1.f)), Alpha));
	// Wind direction: shortest angle; exact antipodes turn clockwise (defined fallback).
	const FVector2D A = From.SafeWindDirection();
	const FVector2D B = To.SafeWindDirection();
	const double A0 = FMath::RadiansToDegrees(FMath::Atan2(A.Y, A.X));
	const double A1 = FMath::RadiansToDegrees(FMath::Atan2(B.Y, B.X));
	double Delta = FRotator::NormalizeAxis(A1 - A0);
	if (FMath::IsNearlyEqual(FMath::Abs(Delta), 180.0, 1.e-6)) { Delta = -180.0; }
	const double Angle = FMath::DegreesToRadians(A0 + Delta * Alpha);
	Out.WindDirection = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
	// Categorical fields switch at a declared point; never interpolated numerically.
	const bool bSwitched = Alpha >= CategoricalSwitchAt;
	Out.PrecipitationType = bSwitched ? To.PrecipitationType : From.PrecipitationType;
	Out.DominantTag = bSwitched ? To.DominantTag : From.DominantTag;
	Out.ContextTags = bSwitched ? To.ContextTags : From.ContextTags;
	if (Out.Precipitation > 0.f && Out.PrecipitationType == EDocPrecipitationType::None)
	{
		Out.PrecipitationType = To.PrecipitationType != EDocPrecipitationType::None ? To.PrecipitationType : From.PrecipitationType;
	}
	// Ground (Wetness/SnowAmount) is not interpolated: it relaxes separately.
	Out.Wetness = From.Wetness;
	Out.SnowAmount = From.SnowAmount;
	return Out;
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocWeatherSubsystem* UDocWeatherSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocWeatherSubsystem>() : nullptr;
}

bool UDocWeatherSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SelectionSeed = 0x5EED;
	SelectionRandom.Initialize(SelectionSeed);
	EventSeed = 0x0E7E;
	EventRandom.Initialize(EventSeed);
	LightningThreshold = -FMath::Loge(FMath::Max(1.e-6, (double)EventRandom.FRand()));
	Current.DominantTag = DocWeatherTags::Clear;
	LastPublished = Current;
}

void UDocWeatherSubsystem::Deinitialize()
{
	for (const TSharedPtr<IDocWeatherRenderAdapter>& Adapter : Adapters)
	{
		Adapter->ReleaseControls();
	}
	Adapters.Reset();
	Overrides.Reset();
	Queue.Reset();
	Super::Deinitialize();
}

TStatId UDocWeatherSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocWeatherSubsystem, STATGROUP_Tickables);
}

void UDocWeatherSubsystem::RegisterProfile(UDocWeatherProfile* Profile)
{
	if (Profile && !Profile->ProfileId.IsNone())
	{
		ProfilesById.Add(Profile->ProfileId, Profile);
		Referenced.AddUnique(Profile);
	}
}

void UDocWeatherSubsystem::RegisterTransition(UDocWeatherTransitionDefinition* Definition)
{
	if (Definition && !Definition->TransitionId.IsNone())
	{
		TransitionsById.Add(Definition->TransitionId, Definition);
		Referenced.AddUnique(Definition);
	}
}

// ---------------------------------------------------------------------------
// Commands and transitions
// ---------------------------------------------------------------------------

FDocSystemResult UDocWeatherSubsystem::SetWeather(UDocWeatherProfile* Profile, UDocWeatherTransitionDefinition* Definition, EDocWeatherCommandPolicy Policy, int64& OutTransitionId)
{
	OutTransitionId = 0;
	TArray<FString> Errors;
	if (!Profile || !Profile->ValidateProfile(Errors))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Profile rejected (nothing applied): %s"), *FString::Join(Errors, TEXT("; "))));
	}
	RegisterProfile(Profile);
	RegisterTransition(Definition);
	const int64 Id = NextTransitionId++;
	if (Transition.bActive)
	{
		if (Policy == EDocWeatherCommandPolicy::RejectIfTransitioning)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Conflict, TEXT("A transition is running"));
		}
		if (Policy == EDocWeatherCommandPolicy::QueueBehindCurrent)
		{
			Queue.Add(FQueued{ Profile, Definition, Id });
			Referenced.AddUnique(Profile);
			OutTransitionId = Id;
			return FDocSystemResult::MakeSuccess();
		}
	}
	if (Policy == EDocWeatherCommandPolicy::ReplaceAndPauseScheduler && Mode != EDocWeatherScheduleMode::Manual)
	{
		bSchedulerPaused = true;
	}
	const FDocSystemResult Started = StartTransition(Profile, Definition, Id);
	if (Started.IsSuccess())
	{
		OutTransitionId = Id;
	}
	return Started;
}

FDocSystemResult UDocWeatherSubsystem::StartTransition(UDocWeatherProfile* Profile, UDocWeatherTransitionDefinition* Definition, int64 Id)
{
	if (Definition)
	{
		if (Definition->Curve == EDocWeatherCurve::CurveAsset && !Definition->CurveAsset)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("CurveAsset transition without a curve"));
		}
		if (Definition->Curve == EDocWeatherCurve::Custom)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Custom transition curves need a provider"));
		}
		if (!FMath::IsFinite(Definition->Duration) || Definition->Duration < 0.f)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Invalid transition duration"));
		}
	}
	if (Transition.bActive)
	{
		// Replace: the current evaluated state (already in Current) becomes the new source. No visible jump.
		FinishTransition(EDocWeatherTransitionOutcome::Replaced);
	}
	if (CurrentProfile && CurrentProfile != Profile)
	{
		OnWeatherEnding.Broadcast(CurrentProfile->ProfileId);
	}
	Transition = FTransitionState();
	Transition.bActive = true;
	Transition.Id = Id;
	Transition.Source = Current;
	Transition.Target = Profile;
	Transition.Definition = Definition;
	Transition.Duration = Definition ? Definition->Duration : 0.0;
	Referenced.AddUnique(Profile);
	OnTransitionStarted.Broadcast(Id, EDocWeatherTransitionOutcome::Completed);
	if (Transition.Duration <= 0.0)
	{
		Current = Blend(Transition.Source, Profile->State, 1.f, 0.f);
		FinishTransition(EDocWeatherTransitionOutcome::Completed); // applied exactly once
		Publish(true);
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocWeatherSubsystem::FinishTransition(EDocWeatherTransitionOutcome Outcome)
{
	if (!Transition.bActive)
	{
		return;
	}
	const int64 Id = Transition.Id;
	UDocWeatherProfile* Target = Transition.Target;
	Transition.bActive = false;
	if (Outcome == EDocWeatherTransitionOutcome::Completed && Target)
	{
		CurrentProfile = Target;
		OnWeatherStarted.Broadcast(Target->ProfileId);
	}
	OnTransitionCompletedNative.Broadcast(Id, Outcome);
	OnTransitionCompleted.Broadcast(Id, Outcome); // exactly one terminal result per transition
	if (Outcome == EDocWeatherTransitionOutcome::Completed && Queue.Num() > 0)
	{
		const FQueued Next = Queue[0];
		Queue.RemoveAt(0);
		StartTransition(Next.Profile, Next.Definition, Next.Id);
	}
}

FDocSystemResult UDocWeatherSubsystem::CancelTransition()
{
	if (!Transition.bActive)
	{
		return FDocSystemResult::MakeNoChange();
	}
	FinishTransition(EDocWeatherTransitionOutcome::Cancelled); // holds the current evaluated state
	Queue.Reset();
	Publish(true);
	return FDocSystemResult::MakeSuccess();
}

bool UDocWeatherSubsystem::EvaluateTransition(FDocWeatherState& Out, FString& OutError) const
{
	const double Linear = Transition.Duration > 0.0 ? FMath::Clamp(Transition.Elapsed / Transition.Duration, 0.0, 1.0) : 1.0;
	float Alpha = (float)Linear;
	const UDocWeatherTransitionDefinition* Def = Transition.Definition;
	if (Def)
	{
		switch (Def->Curve)
		{
		case EDocWeatherCurve::Linear:
			break;
		case EDocWeatherCurve::Step:
			Alpha = Linear >= Def->StepAt ? 1.f : 0.f;
			break;
		case EDocWeatherCurve::CurveAsset:
			if (!Def->CurveAsset) { OutError = TEXT("Curve missing"); return false; }
			Alpha = Def->CurveAsset->GetFloatValue((float)Linear);
			if (Alpha < 0.f || Alpha > 1.f)
			{
				if (Def->Overshoot == EDocCurveOvershoot::Reject) { OutError = TEXT("Curve overshoot rejected"); return false; }
				Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
			}
			break;
		case EDocWeatherCurve::Custom:
			OutError = TEXT("Custom curve provider not installed");
			return false;
		}
	}
	Out = Blend(Transition.Source, Transition.Target->State, Alpha, Def ? Def->CategoricalSwitchAt : 0.5f);
	Out.Wetness = Current.Wetness;
	Out.SnowAmount = Current.SnowAmount;
	return true;
}

// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------

FDocSystemResult UDocWeatherSubsystem::SetSchedule(UDocWeatherSchedule* InSchedule, int32 Seed)
{
	if (!InSchedule)
	{
		Mode = EDocWeatherScheduleMode::Manual;
		Schedule = nullptr;
		return FDocSystemResult::MakeSuccess();
	}
	if (InSchedule->Mode == EDocWeatherScheduleMode::Provider && !ScheduleProvider.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Provider mode needs a schedule provider (Time/Regions/Scripted bridge)"));
	}
	if (InSchedule->Mode != EDocWeatherScheduleMode::Provider && InSchedule->Mode != EDocWeatherScheduleMode::Manual)
	{
		const FDocSystemResult Valid = InSchedule->ValidateSchedule();
		if (!Valid.IsSuccess()) { return Valid; }
	}
	Schedule = InSchedule;
	Referenced.AddUnique(InSchedule);
	Mode = InSchedule->Mode;
	bSchedulerPaused = false;
	SelectionSeed = Seed;
	SelectionRandom.Initialize(Seed);
	SelectionDraws = 0;
	SequenceIndex = -1;
	HoldRemaining = 0.0; // first selection on the next update
	for (const FDocWeatherScheduleEntry& Entry : InSchedule->Entries)
	{
		RegisterProfile(Entry.Profile);
		RegisterTransition(Entry.Transition);
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocWeatherSubsystem::SetSchedulerPaused(bool bPaused)
{
	bSchedulerPaused = bPaused;
}

UDocWeatherProfile* UDocWeatherSubsystem::PickWeighted()
{
	double Total = 0.0;
	for (const FDocWeatherScheduleEntry& E : Schedule->Entries) { Total += FMath::Max(0.f, E.Weight); }
	if (Total <= 0.0) { return nullptr; }
	const double Roll = SelectionRandom.FRand() * Total; // dedicated stream; one draw per selection
	++SelectionDraws;
	double Accumulated = 0.0;
	for (int32 i = 0; i < Schedule->Entries.Num(); ++i) // stable candidate order
	{
		Accumulated += FMath::Max(0.f, Schedule->Entries[i].Weight);
		if (Roll < Accumulated) { SequenceIndex = i; return Schedule->Entries[i].Profile; }
	}
	SequenceIndex = Schedule->Entries.Num() - 1;
	return Schedule->Entries.Last().Profile;
}

double UDocWeatherSubsystem::DrawHold(const UDocWeatherProfile* Profile, float EntryDuration)
{
	if (EntryDuration > 0.f) { return EntryDuration; }
	if (!Profile) { return 60.0; }
	++SelectionDraws;
	return FMath::Max(1.0, (double)SelectionRandom.FRandRange(Profile->MinDuration, Profile->MaxDuration));
}

void UDocWeatherSubsystem::StepScheduler(double DeltaSeconds, int32& SelectionBudget)
{
	if (Mode == EDocWeatherScheduleMode::Manual || bSchedulerPaused || !Schedule || Transition.bActive)
	{
		return;
	}
	HoldRemaining -= DeltaSeconds;
	while (HoldRemaining <= 0.0)
	{
		if (SelectionBudget <= 0)
		{
			HoldRemaining = FMath::Max(HoldRemaining, 1.0); // bounded catch-up: fast-forward, no invented history
			return;
		}
		--SelectionBudget;
		UDocWeatherProfile* Next = nullptr;
		UDocWeatherTransitionDefinition* Def = nullptr;
		float EntryDuration = 0.f;
		if (Mode == EDocWeatherScheduleMode::WeightedRandom)
		{
			Next = PickWeighted();
		}
		else if (Mode == EDocWeatherScheduleMode::TimeDuration && Schedule->Entries.Num() > 0)
		{
			SequenceIndex = (SequenceIndex + 1) % Schedule->Entries.Num();
			Next = Schedule->Entries[SequenceIndex].Profile;
		}
		else if (Mode == EDocWeatherScheduleMode::Provider && ScheduleProvider.IsValid())
		{
			Next = ScheduleProvider->PickProfile(Clock, CurrentProfile, Def);
		}
		if (Schedule->Entries.IsValidIndex(SequenceIndex) && Mode != EDocWeatherScheduleMode::Provider)
		{
			Def = Schedule->Entries[SequenceIndex].Transition;
			EntryDuration = Schedule->Entries[SequenceIndex].Duration;
		}
		HoldRemaining += DrawHold(Next, EntryDuration);
		if (Next && Next != CurrentProfile)
		{
			StartTransition(Next, Def, NextTransitionId++);
			if (Transition.bActive) { return; } // hold continues after the transition completes
		}
	}
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void UDocWeatherSubsystem::RelaxGround(double DeltaSeconds, const FDocWeatherState& Target)
{
	const UDocWeatherSettings* S = GetDefault<UDocWeatherSettings>();
	auto Relax = [DeltaSeconds](float X, float T, float RiseK, float FallK)
	{
		const double K = T > X ? RiseK : FallK;
		return (float)(T + (X - T) * FMath::Exp(-K * DeltaSeconds));
	};
	Current.Wetness = FMath::Clamp(Relax(Current.Wetness, Target.Wetness, S->WetnessRiseRate, S->WetnessDryRate), 0.f, 1.f);
	Current.SnowAmount = FMath::Clamp(Relax(Current.SnowAmount, Target.SnowAmount, S->SnowAccumulateRate, S->SnowMeltRate), 0.f, 1.f);
}

void UDocWeatherSubsystem::StepLightning(double DeltaSeconds, bool bEmitEvents)
{
	// Hazard integration: event times follow the rate regardless of update cadence.
	const double Rate = Current.LightningPerMinute / 60.0;
	if (Rate <= 0.0)
	{
		return;
	}
	LightningHazard += Rate * DeltaSeconds;
	const int32 Max = GetDefault<UDocWeatherSettings>()->MaxEventsPerUpdate;
	int32 Emitted = 0;
	while (LightningHazard >= LightningThreshold)
	{
		LightningHazard -= LightningThreshold;
		LightningThreshold = -FMath::Loge(FMath::Max(1.e-6, (double)EventRandom.FRand()));
		const bool bThunder = EventRandom.FRand() < Current.ThunderChancePerLightning;
		if (bEmitEvents && Emitted < Max)
		{
			FDocLightningEvent Event;
			Event.EventId = NextEventId++;
			Event.ClockTime = Clock;
			Event.bThunder = bThunder;
			OnLightningNative.Broadcast(Event);
			OnLightning.Broadcast(Event);
		}
		else
		{
			++DroppedEvents;
		}
		++Emitted;
	}
}

void UDocWeatherSubsystem::Publish(bool bForce)
{
	const float Threshold = GetDefault<UDocWeatherSettings>()->SignificantDelta;
	const float Delta = FMath::Max3(FMath::Abs(Current.CloudCoverage - LastPublished.CloudCoverage), FMath::Abs(Current.Precipitation - LastPublished.Precipitation),
		FMath::Max3(FMath::Abs(Current.FogDensity - LastPublished.FogDensity) / 10.f, FMath::Abs(Current.WindSpeed - LastPublished.WindSpeed) / 100.f,
			FMath::Max(FMath::Abs(Current.Wetness - LastPublished.Wetness), FMath::Abs(Current.SnowAmount - LastPublished.SnowAmount))));
	if (!bForce && Delta < Threshold && Current.DominantTag == LastPublished.DominantTag && Current.PrecipitationType == LastPublished.PrecipitationType)
	{
		return;
	}
	++Revision;
	LastPublished = Current;
	for (const TSharedPtr<IDocWeatherRenderAdapter>& Adapter : Adapters)
	{
		Adapter->ApplyGlobalState(Current);
	}
	OnWeatherChanged.Broadcast(GetCurrentProfileId());
}

void UDocWeatherSubsystem::Step(double DeltaSeconds, bool bEmitEvents)
{
	if (DeltaSeconds <= 0.0)
	{
		return; // paused
	}
	Clock += DeltaSeconds;
	double SchedulerTime = DeltaSeconds;
	if (Transition.bActive)
	{
		Transition.Elapsed += DeltaSeconds;
		FDocWeatherState Evaluated;
		FString Error;
		if (!EvaluateTransition(Evaluated, Error))
		{
			UE_LOG(LogDocWeather, Warning, TEXT("Transition %lld failed: %s"), Transition.Id, *Error);
			FinishTransition(EDocWeatherTransitionOutcome::Failed);
		}
		else
		{
			Current = Evaluated;
			SchedulerTime = 0.0;
			if (Transition.Elapsed >= Transition.Duration)
			{
				SchedulerTime = Transition.Elapsed - Transition.Duration;
				FinishTransition(EDocWeatherTransitionOutcome::Completed);
			}
		}
	}
	const UDocWeatherProfile* GroundTarget = Transition.bActive ? Transition.Target.Get() : CurrentProfile.Get();
	RelaxGround(DeltaSeconds, GroundTarget ? GroundTarget->State : Current);
	int32 Budget = GetDefault<UDocWeatherSettings>()->MaxCatchUpSelections;
	StepScheduler(SchedulerTime, Budget);
	StepLightning(DeltaSeconds, bEmitEvents);
	Publish(false);
}

FDocSystemResult UDocWeatherSubsystem::AdvanceClock(double Seconds)
{
	if (!FMath::IsFinite(Seconds))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Non-finite jump"));
	}
	if (Seconds < 0.0)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Backward weather clock jumps are not supported"));
	}
	if (Seconds == 0.0)
	{
		return FDocSystemResult::MakeNoChange();
	}
	// Bounded work: phases end at transition completion or selection; selections are capped.
	int32 Budget = GetDefault<UDocWeatherSettings>()->MaxCatchUpSelections;
	double Remaining = Seconds;
	int32 Guard = Budget * 4 + 4;
	while (Remaining > 0.0 && Guard-- > 0)
	{
		double Phase = Remaining;
		if (Transition.bActive)
		{
			Phase = FMath::Min(Remaining, FMath::Max(0.0, Transition.Duration - Transition.Elapsed) + KINDA_SMALL_NUMBER);
		}
		else if (Mode != EDocWeatherScheduleMode::Manual && !bSchedulerPaused && Schedule && Budget > 0)
		{
			Phase = FMath::Min(Remaining, FMath::Max(HoldRemaining, KINDA_SMALL_NUMBER));
		}
		Clock += Phase;
		if (Transition.bActive)
		{
			Transition.Elapsed += Phase;
			FDocWeatherState Evaluated;
			FString Error;
			if (EvaluateTransition(Evaluated, Error)) { Current = Evaluated; }
			if (Transition.Elapsed >= Transition.Duration) { FinishTransition(EDocWeatherTransitionOutcome::Completed); }
		}
		else
		{
			StepScheduler(Phase, Budget);
		}
		const UDocWeatherProfile* GroundTarget = Transition.bActive ? Transition.Target.Get() : CurrentProfile.Get();
		RelaxGround(Phase, GroundTarget ? GroundTarget->State : Current); // closed form: exact for long phases
		Remaining -= Phase;
		if (Budget <= 0 && !Transition.bActive)
		{
			// Fast-forward the rest without history.
			Clock += Remaining;
			RelaxGround(Remaining, CurrentProfile ? CurrentProfile->State : Current);
			HoldRemaining = FMath::Max(1.0, HoldRemaining - Remaining);
			Remaining = 0.0;
		}
	}
	// Skipped lightning is not replayed; restart the hazard cleanly.
	LightningHazard = 0.0;
	Publish(true);
	return FDocSystemResult::MakeSuccess();
}

// ---------------------------------------------------------------------------
// Spatial sampling
// ---------------------------------------------------------------------------

float UDocWeatherRegionOverrideComponent::InfluenceAt(const FVector& Location) const
{
	if (!bActive)
	{
		return 0.f;
	}
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(Location);
	const FVector Outside(FMath::Max(0.0, FMath::Abs(Local.X) - Extent.X), FMath::Max(0.0, FMath::Abs(Local.Y) - Extent.Y), FMath::Max(0.0, FMath::Abs(Local.Z) - Extent.Z));
	const double D = Outside.Size();
	if (D <= 0.0) { return 1.f; }
	return BlendDistance > 0.f ? (float)FMath::Clamp(1.0 - D / BlendDistance, 0.0, 1.0) : 0.f;
}

FDocWeatherState UDocWeatherSubsystem::SampleWeatherAtLocation(FVector Location) const
{
	struct FContribution { const UDocWeatherRegionOverrideComponent* O; float W; };
	TArray<FContribution> Active;
	int32 TopPriority = TNumericLimits<int32>::Lowest();
	for (const TWeakObjectPtr<UDocWeatherRegionOverrideComponent>& Weak : Overrides)
	{
		const UDocWeatherRegionOverrideComponent* O = Weak.Get();
		const float W = O ? O->InfluenceAt(Location) * O->Weight : 0.f;
		if (W > 0.f)
		{
			Active.Add({ O, W });
			TopPriority = FMath::Max(TopPriority, O->Priority);
		}
	}
	FDocWeatherState Out = Current;
	if (Active.Num() == 0)
	{
		return Out;
	}
	// Documented band: only the highest priority present applies.
	Active.RemoveAll([TopPriority](const FContribution& C) { return C.O->Priority != TopPriority; });
	Active.Sort([](const FContribution& A, const FContribution& B) { return A.O->RegionId.LexicalLess(B.O->RegionId); });

	auto BlendField = [&Active](bool FDocWeatherFieldMask::* Mask, float Global, float FDocWeatherState::* Field)
	{
		double Sum = 0.0, Value = 0.0;
		for (const FContribution& C : Active) { if (C.O->Fields.*Mask) { Sum += C.W; Value += C.W * (C.O->Values.*Field); } }
		if (Sum <= 0.0) { return Global; }
		if (Sum > 1.0) { return (float)(Value / Sum); } // normalized; no base remainder
		return (float)((1.0 - Sum) * Global + Value);
	};
	Out.CloudCoverage = BlendField(&FDocWeatherFieldMask::bClouds, Current.CloudCoverage, &FDocWeatherState::CloudCoverage);
	Out.CloudDensity = BlendField(&FDocWeatherFieldMask::bClouds, Current.CloudDensity, &FDocWeatherState::CloudDensity);
	Out.Precipitation = BlendField(&FDocWeatherFieldMask::bPrecipitation, Current.Precipitation, &FDocWeatherState::Precipitation);
	Out.FogDensity = BlendField(&FDocWeatherFieldMask::bFog, Current.FogDensity, &FDocWeatherState::FogDensity);
	Out.FogHeight = BlendField(&FDocWeatherFieldMask::bFog, Current.FogHeight, &FDocWeatherState::FogHeight);
	Out.WindSpeed = BlendField(&FDocWeatherFieldMask::bWind, Current.WindSpeed, &FDocWeatherState::WindSpeed);
	Out.Temperature = BlendField(&FDocWeatherFieldMask::bTemperature, Current.Temperature, &FDocWeatherState::Temperature);
	Out.Visibility = BlendField(&FDocWeatherFieldMask::bVisibility, Current.Visibility, &FDocWeatherState::Visibility);
	Out.AmbientLightMultiplier = BlendField(&FDocWeatherFieldMask::bLight, Current.AmbientLightMultiplier, &FDocWeatherState::AmbientLightMultiplier);

	// Categorical: the strongest contributor (then stable RegionId) at ≥ half influence.
	const FContribution* Strongest = nullptr;
	for (const FContribution& C : Active)
	{
		if (C.O->Fields.bDominantTag && (!Strongest || C.W > Strongest->W)) { Strongest = &C; }
	}
	if (Strongest && Strongest->W >= 0.5f) { Out.DominantTag = Strongest->O->Values.DominantTag; }

	// Interior exposure: local precipitation suppressed; the global state is untouched.
	float Interior = 0.f;
	for (const FContribution& C : Active) { if (C.O->bInterior) { Interior = FMath::Max(Interior, C.W); } }
	Out.Precipitation *= 1.f - Interior;
	return Out;
}

void UDocWeatherSubsystem::RegisterOverride(UDocWeatherRegionOverrideComponent* Override)
{
	if (Override) { Overrides.AddUnique(Override); }
}

void UDocWeatherSubsystem::UnregisterOverride(UDocWeatherRegionOverrideComponent* Override)
{
	Overrides.Remove(Override); // later samples recompute from the remaining sources
	Overrides.RemoveAll([](const TWeakObjectPtr<UDocWeatherRegionOverrideComponent>& W) { return !W.IsValid(); });
}

void UDocWeatherRegionOverrideComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocWeatherSubsystem* Weather = UDocWeatherSubsystem::Get(this)) { Weather->RegisterOverride(this); }
}

void UDocWeatherRegionOverrideComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocWeatherSubsystem* Weather = UDocWeatherSubsystem::Get(this)) { Weather->UnregisterOverride(this); }
	Super::EndPlay(EndPlayReason);
}

void UDocWeatherSubsystem::RegisterAdapter(TSharedPtr<IDocWeatherRenderAdapter> Adapter)
{
	if (Adapter.IsValid() && !Adapters.Contains(Adapter))
	{
		Adapters.Add(Adapter);
		Adapter->ApplyGlobalState(Current);
	}
}

void UDocWeatherSubsystem::UnregisterAdapter(TSharedPtr<IDocWeatherRenderAdapter> Adapter)
{
	if (Adapters.Remove(Adapter) > 0)
	{
		Adapter->ReleaseControls();
	}
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

FDocWeatherSaveData UDocWeatherSubsystem::CaptureState() const
{
	FDocWeatherSaveData Data;
	Data.Revision = Revision;
	Data.Clock = Clock;
	Data.Current = Current;
	Data.CurrentProfileId = GetCurrentProfileId();
	Data.bTransitionActive = Transition.bActive;
	Data.TransitionId = Transition.Id;
	Data.TransitionSource = Transition.Source;
	Data.TransitionTargetId = Transition.Target ? Transition.Target->ProfileId : NAME_None;
	Data.TransitionDefinitionId = Transition.Definition ? Transition.Definition->TransitionId : NAME_None;
	Data.TransitionElapsed = Transition.Elapsed;
	Data.TransitionDuration = Transition.Duration;
	Data.Mode = Mode;
	Data.ScheduleId = Schedule ? Schedule->ScheduleId : NAME_None;
	Data.bSchedulerPaused = bSchedulerPaused;
	Data.HoldRemaining = HoldRemaining;
	Data.SequenceIndex = SequenceIndex;
	Data.SelectionSeed = SelectionRandom.GetCurrentSeed(); // exact continuation point, not just the initial seed
	Data.SelectionDraws = SelectionDraws;
	Data.EventSeed = EventRandom.GetCurrentSeed();
	Data.LightningHazard = LightningHazard;
	Data.LightningThreshold = LightningThreshold;
	Data.NextEventId = NextEventId;
	return Data;
}

FDocSystemResult UDocWeatherSubsystem::RestoreState(const FDocWeatherSaveData& Data)
{
	if (Data.Version > 1)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unsupported, TEXT("Weather state from a newer version"));
	}
	TArray<FString> Errors;
	if (!Data.Current.Validate(Errors) || (Data.bTransitionActive && !Data.TransitionSource.Validate(Errors)))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, FString::Join(Errors, TEXT("; ")));
	}
	UDocWeatherProfile* Profile = Data.CurrentProfileId.IsNone() ? nullptr : ProfilesById.FindRef(Data.CurrentProfileId);
	UDocWeatherProfile* Target = Data.TransitionTargetId.IsNone() ? nullptr : ProfilesById.FindRef(Data.TransitionTargetId);
	UDocWeatherTransitionDefinition* Def = Data.TransitionDefinitionId.IsNone() ? nullptr : TransitionsById.FindRef(Data.TransitionDefinitionId);
	if ((!Data.CurrentProfileId.IsNone() && !Profile) || (Data.bTransitionActive && !Target) || (!Data.TransitionDefinitionId.IsNone() && !Def))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Saved profile/transition ids are not registered"));
	}
	if (!Data.ScheduleId.IsNone() && (!Schedule || Schedule->ScheduleId != Data.ScheduleId))
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound, TEXT("Saved schedule is not active; SetSchedule first"));
	}
	// Apply (no started/changed gameplay events replayed; one refreshed publish).
	Clock = Data.Clock;
	Current = Data.Current;
	CurrentProfile = Profile;
	Transition = FTransitionState();
	Transition.bActive = Data.bTransitionActive;
	Transition.Id = Data.TransitionId;
	Transition.Source = Data.TransitionSource;
	Transition.Target = Target;
	Transition.Definition = Def;
	Transition.Elapsed = Data.TransitionElapsed;
	Transition.Duration = Data.TransitionDuration;
	Mode = Data.Mode;
	bSchedulerPaused = Data.bSchedulerPaused;
	HoldRemaining = Data.HoldRemaining;
	SequenceIndex = Data.SequenceIndex;
	SelectionRandom.Initialize(Data.SelectionSeed);
	SelectionDraws = Data.SelectionDraws;
	EventRandom.Initialize(Data.EventSeed);
	LightningHazard = Data.LightningHazard;
	LightningThreshold = Data.LightningThreshold;
	NextEventId = Data.NextEventId;
	NextTransitionId = FMath::Max(NextTransitionId, Data.TransitionId + 1);
	Queue.Reset();
	Revision = FMath::Max(Revision, Data.Revision);
	LastPublished = Current;
	++Revision;
	for (const TSharedPtr<IDocWeatherRenderAdapter>& Adapter : Adapters) { Adapter->ApplyGlobalState(Current); }
	return FDocSystemResult::MakeSuccess();
}
