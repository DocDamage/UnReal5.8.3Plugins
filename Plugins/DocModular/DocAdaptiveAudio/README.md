# DocAdaptiveAudio

A logical director for music, ambience, environmental layers, random one-shots and stingers over native Unreal playback (Rev 2 handoff, Section 10). It does not replace Unreal's DSP, Sound Classes, Mixes or Submixes.

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Director | `UDocAdaptiveAudioSubsystem` (world) |
| Assets | `UDocAudioStateProfile`, `UDocAudioLayerDefinition` (soft `USoundBase`: Sound Wave, Sound Cue, or a MetaSound source through its sound interface) |
| Playback | `FDocNativeAudioBackend` (2D or attached `UAudioComponent`s with fades). `FDocNullAudioBackend` is used on dedicated servers and on hosts with no audio device |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | Quartz scheduling bridge (AUD-05), Audio Modulation and typed MetaSound controls, Regions/Time/Events/Weather adapters |

## Channels

Channels are identified by tags: `Audio.Music`, `Ambience`, `Environment`, `Tension`, `Combat`, `Stinger`, `UI`, `DialogueSupport` and `Custom`. Each channel has its own blend rule, set in Project Settings:

| Rule | Behaviour |
|---|---|
| Exclusive | One effective profile. Picked by highest priority, then higher tie-breaker, then most recent |
| Layered | Every valid profile plays, bounded by voice limits |
| AdditiveOneShot | Each request plays once and releases itself, so there is no lease |

Channels are independent. A high-priority music request never silences dialogue support or UI audio.

## Requests and transitions

- `RequestAudioState(Profile, Owner)` returns an owner-scoped lease.
  - Only **currently valid** requests are considered: the owner is alive and the profile's `Condition` matches the combined context.
  - Releasing an override does not bring back a request that was removed or expired underneath it.
  - Dead owners are pruned every tick.
- Transitions go `Requested → AssetPreloading → Scheduled → Playing/Fading → Released`, or end as `Failed`, `Cancelled` or `TimedOut`.
  - The previous profile keeps playing until the new one is loaded.
  - Each pending transition has an id, so a superseded or cancelled load or schedule never starts old audio.
- If a profile fails to load, its `FailurePolicy` decides what happens:
  - **KeepPrevious** (default): the current audio carries on.
  - **UseFallback**: switches to `FallbackProfile`. A failing fallback does not chain to another fallback.
  - **Silence**: the channel fades out.

  A failed profile is not retried until that channel's requests change.
- Musical boundaries (Beat, Bar, Phrase, Custom) need a scheduler installed through `SetScheduler`, which the Quartz bridge provides.
  - Without a scheduler, the transition is applied immediately and the channel diagnostic says so.
  - Setting `bRejectUnavailableQuantization` makes the transition fail instead.
  - A game-thread timer is never presented as sample-accurate.
- Layer definitions carry tempo, meter, loop length and start offset.
  - `bLoop` restarts a non-looping asset when it finishes.
  - Phase continuity after a stop or eviction is not promised.

## Context, layers, one-shots, emitters

- **Context tags.** Adapters supply context with `SetContextTags(Source, Tags)`, and per local player with `SetListenerContextTags`.
  - The split-screen policy is one shared output mix.
  - With `SharedOutputCombinedState` (default), every local player's tags are combined.
  - With `SharedOutputPrimaryListener`, only the first local player's tags count.
- **Layers.** `SetLayerEnabled(Channel, LayerTag, bEnabled)` fades one layer role in or out on a channel. The setting applies to future profiles too.
- **Random one-shots.** Each profile has its own interval and maximum concurrency. A global one-shots-per-second budget also applies. Selection uses a seeded `FRandomStream`, so the choice of sounds is reproducible; the rendered audio may still differ between machines.
- **Voice limits.** Each profile has a `MaxVoices` limit, and there is also a global `MaxTotalVoices` limit.
- **Emitters.** `UDocAudioEmitterComponent` is a positional ambient emitter.
  - Only the highest-priority, nearest emitters within `MaxDistance` of a listener play, up to `MaxActiveEmitters`.
  - An emitter that loses its slot fades out.
- **Ownership.** The director stops only the voices it owns. It never restores a global Sound Mix snapshot.
- **Debug.** `GetChannelDebug` reports the effective requests, the playing and pending profiles, enabled layers, voice count, requested versus actual time, and the last diagnostic.

## Tests

The tests are under `Doc.Audio.*`: PriorityAndChannels, RemovedRequestsNotRestored, SupersededAndFailedLoads, LayersOneShotsLimits, Quantization, StingersAndContext, TeardownAndChurn and EmitterBudget.

They use the logical-only backend and a deferred test loader, so they cover the director's behaviour but not audible output. The following still need a cooked build with audio enabled:

- AUD-04: Sound Wave, Sound Cue and MetaSound playback.
- AUD-05: Quartz timing.
- AUD-07: split-screen listener behaviour on a real device.
