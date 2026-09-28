#include "DocAdaptiveAudioSubsystem.h"
#include "DocAdaptiveAudioLog.h"
#include "AudioDeviceHandle.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocAdaptiveAudioSubsystem)

// ---------------------------------------------------------------------------
// Tags, profile, settings
// ---------------------------------------------------------------------------

namespace DocAudioTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Audio, "Audio", "DocAdaptiveAudio channel root");
	UE_DEFINE_GAMEPLAY_TAG(Music, "Audio.Music");
	UE_DEFINE_GAMEPLAY_TAG(Ambience, "Audio.Ambience");
	UE_DEFINE_GAMEPLAY_TAG(Environment, "Audio.Environment");
	UE_DEFINE_GAMEPLAY_TAG(Tension, "Audio.Tension");
	UE_DEFINE_GAMEPLAY_TAG(Combat, "Audio.Combat");
	UE_DEFINE_GAMEPLAY_TAG(Stinger, "Audio.Stinger");
	UE_DEFINE_GAMEPLAY_TAG(UI, "Audio.UI");
	UE_DEFINE_GAMEPLAY_TAG(DialogueSupport, "Audio.DialogueSupport");
	UE_DEFINE_GAMEPLAY_TAG(Custom, "Audio.Custom");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Layer, "Audio.Layer", "Layer roles (Base, Percussion, Melody, Tension, Combat, custom children)");
}

void UDocAudioStateProfile::GatherSoundPaths(TArray<FSoftObjectPath>& Out) const
{
	for (const FDocAudioLayerRef& Ref : Layers)
	{
		if (Ref.Layer && !Ref.Layer->Sound.IsNull())
		{
			Out.AddUnique(Ref.Layer->Sound.ToSoftObjectPath());
		}
	}
	for (const TSoftObjectPtr<USoundBase>& OneShot : RandomOneShots)
	{
		if (!OneShot.IsNull())
		{
			Out.AddUnique(OneShot.ToSoftObjectPath());
		}
	}
}

UDocAdaptiveAudioSettings::UDocAdaptiveAudioSettings()
{
	auto Add = [this](const FGameplayTag& Tag, EDocAudioBlendRule Rule)
	{
		FDocAudioChannelConfig Config;
		Config.Channel = Tag;
		Config.Rule = Rule;
		Channels.Add(Config);
	};
	Add(DocAudioTags::Music, EDocAudioBlendRule::Exclusive);
	Add(DocAudioTags::Ambience, EDocAudioBlendRule::Layered);
	Add(DocAudioTags::Environment, EDocAudioBlendRule::Layered);
	Add(DocAudioTags::Tension, EDocAudioBlendRule::Exclusive);
	Add(DocAudioTags::Combat, EDocAudioBlendRule::Exclusive);
	Add(DocAudioTags::Stinger, EDocAudioBlendRule::AdditiveOneShot);
	Add(DocAudioTags::UI, EDocAudioBlendRule::AdditiveOneShot);
	Add(DocAudioTags::DialogueSupport, EDocAudioBlendRule::Layered);
	Add(DocAudioTags::Custom, EDocAudioBlendRule::Exclusive);
}

EDocAudioBlendRule UDocAdaptiveAudioSettings::GetRule(const FGameplayTag& Channel) const
{
	for (const FDocAudioChannelConfig& Config : Channels)
	{
		if (Config.Channel == Channel)
		{
			return Config.Rule;
		}
	}
	return EDocAudioBlendRule::Exclusive;
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

UDocAdaptiveAudioSubsystem* UDocAdaptiveAudioSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDocAdaptiveAudioSubsystem>() : nullptr;
}

bool UDocAdaptiveAudioSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDocAdaptiveAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Random.Initialize(0x0D0CA0D1);
	for (const FDocAudioChannelConfig& Config : GetDefault<UDocAdaptiveAudioSettings>()->Channels)
	{
		if (Config.Channel.IsValid())
		{
			GetOrAddChannel(Config.Channel);
		}
	}
}

void UDocAdaptiveAudioSubsystem::Deinitialize()
{
	bDeinitializing = true;
	for (TPair<FGameplayTag, FChannel>& Pair : Channels)
	{
		for (FPending& Pending : Pair.Value.Pending)
		{
			if (Pending.LoadHandle.IsValid())
			{
				Pending.LoadHandle->CancelHandle();
			}
		}
	}
	for (TSharedPtr<FStreamableHandle>& Load : EmitterLoads)
	{
		if (Load.IsValid()) { Load->CancelHandle(); }
	}
	if (Backend.IsValid())
	{
		Backend->StopAll(); // every voice this director owns
	}
	Channels.Reset();
	Requests.Reset();
	Emitters.Reset();
	FadingEmitterVoices.Reset();
	EmitterLoads.Reset();
	Super::Deinitialize();
}

TStatId UDocAdaptiveAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDocAdaptiveAudioSubsystem, STATGROUP_Tickables);
}

void UDocAdaptiveAudioSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UDocAdaptiveAudioSubsystem* This = CastChecked<UDocAdaptiveAudioSubsystem>(InThis);
	This->Requests.ForEachMutable([&Collector](const FDocRequestHandle&, FRequest& Request)
	{
		Collector.AddReferencedObject(Request.Profile);
	});
	for (TPair<FGameplayTag, FChannel>& Pair : This->Channels)
	{
		for (FPlaying& Playing : Pair.Value.Playing)
		{
			Collector.AddReferencedObject(Playing.Profile);
			for (FVoice& Voice : Playing.Voices) { Collector.AddReferencedObject(Voice.Sound); }
			for (TObjectPtr<USoundBase>& Sound : Playing.OneShotSounds) { Collector.AddReferencedObject(Sound); }
		}
		for (FPending& Pending : Pair.Value.Pending)
		{
			Collector.AddReferencedObject(Pending.Profile);
		}
	}
	Super::AddReferencedObjects(InThis, Collector);
}

IDocAudioPlaybackBackend& UDocAdaptiveAudioSubsystem::EnsureBackend()
{
	if (!Backend.IsValid())
	{
		UWorld* World = GetWorld();
		if (!World || World->GetNetMode() == NM_DedicatedServer)
		{
			Backend = MakeShared<FDocNullAudioBackend>(TEXT("Null (dedicated server: logical cues only)"));
		}
		else if (!World->GetAudioDevice().IsValid())
		{
			Backend = MakeShared<FDocNullAudioBackend>(TEXT("Muted (no audio device)"));
		}
		else
		{
			Backend = MakeShared<FDocNativeAudioBackend>();
		}
		UE_LOG(LogDocAdaptiveAudio, Log, TEXT("Playback backend: %s"), *Backend->Describe());
	}
	return *Backend;
}

FString UDocAdaptiveAudioSubsystem::DescribeBackend() const
{
	return Backend.IsValid() ? Backend->Describe() : FString(TEXT("(not created yet)"));
}

