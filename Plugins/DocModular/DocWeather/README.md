# DocWeather

Renderer-independent environmental state: manual control, weighted and duration scheduling, interruptible transitions, regional sampling and rate-independent lightning events (Modules 11–20 handoff, Section 4; milestone 5.3).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| State | `UDocWeatherSubsystem` (world). Computes state only. It never searches the world for a sky Blueprint or owns lights |
| Data | `UDocWeatherProfile`, `UDocWeatherTransitionDefinition`, `UDocWeatherSchedule`; `UDocWeatherRegionOverrideComponent` |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | SkyAtmosphere, VolumetricClouds, Niagara, material and audio adapters; Time, Regions and Save bridges; the replicated transport actor (WEA-09); editor preview/graph/debugger tools |

## State and units

| Fields | Contract |
|---|---|
| CloudCoverage, Humidity, Wetness, SnowAmount | Fraction in [0,1] |
| Precipitation | Intensity in [0,1] plus `PrecipitationType` (None/Rain/Snow/Sleet/Hail) |
| CloudDensity, FogDensity | Authored control in [0,10]. Not a physical density |
| FogHeight | Metres above the map's declared reference |
| WindSpeed / WindDirection | m/s in [0,100], plus a normalized XY direction. A zero direction falls back to +X |
| Temperature | °C in [-80,60] |
| Visibility | Metres, > 0. `UnlimitedVisibility` (1e7) means unlimited |
| AmbientLightMultiplier | In [0,4] |
| LightningPerMinute, ThunderChancePerLightning | A rate, plus the probability that thunder accompanies a strike. Never a chance per frame |

`Validate()` rejects out-of-range values. A rejected profile is never partially applied. Mixed weather uses a declared `DominantTag` plus context tags; the category is never interpolated.

## Transitions

- **Curves.** Linear, CurveAsset (overshoot is clamped or rejected per definition), Step, and Custom. Custom needs a provider and is Unsupported without one.
- **Zero duration.** The new state is applied immediately, exactly once.
- **Replacing a transition.** The current evaluated snapshot becomes the new source, so nothing visibly jumps. The replaced transition ends with `Replaced`.
- **Other policies.** Each command specifies one of:
  - replace and pause the scheduler
  - replace and keep the scheduler running
  - queue behind the current transition
  - reject while a transition is running
- **Terminal results.** Every transition reports exactly one terminal result: Completed, Replaced, Cancelled or Failed. Late callbacks cannot fire for a superseded transition.
- **Interpolation.** Wind direction takes the shortest angle; exact opposites turn clockwise. Visibility is interpolated in log space. Precipitation type and dominant tag switch at `CategoricalSwitchAt`.
- **Wetness and snow** are not interpolated. They relax toward the profile's targets with `x(t+dt) = target + (x - target)·exp(-k·dt)`, using separate rise and fall rates. This is a game-state model, not hydrology.

## Scheduling and randomness

- **Modes.** `Manual` (no hidden scheduler), `WeightedRandom` and `TimeDuration` are built in. `Provider` covers TimeDriven, RegionDriven and Scripted; it needs a bridge and returns Unavailable without one.
- **Weighted selection.** Candidates are drawn in a stable order (the array order) from a dedicated `FRandomStream`. Weights that are all zero, negative or non-finite give `InvalidConfiguration`.
- **Save and restore of randomness.** The save stores the **current** stream state, not just the seed, so a restored game continues the same random sequence.
- **Lightning.** Lightning uses a separate stream and hazard integration, so event rates don't depend on update cadence. Events per update are capped.
- **Clock.**
  - The weather clock follows world ticks, so it respects pause and time dilation.
  - `AdvanceClock` performs a bounded forward catch-up, capped at `MaxCatchUpSelections`. Past that it fast-forwards without inventing history. Skipped lightning is not replayed.
  - Backward jumps return Unsupported.

## Regions

`SampleWeatherAtLocation` works as follows:

1. Collect the overrides whose influence at the location is non-zero. Influence is full inside the box extent and fades over `BlendDistance`.
2. Keep only the **highest priority band** present.
3. Blend each masked field: the global value takes the remaining weight, and the override weights are normalized when their sum exceeds 1.
4. Take categorical fields from the strongest override at ≥ 0.5 influence; ties are broken by `RegionId`.
5. Apply interior overrides, which suppress precipitation locally. The global state keeps its weather, so one player inside and one outside get different samples.

Removing an override simply recomputes the result from the remaining sources.

## Adapters, events, persistence

- **Adapters.** `IDocWeatherRenderAdapter` adapters are registered explicitly. On unregister, an adapter releases **only its own** controls.
- **Events.** `OnWeatherStarted`, `OnWeatherEnding`, `OnTransitionStarted`, `OnTransitionCompleted` (with outcome) and `OnLightning`. `OnWeatherChanged` fires only when the evaluated state changes by more than `SignificantDelta`, together with a revision bump, not every frame.
- **Persistence.** `CaptureState` / `RestoreState` (`FDocWeatherSaveData`) cover the global state, the transition source/target/elapsed time, the scheduler mode, hold time and random streams, lightning accumulators and the revision. Restore validates versions and ids before applying anything, and emits no gameplay events.
- **Dedicated servers.** No renderer is involved, so the full state runs on dedicated servers.

## Tests

`Doc.Weather.*`: ProfileApply, Transition, TransitionCompletion, RandomSelection, RegionalOverride, SaveRestore, ClockDiscontinuity, RateIndependence and AdapterLifecycle. AdapterLifecycle includes the no-player / no-renderer path.

Still open: late join over a real transport (WEA-09) and renderer adapters (WEA-10 with real targets).