void UDocAdaptiveAudioSubsystem::SetPlaybackBackend(TSharedPtr<IDocAudioPlaybackBackend> InBackend)
{
	if (Backend.IsValid())
	{
		Backend->StopAll();
	}
	for (FEmitterState& Emitter : Emitters) { Emitter.Voice = 0; }
	FadingEmitterVoices.Reset();
	Backend = InBackend;
	// Voices belonged to the old backend: drop them and restart steady layers on the new one.
	for (TPair<FGameplayTag, FChannel>& Pair : Channels)
	{
		FChannel& Channel = Pair.Value;
		Channel.Playing.RemoveAll([](const FPlaying& P) { return P.bFadingOut || P.bOneShotProfile; });
		for (FPlaying& Playing : Channel.Playing)
		{
			Playing.Voices.Reset();
			if (Backend.IsValid())
			{
				for (const FDocAudioLayerRef& Ref : Playing.Profile->Layers)
				{
					if (IsLayerEnabled(Channel, Ref)) { StartLayerVoice(Channel, Playing, Ref, false, 0.f); }
				}
			}
		}
	}
}

void UDocAdaptiveAudioSubsystem::SetRandomSeed(int32 Seed)
{
	Random.Initialize(Seed);
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

UDocAdaptiveAudioSubsystem::FChannel& UDocAdaptiveAudioSubsystem::GetOrAddChannel(const FGameplayTag& Tag)
{
	if (FChannel* Existing = Channels.Find(Tag))
	{
		return *Existing;
	}
	FChannel& Channel = Channels.Add(Tag);
	Channel.Tag = Tag;
	Channel.Rule = GetDefault<UDocAdaptiveAudioSettings>()->GetRule(Tag);
	return Channel;
}

FDocAudioRequestInfo UDocAdaptiveAudioSubsystem::RequestAudioState(UDocAudioStateProfile* Profile, UObject* Owner, int32 TieBreaker)
{
	FDocAudioRequestInfo Info;
	if (bDeinitializing)
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::NotReady, TEXT("World is tearing down"));
		return Info;
	}
	if (!Profile || !Profile->Channel.IsValid())
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Profile with a valid Channel tag required"));
		return Info;
	}
	if (!Owner)
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Requests need an owner"));
		return Info;
	}
	FChannel& Channel = GetOrAddChannel(Profile->Channel);
	if (Channel.Rule == EDocAudioBlendRule::AdditiveOneShot)
	{
		// Plays once and releases itself; there is no lease to hold.
		BeginTransition(Channel, Profile, /*bOneShot*/ true, /*bIsFallback*/ false);
		FlushEvents();
		Info.Result = FDocSystemResult::MakeSuccess();
		Info.Result.Diagnostic = TEXT("One-shot channel: no lease");
		return Info;
	}
	const int32 Limit = GetDefault<UDocAdaptiveAudioSettings>()->MaxPendingRequests;
	if (Limit > 0 && Requests.Num() >= Limit)
	{
		Info.Result = FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("Audio request limit reached"));
		return Info;
	}
	FRequest Request;
	Request.Owner = Owner;
	Request.Profile = Profile;
	Request.TieBreaker = TieBreaker;
	Request.Sequence = NextSequence++;
	Info.Handle = Requests.Add(Owner, Request);
	Channel.FailedProfiles.Reset();
	Recompute(Profile->Channel);
	FlushEvents();
	Info.Result = FDocSystemResult::MakeSuccess();
	return Info;
}

FDocSystemResult UDocAdaptiveAudioSubsystem::ReleaseAudioState(FDocRequestHandle Handle, UObject* Owner)
{
	switch (Requests.Validate(Handle, Owner))
	{
	case EDocHandleStatus::Active:
		break;
	case EDocHandleStatus::WrongScope:
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Handle belongs to another owner"));
	default:
		return FDocSystemResult::MakeNoChange(TEXT("Already released"));
	}
	FRequest Removed;
	Requests.Remove(Handle, Owner, &Removed);
	if (Removed.Profile)
	{
		GetOrAddChannel(Removed.Profile->Channel).FailedProfiles.Reset();
		Recompute(Removed.Profile->Channel);
	}
	FlushEvents();
	return FDocSystemResult::MakeSuccess();
}

int32 UDocAdaptiveAudioSubsystem::ReleaseAllForOwner(UObject* Owner)
{
	TSet<FGameplayTag> Touched;
	const FObjectKey OwnerKey(Owner);
	const int32 Removed = Requests.RemoveIf([&](const FDocRequestHandle&, const FRequest& R)
	{
		if (FObjectKey(R.Owner.GetEvenIfUnreachable()) == OwnerKey)
		{
			if (R.Profile) { Touched.Add(R.Profile->Channel); }
			return true;
		}
		return false;
	});
	for (const FGameplayTag& Tag : Touched)
	{
		GetOrAddChannel(Tag).FailedProfiles.Reset();
		Recompute(Tag);
	}
	FlushEvents();
	return Removed;
}

bool UDocAdaptiveAudioSubsystem::IsRequestValid(const FRequest& Request) const
{
	if (!Request.Owner.IsValid() || !Request.Profile)
	{
		return false;
	}
	return Request.Profile->Condition.IsEmpty() || Request.Profile->Condition.Matches(GetCombinedContext());
}

void UDocAdaptiveAudioSubsystem::RecomputeAll()
{
	TArray<FGameplayTag> Tags;
	Channels.GetKeys(Tags);
	for (const FGameplayTag& Tag : Tags)
	{
		Recompute(Tag);
	}
	FlushEvents();
}

void UDocAdaptiveAudioSubsystem::Recompute(const FGameplayTag& ChannelTag)
{
	if (bDeinitializing)
	{
		return;
	}
	FChannel& Channel = GetOrAddChannel(ChannelTag);
	if (Channel.Rule == EDocAudioBlendRule::AdditiveOneShot)
	{
		return;
	}

	// Currently valid requests only: a removed/expired request is never restored just because it was underneath.
	TArray<const FRequest*> Valid;
	Requests.ForEach([&](const FDocRequestHandle&, const FRequest& R)
	{
		if (R.Profile && R.Profile->Channel == ChannelTag && IsRequestValid(R))
		{
			Valid.Add(&R);
		}
	});
	Valid.Sort([](const FRequest& A, const FRequest& B)
	{
		if (A.Profile->Priority != B.Profile->Priority) { return A.Profile->Priority > B.Profile->Priority; }
		if (A.TieBreaker != B.TieBreaker) { return A.TieBreaker > B.TieBreaker; }
		return A.Sequence > B.Sequence; // most recent
	});
	Channel.EffectiveRequests = Valid.Num();

	TArray<UDocAudioStateProfile*> Desired;
	for (const FRequest* R : Valid)
	{
		if (!Desired.Contains(R->Profile.Get()))
		{
			Desired.Add(R->Profile.Get());
		}
		if (Channel.Rule == EDocAudioBlendRule::Exclusive)
		{
			break;
		}
	}

	auto IsSteadyPlaying = [&Channel](const UDocAudioStateProfile* Profile)
	{
		return Channel.Playing.ContainsByPredicate([Profile](const FPlaying& P) { return P.Profile == Profile && !P.bFadingOut && !P.bOneShotProfile; });
	};
	auto IsPending = [&Channel](const UDocAudioStateProfile* Profile)
	{
		return Channel.Pending.ContainsByPredicate([Profile](const FPending& P) { return P.Profile == Profile && !P.bOneShot; });
	};

	// Cancel pending transitions that are no longer desired (their late completion will be ignored).
	for (int32 i = Channel.Pending.Num() - 1; i >= 0; --i)
	{
		FPending& Pending = Channel.Pending[i];
		if (!Pending.bOneShot && !Pending.bIsFallback && !Desired.Contains(Pending.Profile.Get()))
		{
			CancelPending(Channel, Pending);
			Channel.Pending.RemoveAt(i);
		}
	}

	if (Channel.Rule == EDocAudioBlendRule::Exclusive)
	{
		UDocAudioStateProfile* Best = Desired.Num() ? Desired[0] : nullptr;
		if (!Best)
		{
			bool bAnyFade = false;
			for (FPlaying& Playing : Channel.Playing)
			{
				if (!Playing.bFadingOut && !Playing.bOneShotProfile) { FadeOutPlaying(Channel, Playing); bAnyFade = true; }
			}
			for (int32 i = Channel.Pending.Num() - 1; i >= 0; --i)
			{
				if (!Channel.Pending[i].bOneShot) { CancelPending(Channel, Channel.Pending[i]); Channel.Pending.RemoveAt(i); }
			}
			if (bAnyFade) { SetState(Channel, EDocAudioTransitionState::Fading, FGameplayTag()); }
			else if (Channel.State != EDocAudioTransitionState::Idle) { SetState(Channel, EDocAudioTransitionState::Released, FGameplayTag()); }
			return;
		}
		if (IsSteadyPlaying(Best))
		{
			// Already audible (e.g. a higher request was released while this kept playing, or re-requested).
			for (int32 i = Channel.Pending.Num() - 1; i >= 0; --i)
			{
				if (!Channel.Pending[i].bOneShot) { CancelPending(Channel, Channel.Pending[i]); Channel.Pending.RemoveAt(i); }
			}
			for (FPlaying& Playing : Channel.Playing)
			{
				if (Playing.Profile != Best && !Playing.bFadingOut && !Playing.bOneShotProfile) { FadeOutPlaying(Channel, Playing); }
			}
			if (Channel.State != EDocAudioTransitionState::Playing) { SetState(Channel, EDocAudioTransitionState::Playing, Best->ProfileId); }
			return;
		}
		if (IsPending(Best) || Channel.FailedProfiles.Contains(FObjectKey(Best)))
		{
			return; // loading, or failed and handled by its failure policy until requests change
		}
		for (int32 i = Channel.Pending.Num() - 1; i >= 0; --i)
		{
			if (!Channel.Pending[i].bOneShot) { CancelPending(Channel, Channel.Pending[i]); Channel.Pending.RemoveAt(i); }
		}
		BeginTransition(Channel, Best, false, false);
		return;
	}

	// Layered
	for (FPlaying& Playing : Channel.Playing)
	{
		if (!Playing.bFadingOut && !Playing.bOneShotProfile && !Desired.Contains(Playing.Profile.Get()))
		{
			FadeOutPlaying(Channel, Playing);
		}
	}
	for (UDocAudioStateProfile* Profile : Desired)
	{
		if (!IsSteadyPlaying(Profile) && !IsPending(Profile) && !Channel.FailedProfiles.Contains(FObjectKey(Profile)))
		{
			BeginTransition(Channel, Profile, false, false);
		}
	}
	if (Desired.Num() == 0 && Channel.Pending.Num() == 0)
	{
		const bool bFading = Channel.Playing.ContainsByPredicate([](const FPlaying& P) { return P.bFadingOut; });
		if (bFading) { SetState(Channel, EDocAudioTransitionState::Fading, FGameplayTag()); }
		else if (Channel.State != EDocAudioTransitionState::Idle) { SetState(Channel, EDocAudioTransitionState::Released, FGameplayTag()); }
	}
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

void UDocAdaptiveAudioSubsystem::BeginTransition(FChannel& Channel, UDocAudioStateProfile* Profile, bool bOneShot, bool bIsFallback)
{
	const UDocAdaptiveAudioSettings* Settings = GetDefault<UDocAdaptiveAudioSettings>();
	FPending Pending;
	Pending.Id = NextPendingId++;
	Pending.Profile = Profile;
	Pending.RequestedAt = Clock;
	Pending.Deadline = Settings->LoadTimeoutSeconds > 0.f ? Clock + Settings->LoadTimeoutSeconds : 0.0;
	Pending.bOneShot = bOneShot;
	Pending.bIsFallback = bIsFallback;
	Pending.State = EDocAudioTransitionState::AssetPreloading;
	const int64 Id = Pending.Id;
	const FGameplayTag ChannelTag = Channel.Tag;
	Channel.Pending.Add(Pending);
	Channel.LastRequestedTime = Clock;
	if (!bOneShot)
	{
		SetState(Channel, EDocAudioTransitionState::Requested, Profile->ProfileId);
		SetState(Channel, EDocAudioTransitionState::AssetPreloading, Profile->ProfileId);
	}

	TArray<FSoftObjectPath> Paths;
	Profile->GatherSoundPaths(Paths);
	Paths.RemoveAll([](const FSoftObjectPath& Path) { return Path.ResolveObject() != nullptr; });
	if (Paths.Num() == 0)
	{
		OnPendingLoaded(ChannelTag, Id);
		return;
	}

	TWeakObjectPtr<UDocAdaptiveAudioSubsystem> WeakThis(this);
	auto OnDone = [WeakThis, ChannelTag, Id]()
	{
		if (UDocAdaptiveAudioSubsystem* Self = WeakThis.Get())
		{
			Self->OnPendingLoaded(ChannelTag, Id);
			Self->FlushEvents();
		}
	};
	if (LoaderOverride)
	{
		LoaderOverride(Paths, OnDone);
		return;
	}
	TSharedPtr<FStreamableHandle> Handle = Streamable.RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda(OnDone), FStreamableManager::AsyncLoadHighPriority);
	// The delegate may already have run; attach the handle only if the transition is still pending.
	if (FChannel* Still = Channels.Find(ChannelTag))
	{
		if (FPending* Found = Still->Pending.FindByPredicate([Id](const FPending& P) { return P.Id == Id; }))
		{
			Found->LoadHandle = Handle;
		}
	}
}

void UDocAdaptiveAudioSubsystem::CancelPending(FChannel& Channel, FPending& Pending)
{
	if (Pending.LoadHandle.IsValid())
	{
		Pending.LoadHandle->CancelHandle();
		Pending.LoadHandle.Reset();
	}
	if (!Pending.bOneShot && Pending.Profile)
	{
		SetState(Channel, EDocAudioTransitionState::Cancelled, Pending.Profile->ProfileId);
	}
}

void UDocAdaptiveAudioSubsystem::OnPendingLoaded(FGameplayTag ChannelTag, int64 PendingId)
{
	FChannel* Channel = Channels.Find(ChannelTag);
	if (!Channel || bDeinitializing)
	{
		return;
	}
	const int32 Index = Channel->Pending.IndexOfByPredicate([PendingId](const FPending& P) { return P.Id == PendingId; });
	if (Index == INDEX_NONE)
	{
		return; // superseded or cancelled: a late completion never resurrects an old profile
	}
	UDocAudioStateProfile* Profile = Channel->Pending[Index].Profile;

	TArray<FString> Missing;
	int32 Resolved = 0;
	for (const FDocAudioLayerRef& Ref : Profile->Layers)
	{
		if (Ref.Layer && !Ref.Layer->Sound.IsNull())
		{
			if (Ref.Layer->Sound.Get()) { ++Resolved; }
			else { Missing.Add(Ref.Layer->Sound.ToString()); }
		}
	}
	for (const TSoftObjectPtr<USoundBase>& OneShot : Profile->RandomOneShots)
	{
		if (!OneShot.IsNull() && OneShot.Get()) { ++Resolved; }
	}
	if (Missing.Num() > 0 || Resolved == 0)
	{
		FailPending(*Channel, Index, EDocAudioTransitionState::Failed,
			Missing.Num() ? FString::Printf(TEXT("Failed to load: %s"), *FString::Join(Missing, TEXT(", "))) : FString(TEXT("Profile has no playable sounds")));
		return;
	}

	if (Profile->Quantization != EDocAudioQuantization::Immediate && !Channel->Pending[Index].bOneShot)
	{
		const FString BoundaryName = StaticEnum<EDocAudioQuantization>()->GetNameStringByValue((int64)Profile->Quantization);
		if (Scheduler)
		{
			Channel->Pending[Index].State = EDocAudioTransitionState::Scheduled;
			SetState(*Channel, EDocAudioTransitionState::Scheduled, Profile->ProfileId);
			TWeakObjectPtr<UDocAdaptiveAudioSubsystem> WeakThis(this);
			if (Scheduler(Profile->Quantization, Profile, [WeakThis, ChannelTag, PendingId]()
				{
					if (UDocAdaptiveAudioSubsystem* Self = WeakThis.Get()) { Self->CommitPending(ChannelTag, PendingId); Self->FlushEvents(); }
				}))
			{
				return;
			}
		}
		Channel = Channels.Find(ChannelTag);
		const int32 Again = Channel ? Channel->Pending.IndexOfByPredicate([PendingId](const FPending& P) { return P.Id == PendingId; }) : INDEX_NONE;
		if (Again == INDEX_NONE)
		{
			return;
		}
		if (GetDefault<UDocAdaptiveAudioSettings>()->bRejectUnavailableQuantization)
		{
			FailPending(*Channel, Again, EDocAudioTransitionState::Failed,
				FString::Printf(TEXT("%s boundary requested but no scheduling capability is installed"), *BoundaryName));
			return;
		}
		Channel->LastDiagnostic = FString::Printf(TEXT("%s boundary unavailable (no scheduler); applied immediately"), *BoundaryName);
	}
	CommitPending(ChannelTag, PendingId);
}

void UDocAdaptiveAudioSubsystem::CommitPending(FGameplayTag ChannelTag, int64 PendingId)
{
	FChannel* Channel = Channels.Find(ChannelTag);
	if (!Channel || bDeinitializing)
	{
		return;
	}
	const int32 Index = Channel->Pending.IndexOfByPredicate([PendingId](const FPending& P) { return P.Id == PendingId; });
	if (Index == INDEX_NONE)
	{
		return; // cancelled while scheduled
	}
	UDocAudioStateProfile* Profile = Channel->Pending[Index].Profile;
	const bool bOneShot = Channel->Pending[Index].bOneShot;
	Channel->Pending.RemoveAt(Index);

	if (!bOneShot && Channel->Rule == EDocAudioBlendRule::Exclusive)
	{
		for (FPlaying& Playing : Channel->Playing)
		{
			if (!Playing.bFadingOut && !Playing.bOneShotProfile) { FadeOutPlaying(*Channel, Playing); }
		}
	}
	StartProfile(*Channel, Profile, bOneShot);
	Channel->LastActualTime = Clock;
	if (!bOneShot)
	{
		SetState(*Channel, EDocAudioTransitionState::Playing, Profile->ProfileId);
	}
}

void UDocAdaptiveAudioSubsystem::FailPending(FChannel& Channel, int32 PendingIndex, EDocAudioTransitionState FailState, const FString& Why)
{
	FPending Pending = Channel.Pending[PendingIndex];
	Channel.Pending.RemoveAt(PendingIndex);
	if (Pending.LoadHandle.IsValid())
	{
		Pending.LoadHandle->CancelHandle();
	}
	UDocAudioStateProfile* Profile = Pending.Profile;
	Channel.LastDiagnostic = Why;
	UE_LOG(LogDocAdaptiveAudio, Warning, TEXT("%s: profile %s %s: %s"), *Channel.Tag.ToString(), *GetNameSafe(Profile),
		*StaticEnum<EDocAudioTransitionState>()->GetNameStringByValue((int64)FailState), *Why);
	if (Pending.bOneShot || !Profile)
	{
		return;
	}
	Channel.FailedProfiles.Add(FObjectKey(Profile));
	SetState(Channel, FailState, Profile->ProfileId);

	EDocAudioFailurePolicy Policy = Profile->FailurePolicy;
	if (Policy == EDocAudioFailurePolicy::UseFallback && (Pending.bIsFallback || !Profile->FallbackProfile))
	{
		Policy = EDocAudioFailurePolicy::Silence; // a failing fallback does not chain further
	}
	switch (Policy)
	{
	case EDocAudioFailurePolicy::KeepPrevious:
		break; // previous valid profile keeps playing untouched
	case EDocAudioFailurePolicy::UseFallback:
	{
		UDocAudioStateProfile* Fallback = Profile->FallbackProfile;
		const bool bAlready = Channel.Playing.ContainsByPredicate([Fallback](const FPlaying& P) { return P.Profile == Fallback && !P.bFadingOut; });
		if (!bAlready && !Channel.FailedProfiles.Contains(FObjectKey(Fallback)))
		{
			BeginTransition(Channel, Fallback, false, /*bIsFallback*/ true);
		}
		break;
	}
	case EDocAudioFailurePolicy::Silence:
		for (FPlaying& Playing : Channel.Playing)
		{
			if (!Playing.bFadingOut && !Playing.bOneShotProfile) { FadeOutPlaying(Channel, Playing); }
		}
		break;
	}
}

bool UDocAdaptiveAudioSubsystem::IsLayerEnabled(const FChannel& Channel, const FDocAudioLayerRef& Ref) const
{
	if (!Ref.Layer)
	{
		return false;
	}
	if (const bool* Override = Channel.LayerOverrides.Find(Ref.Layer->LayerTag))
	{
		return *Override;
	}
	return Ref.bEnabledByDefault;
}

int32 UDocAdaptiveAudioSubsystem::StartLayerVoice(FChannel& Channel, FPlaying& Playing, const FDocAudioLayerRef& Ref, bool bOneShot, float FadeIn)
{
	const UDocAdaptiveAudioSettings* Settings = GetDefault<UDocAdaptiveAudioSettings>();
	USoundBase* Sound = Ref.Layer ? Ref.Layer->Sound.Get() : nullptr;
	if (!Sound)
	{
		return 0;
	}
	if (GetTotalVoiceCount() >= Settings->MaxTotalVoices || Playing.Voices.Num() >= Playing.Profile->MaxVoices)
	{
		Channel.LastDiagnostic = FString::Printf(TEXT("Voice limit reached; layer %s not started"), *Ref.Layer->LayerTag.ToString());
		return 0;
	}
	IDocAudioPlaybackBackend::FPlayParams Params;
	Params.Sound = Sound;
	Params.Volume = Ref.Layer->Volume;
	Params.FadeInSeconds = FadeIn;
	Params.StartTime = Ref.Layer->StartOffsetSeconds;
	Params.Curve = Playing.Profile->FadeCurve;
	const int32 Id = EnsureBackend().Play(GetWorld(), Params);
	if (Id == 0)
	{
		Channel.LastDiagnostic = FString::Printf(TEXT("Playback refused %s"), *GetNameSafe(Sound));
		return 0;
	}
	FVoice Voice;
	Voice.Id = Id;
	Voice.Layer = Ref.Layer->LayerTag;
	Voice.Sound = Sound;
	Voice.Volume = Ref.Layer->Volume;
	Voice.bLoop = !bOneShot && Ref.Layer->bLoop;
	Voice.bOneShot = bOneShot;
	Playing.Voices.Add(Voice);
	return Id;
}

void UDocAdaptiveAudioSubsystem::StartProfile(FChannel& Channel, UDocAudioStateProfile* Profile, bool bOneShot)
{
	FPlaying NewPlaying;
	NewPlaying.Profile = Profile;
	NewPlaying.bOneShotProfile = bOneShot;
	for (const TSoftObjectPtr<USoundBase>& OneShot : Profile->RandomOneShots)
	{
		if (USoundBase* Sound = OneShot.Get()) { NewPlaying.OneShotSounds.Add(Sound); }
	}
	int32 Wanted = 0;
	int32 Started = 0;
	for (const FDocAudioLayerRef& Ref : Profile->Layers)
	{
		if (IsLayerEnabled(Channel, Ref))
		{
			++Wanted;
			Started += StartLayerVoice(Channel, NewPlaying, Ref, bOneShot, bOneShot ? 0.f : Profile->FadeInSeconds) != 0 ? 1 : 0;
		}
	}
	if (bOneShot && Profile->Layers.Num() == 0 && NewPlaying.OneShotSounds.Num() > 0 && ConsumeOneShotBudget())
	{
		// Stinger defined only by a random set: play one of them once.
		IDocAudioPlaybackBackend::FPlayParams Params;
		Params.Sound = NewPlaying.OneShotSounds[Random.RandRange(0, NewPlaying.OneShotSounds.Num() - 1)];
		Params.Volume = Profile->OneShotVolume;
		if (const int32 Id = EnsureBackend().Play(GetWorld(), Params))
		{
			FVoice Voice;
			Voice.Id = Id;
			Voice.Sound = Params.Sound;
			Voice.bOneShot = true;
			NewPlaying.Voices.Add(Voice);
		}
	}
	if (Wanted > 0 && Started == 0)
	{
		UE_LOG(LogDocAdaptiveAudio, Warning, TEXT("%s: no layer of %s could start (%s)"), *Channel.Tag.ToString(), *GetNameSafe(Profile), *Channel.LastDiagnostic);
	}
	NewPlaying.NextOneShotTime = Clock + Random.FRandRange(Profile->OneShotMinIntervalSeconds, FMath::Max(Profile->OneShotMinIntervalSeconds, Profile->OneShotMaxIntervalSeconds));
	if (bOneShot && NewPlaying.Voices.Num() == 0)
	{
		return; // nothing audible; nothing to track
	}
	Channel.Playing.Add(MoveTemp(NewPlaying));
}

void UDocAdaptiveAudioSubsystem::FadeOutPlaying(FChannel& Channel, FPlaying& Playing)
{
	Playing.bFadingOut = true;
	const float Seconds = Playing.Profile ? Playing.Profile->FadeOutSeconds : 0.f;
	for (FVoice& Voice : Playing.Voices)
	{
		if (Voice.FadeDeadline <= 0.0)
		{
			EnsureBackend().FadeOutAndStop(Voice.Id, Seconds, Playing.Profile ? Playing.Profile->FadeCurve : EDocAudioFadeCurve::Linear);
			Voice.FadeDeadline = Clock + FMath::Max(Seconds, 0.f) + KINDA_SMALL_NUMBER;
		}
	}
}

void UDocAdaptiveAudioSubsystem::SetState(FChannel& Channel, EDocAudioTransitionState NewState, const FGameplayTag& ProfileId)
{
	Channel.State = NewState;
	QueuedEvents.Add(FQueuedEvent{ Channel.Tag, ProfileId, NewState });
}

void UDocAdaptiveAudioSubsystem::FlushEvents()
{
	if (bFlushingEvents)
	{
		return; // the outer flush drains events queued by handlers
	}
	TGuardValue<bool> Guard(bFlushingEvents, true);
	for (int32 i = 0; i < QueuedEvents.Num(); ++i)
	{
		const FQueuedEvent Event = QueuedEvents[i];
		OnChannelStateChangedNative.Broadcast(Event.Channel, Event.Profile, Event.State);
		OnChannelStateChanged.Broadcast(Event.Channel, Event.Profile, Event.State);
	}
	QueuedEvents.Reset();
}

// ---------------------------------------------------------------------------
// Context and layers
// ---------------------------------------------------------------------------

void UDocAdaptiveAudioSubsystem::SetContextTags(FName Source, const FGameplayTagContainer& Tags)
{
	const FGameplayTagContainer* Existing = ContextSources.Find(Source);
	if (Existing && *Existing == Tags)
	{
		return;
	}
	ContextSources.Add(Source, Tags);
	RecomputeAll();
}

void UDocAdaptiveAudioSubsystem::ClearContextSource(FName Source)
{
	if (ContextSources.Remove(Source) > 0)
	{
		RecomputeAll();
	}
}

void UDocAdaptiveAudioSubsystem::SetListenerContextTags(ULocalPlayer* Player, const FGameplayTagContainer& Tags)
{
	if (!Player)
	{
		return;
	}
	ListenerContexts.Add(Player, Tags);
	RecomputeAll();
}

FGameplayTagContainer UDocAdaptiveAudioSubsystem::GetCombinedContext() const
{
	FGameplayTagContainer Combined;
	for (const TPair<FName, FGameplayTagContainer>& Pair : ContextSources)
	{
		Combined.AppendTags(Pair.Value);
	}
	const bool bPrimaryOnly = GetDefault<UDocAdaptiveAudioSettings>()->ListenerPolicy == EDocAudioListenerPolicy::SharedOutputPrimaryListener;
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	const ULocalPlayer* Primary = GI ? GI->GetFirstGamePlayer() : nullptr;
	for (const TPair<TWeakObjectPtr<ULocalPlayer>, FGameplayTagContainer>& Pair : ListenerContexts)
	{
		const ULocalPlayer* Player = Pair.Key.Get();
		if (Player && (!bPrimaryOnly || Player == Primary))
		{
			Combined.AppendTags(Pair.Value);
		}
	}
	return Combined;
}

FDocSystemResult UDocAdaptiveAudioSubsystem::SetLayerEnabled(FGameplayTag ChannelTag, FGameplayTag LayerTag, bool bEnabled)
{
	if (!ChannelTag.IsValid() || !LayerTag.IsValid())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidInput, TEXT("Channel and layer tags required"));
	}
	FChannel& Channel = GetOrAddChannel(ChannelTag);
	const bool* Existing = Channel.LayerOverrides.Find(LayerTag);
	if (Existing && *Existing == bEnabled)
	{
		return FDocSystemResult::MakeNoChange();
	}
	Channel.LayerOverrides.Add(LayerTag, bEnabled);
	ON_SCOPE_EXIT { FlushEvents(); };
	for (FPlaying& Playing : Channel.Playing)
	{
		if (Playing.bFadingOut || Playing.bOneShotProfile)
		{
			continue;
		}
		for (const FDocAudioLayerRef& Ref : Playing.Profile->Layers)
		{
			if (!Ref.Layer || Ref.Layer->LayerTag != LayerTag)
			{
				continue;
			}
			if (bEnabled)
			{
				const bool bHasVoice = Playing.Voices.ContainsByPredicate([&](const FVoice& V) { return V.Layer == LayerTag && V.FadeDeadline <= 0.0; });
				if (!bHasVoice) { StartLayerVoice(Channel, Playing, Ref, false, Playing.Profile->FadeInSeconds); }
			}
			else
			{
				for (FVoice& Voice : Playing.Voices)
				{
					if (Voice.Layer == LayerTag && Voice.FadeDeadline <= 0.0)
					{
						EnsureBackend().FadeOutAndStop(Voice.Id, Playing.Profile->FadeOutSeconds, Playing.Profile->FadeCurve);
						Voice.FadeDeadline = Clock + Playing.Profile->FadeOutSeconds + KINDA_SMALL_NUMBER;
					}
				}
			}
		}
	}
	return FDocSystemResult::MakeSuccess();
}

void UDocAdaptiveAudioSubsystem::ClearLayerOverrides(FGameplayTag ChannelTag)
{
	if (FChannel* Channel = Channels.Find(ChannelTag))
	{
		TArray<FGameplayTag> Keys;
		Channel->LayerOverrides.GetKeys(Keys);
		Channel->LayerOverrides.Reset();
		// Re-apply defaults to what is playing.
		for (FPlaying& Playing : Channel->Playing)
		{
			if (Playing.bFadingOut || Playing.bOneShotProfile) { continue; }
			for (const FDocAudioLayerRef& Ref : Playing.Profile->Layers)
			{
				if (!Ref.Layer || !Keys.Contains(Ref.Layer->LayerTag)) { continue; }
				const bool bHasVoice = Playing.Voices.ContainsByPredicate([&](const FVoice& V) { return V.Layer == Ref.Layer->LayerTag && V.FadeDeadline <= 0.0; });
				if (Ref.bEnabledByDefault && !bHasVoice) { StartLayerVoice(*Channel, Playing, Ref, false, Playing.Profile->FadeInSeconds); }
				if (!Ref.bEnabledByDefault && bHasVoice)
				{
					for (FVoice& Voice : Playing.Voices)
					{
						if (Voice.Layer == Ref.Layer->LayerTag && Voice.FadeDeadline <= 0.0)
						{
							EnsureBackend().FadeOutAndStop(Voice.Id, Playing.Profile->FadeOutSeconds, Playing.Profile->FadeCurve);
							Voice.FadeDeadline = Clock + Playing.Profile->FadeOutSeconds + KINDA_SMALL_NUMBER;
						}
					}
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void UDocAdaptiveAudioSubsystem::TickDirector(float DeltaSeconds)
{
	if (bDeinitializing)
	{
		return;
	}
	Clock += FMath::Max(0.f, DeltaSeconds);
	PruneDeadOwners();

	TArray<FGameplayTag> Tags;
	Channels.GetKeys(Tags);
	for (const FGameplayTag& Tag : Tags)
	{
		FChannel* Channel = Channels.Find(Tag);
		if (!Channel) { continue; }
		for (int32 i = Channel->Pending.Num() - 1; i >= 0; --i)
		{
			const FPending& Pending = Channel->Pending[i];
			if (Pending.State == EDocAudioTransitionState::AssetPreloading && Pending.Deadline > 0.0 && Clock > Pending.Deadline)
			{
				FailPending(*Channel, i, EDocAudioTransitionState::TimedOut, TEXT("Asset preload timed out"));
				Channel = Channels.Find(Tag);
				if (!Channel) { break; }
				i = FMath::Min(i, Channel->Pending.Num());
			}
		}
		if (Channel)
		{
			TickVoices(*Channel);
			TickOneShots(*Channel);
		}
	}
	TickEmitters();
	FlushEvents();
}

void UDocAdaptiveAudioSubsystem::PruneDeadOwners()
{
	TSet<FGameplayTag> Touched;
	Requests.RemoveIf([&Touched](const FDocRequestHandle&, const FRequest& R)
	{
		if (!R.Owner.IsValid())
		{
			if (R.Profile) { Touched.Add(R.Profile->Channel); }
			return true;
		}
		return false;
	});
	for (const FGameplayTag& Tag : Touched)
	{
		GetOrAddChannel(Tag).FailedProfiles.Reset();
		Recompute(Tag);
	}
}

void UDocAdaptiveAudioSubsystem::TickVoices(FChannel& Channel)
{
	IDocAudioPlaybackBackend& Playback = EnsureBackend();
	bool bHadFading = false;
	for (int32 p = Channel.Playing.Num() - 1; p >= 0; --p)
	{
		FPlaying& Playing = Channel.Playing[p];
		bHadFading |= Playing.bFadingOut;
		for (int32 v = Playing.Voices.Num() - 1; v >= 0; --v)
		{
			FVoice& Voice = Playing.Voices[v];
			if (Voice.FadeDeadline > 0.0 && Clock >= Voice.FadeDeadline)
			{
				Playback.Stop(Voice.Id);
				Playing.Voices.RemoveAt(v);
				continue;
			}
			if (!Playback.IsPlaying(Voice.Id))
			{
				Playback.Stop(Voice.Id); // release backend resources for finished sources
				if (Voice.bLoop && Voice.FadeDeadline <= 0.0 && !Playing.bFadingOut)
				{
					// Loop rule: restart a non-looping asset that finished (phase continuity not promised).
					IDocAudioPlaybackBackend::FPlayParams Params;
					Params.Sound = Voice.Sound;
					Params.Volume = Voice.Volume;
					Voice.Id = Playback.Play(GetWorld(), Params);
					if (Voice.Id != 0) { continue; }
				}
				Playing.Voices.RemoveAt(v);
			}
		}
		if (Playing.Voices.Num() == 0 && (Playing.bFadingOut || Playing.bOneShotProfile))
		{
			Channel.Playing.RemoveAt(p);
		}
	}
	if (bHadFading && Channel.Pending.Num() == 0 && Channel.State == EDocAudioTransitionState::Fading
		&& !Channel.Playing.ContainsByPredicate([](const FPlaying& P) { return P.bFadingOut; }))
	{
		SetState(Channel, EDocAudioTransitionState::Released, FGameplayTag());
	}
}

bool UDocAdaptiveAudioSubsystem::ConsumeOneShotBudget()
{
	const int32 Max = GetDefault<UDocAdaptiveAudioSettings>()->MaxOneShotsPerSecond;
	RecentOneShots.RemoveAll([this](double T) { return T < Clock - 1.0; });
	if (Max > 0 && RecentOneShots.Num() >= Max)
	{
		return false;
	}
	RecentOneShots.Add(Clock);
	return true;
}

void UDocAdaptiveAudioSubsystem::TickOneShots(FChannel& Channel)
{
	const int32 MaxTotal = GetDefault<UDocAdaptiveAudioSettings>()->MaxTotalVoices;
	for (FPlaying& Playing : Channel.Playing)
	{
		if (Playing.bFadingOut || Playing.bOneShotProfile || Playing.OneShotSounds.Num() == 0 || Clock < Playing.NextOneShotTime)
		{
			continue;
		}
		const UDocAudioStateProfile* Profile = Playing.Profile;
		Playing.NextOneShotTime = Clock + Random.FRandRange(Profile->OneShotMinIntervalSeconds, FMath::Max(Profile->OneShotMinIntervalSeconds, Profile->OneShotMaxIntervalSeconds));
		const int32 Active = Playing.Voices.FilterByPredicate([](const FVoice& V) { return V.bOneShot; }).Num();
		if (Active >= Profile->MaxConcurrentOneShots || Playing.Voices.Num() >= Profile->MaxVoices || GetTotalVoiceCount() >= MaxTotal || !ConsumeOneShotBudget())
		{
			continue;
		}
		IDocAudioPlaybackBackend::FPlayParams Params;
		Params.Sound = Playing.OneShotSounds[Random.RandRange(0, Playing.OneShotSounds.Num() - 1)];
		Params.Volume = Profile->OneShotVolume;
		if (const int32 Id = EnsureBackend().Play(GetWorld(), Params))
		{
			FVoice Voice;
			Voice.Id = Id;
			Voice.Sound = Params.Sound;
			Voice.Volume = Params.Volume;
			Voice.bOneShot = true;
			Playing.Voices.Add(Voice);
		}
	}
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

int32 UDocAdaptiveAudioSubsystem::GetTotalVoiceCount() const
{
	int32 Count = 0;
	for (const TPair<FGameplayTag, FChannel>& Pair : Channels)
	{
		for (const FPlaying& Playing : Pair.Value.Playing)
		{
			Count += Playing.Voices.Num();
		}
	}
	return Count + GetActiveEmitterCount() + FadingEmitterVoices.Num();
}

FDocAudioChannelDebug UDocAdaptiveAudioSubsystem::GetChannelDebug(FGameplayTag ChannelTag) const
{
	FDocAudioChannelDebug Debug;
	Debug.Channel = ChannelTag;
	const FChannel* Channel = Channels.Find(ChannelTag);
	if (!Channel)
	{
		Debug.Rule = GetDefault<UDocAdaptiveAudioSettings>()->GetRule(ChannelTag);
		return Debug;
	}
	Debug.Rule = Channel->Rule;
	Debug.State = Channel->State;
	Debug.EffectiveRequests = Channel->EffectiveRequests;
	Debug.LastRequestedTime = Channel->LastRequestedTime;
	Debug.LastActualTime = Channel->LastActualTime;
	Debug.LastDiagnostic = Channel->LastDiagnostic;
	for (const FPlaying& Playing : Channel->Playing)
	{
		Debug.VoiceCount += Playing.Voices.Num();
		if (!Playing.bFadingOut && Playing.Profile)
		{
			Debug.PlayingProfiles.AddUnique(Playing.Profile->ProfileId);
			for (const FVoice& Voice : Playing.Voices)
			{
				if (Voice.Layer.IsValid() && Voice.FadeDeadline <= 0.0) { Debug.EnabledLayers.AddUnique(Voice.Layer); }
			}
		}
	}
	for (const FPending& Pending : Channel->Pending)
	{
		if (!Pending.bOneShot && Pending.Profile) { Debug.PendingProfile = Pending.Profile->ProfileId; }
	}
	return Debug;
}

TArray<FGameplayTag> UDocAdaptiveAudioSubsystem::GetKnownChannels() const
{
	TArray<FGameplayTag> Out;
	Channels.GetKeys(Out);
	return Out;
}

// ---------------------------------------------------------------------------
// Emitters
// ---------------------------------------------------------------------------

void UDocAdaptiveAudioSubsystem::RegisterEmitter(UDocAudioEmitterComponent* Emitter)
{
	if (Emitter && !Emitters.ContainsByPredicate([Emitter](const FEmitterState& S) { return S.Emitter.Get() == Emitter; }))
	{
		Emitters.Add(FEmitterState{ Emitter, 0 });
		NextEmitterEvaluation = Clock; // prioritized reevaluation
	}
}

void UDocAdaptiveAudioSubsystem::UnregisterEmitter(UDocAudioEmitterComponent* Emitter)
{
	for (int32 i = Emitters.Num() - 1; i >= 0; --i)
	{
		if (Emitters[i].Emitter.Get() == Emitter || !Emitters[i].Emitter.IsValid())
		{
			if (Emitters[i].Voice != 0 && Backend.IsValid())
			{
				Backend->Stop(Emitters[i].Voice);
			}
			Emitters.RemoveAt(i);
		}
	}
}

bool UDocAdaptiveAudioSubsystem::IsEmitterVoiceActive(const UDocAudioEmitterComponent* Emitter) const
{
	return Emitters.ContainsByPredicate([Emitter](const FEmitterState& S) { return S.Emitter.Get() == Emitter && S.Voice != 0; });
}

int32 UDocAdaptiveAudioSubsystem::GetActiveEmitterCount() const
{
	int32 Count = 0;
	for (const FEmitterState& State : Emitters)
	{
		Count += State.Voice != 0 ? 1 : 0;
	}
	return Count;
}

void UDocAdaptiveAudioSubsystem::TickEmitters()
{
	IDocAudioPlaybackBackend& Playback = EnsureBackend();
	for (int32 i = FadingEmitterVoices.Num() - 1; i >= 0; --i)
	{
		if (Clock >= FadingEmitterVoices[i].Value || !Playback.IsPlaying(FadingEmitterVoices[i].Key))
		{
			Playback.Stop(FadingEmitterVoices[i].Key);
			FadingEmitterVoices.RemoveAt(i);
		}
	}
	EmitterLoads.RemoveAll([](const TSharedPtr<FStreamableHandle>& H) { return !H.IsValid() || H->HasLoadCompleted() || H->WasCanceled(); });

	if (Emitters.Num() == 0 || Clock < NextEmitterEvaluation)
	{
		return;
	}
	const UDocAdaptiveAudioSettings* Settings = GetDefault<UDocAdaptiveAudioSettings>();
	NextEmitterEvaluation = Clock + Settings->EmitterEvaluationIntervalSeconds;

	TArray<FVector> Listeners = ListenerOverride;
	if (Listeners.Num() == 0)
	{
		if (UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				if (PC && PC->IsLocalController())
				{
					FVector Location;
					FRotator Rotation;
					PC->GetPlayerViewPoint(Location, Rotation);
					Listeners.Add(Location);
				}
			}
		}
	}

	struct FCandidate { int32 Index; int32 Priority; double DistSq; };
	TArray<FCandidate> Candidates;
	for (int32 i = Emitters.Num() - 1; i >= 0; --i)
	{
		UDocAudioEmitterComponent* Emitter = Emitters[i].Emitter.Get();
		if (!Emitter)
		{
			if (Emitters[i].Voice) { Playback.Stop(Emitters[i].Voice); }
			Emitters.RemoveAt(i);
			continue;
		}
		if (Emitters[i].Voice && !Playback.IsPlaying(Emitters[i].Voice))
		{
			Playback.Stop(Emitters[i].Voice);
			Emitters[i].Voice = 0;
		}
	}
	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		const UDocAudioEmitterComponent* Emitter = Emitters[i].Emitter.Get();
		double Best = TNumericLimits<double>::Max();
		for (const FVector& L : Listeners)
		{
			Best = FMath::Min(Best, FVector::DistSquared(L, Emitter->GetComponentLocation()));
		}
		if (!Emitter->Sound.IsNull() && Best <= FMath::Square((double)Emitter->MaxDistance))
		{
			Candidates.Add({ i, Emitter->Priority, Best });
		}
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.Priority != B.Priority ? A.Priority > B.Priority : A.DistSq < B.DistSq;
	});

	TSet<int32> Selected;
	for (int32 c = 0; c < Candidates.Num() && Selected.Num() < Settings->MaxActiveEmitters; ++c)
	{
		Selected.Add(Candidates[c].Index);
	}
	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		FEmitterState& State = Emitters[i];
		UDocAudioEmitterComponent* Emitter = State.Emitter.Get();
		if (!Selected.Contains(i))
		{
			if (State.Voice)
			{
				Playback.FadeOutAndStop(State.Voice, Emitter->FadeSeconds, EDocAudioFadeCurve::Linear);
				FadingEmitterVoices.Add(TPair<int32, double>(State.Voice, Clock + Emitter->FadeSeconds + KINDA_SMALL_NUMBER));
				State.Voice = 0;
			}
			continue;
		}
		if (State.Voice)
		{
			continue;
		}
		USoundBase* Sound = Emitter->Sound.Get();
		if (!Sound)
		{
			EmitterLoads.Add(Streamable.RequestAsyncLoad(Emitter->Sound.ToSoftObjectPath(), FStreamableDelegate()));
			NextEmitterEvaluation = Clock; // try again next tick
			continue;
		}
		if (GetTotalVoiceCount() >= Settings->MaxTotalVoices)
		{
			break;
		}
		IDocAudioPlaybackBackend::FPlayParams Params;
		Params.Sound = Sound;
		Params.Volume = Emitter->Volume;
		Params.FadeInSeconds = Emitter->FadeSeconds;
		Params.AttachTo = Emitter;
		State.Voice = Playback.Play(GetWorld(), Params);
	}
}

// ---------------------------------------------------------------------------
// Emitter component
// ---------------------------------------------------------------------------

bool UDocAudioEmitterComponent::IsEmitterActive() const
{
	// Active = currently holding a voice from the director's budget.
	const UDocAdaptiveAudioSubsystem* Audio = UDocAdaptiveAudioSubsystem::Get(this);
	if (!Audio)
	{
		return false;
	}
	return Audio->IsEmitterVoiceActive(this);
}

void UDocAudioEmitterComponent::OnRegister()
{
	Super::OnRegister();
	if (!IsTemplate())
	{
		if (UDocAdaptiveAudioSubsystem* Audio = UDocAdaptiveAudioSubsystem::Get(this))
		{
			Audio->RegisterEmitter(this);
		}
	}
}

void UDocAudioEmitterComponent::OnUnregister()
{
	if (!IsTemplate())
	{
		if (UDocAdaptiveAudioSubsystem* Audio = UDocAdaptiveAudioSubsystem::Get(this))
		{
			Audio->UnregisterEmitter(this);
		}
	}
	Super::OnUnregister();
}

void UDocAudioEmitterComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDocAdaptiveAudioSubsystem* Audio = UDocAdaptiveAudioSubsystem::Get(this))
	{
		Audio->RegisterEmitter(this);
	}
}

void UDocAudioEmitterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDocAdaptiveAudioSubsystem* Audio = UDocAdaptiveAudioSubsystem::Get(this))
	{
		Audio->UnregisterEmitter(this);
	}
	Super::EndPlay(EndPlayReason);
}
