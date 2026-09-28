# DocSurfaceFeedback

DocSurfaceFeedback turns surface, context and event requests into audio, visual, tactile and gameplay responses (Modules 11–20 handoff, Section 5; build milestone 5.1).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Resolver/dispatcher | `UDocSurfaceFeedbackSubsystem` (world). `ResolveFeedback` is pure; `SubmitFeedback` performs side effects |
| Data | `UDocSurfaceResponseProfile` (rules) and `UDocSurfaceMappingAsset` (physical material and surface type → `Surface.*`) |
| Producers | `UDocSurfaceFeedbackComponent` (manual, hit, traced, distance steps, landing) and `UDocSurfaceAnimNotify` |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, PhysicsCore |
| Not included | MetaSound-control and Niagara executor bridges, Events bridge, network transport bridge (SFC-10 transport), pooling (see below) |

Type names use the suite's `Doc` prefix. They map to the original types as follows: `UDocSurfaceFeedbackSubsystem` = `USurfaceFeedbackSubsystem`, `UDocSurfaceFeedbackComponent`, `UDocSurfaceResponseProfile`, `FDocSurfaceFeedbackRequest` and `FDocSurfaceFeedbackResult`.

## Surface mapping

The resolver checks these sources in a configurable order: the explicit **physical material**, then the project's **Physical Surface type**, then an optional host `IDocSurfaceTagProvider`. If none match, it uses `DefaultSurface`. The host's surface table is only read, never replaced, so `SurfaceType1` means whatever your project says it means.

The result's `SurfaceOrigin` tells you where the surface came from, and keeps these cases apart:

- **Unmapped:** nothing matched and there is no default.
- **ConfiguredDefault:** nothing matched, so the configured default was used.
- **NoHit:** the request carried no hit.

## Rules

Each rule has a stable `RuleId`, an event (exact or ancestor match), a surface (exact, ancestor or any), required and blocked context tags, an inclusive magnitude range, a priority, responses and an optional per-source cooldown.

- **Ranking:** priority, then event specificity, then surface specificity, then the number of satisfied context constraints, then `RuleId` (stable).
- **Ambiguity:** equally ranked rules that could be confused are reported by `FindAuthoringProblems` and by data validation.
- **Fallback chain:** event+surface+context → event+surface → event only → default rule. The result reports which stage matched (`MatchPath`), the selected rule, the candidate count and the rejected reasons.
- **Missing assets:** if a variation is missing, the response uses its own `Fallback` asset or reports `AssetUnavailable`. It never falls through to a different rule.
- **Variation choice:** uses a request-local random stream, not a shared one, with a no-immediate-repeat option per source and rule.

## Dispatch and budgets

Each response gets its own admission result: `Executed`, `BudgetSuppressed`, `Unsupported`, `AssetUnavailable`, `Cancelled`, `PermissionDenied` or `Pending`. Resolving a request successfully and playing it successfully are reported separately.

**Executors.** These kinds are registered natively by the subsystem:

- Sound (`SpawnSoundAtLocation`)
- Decal
- Camera shake (local players in range)
- Haptic (the instigating local player only)
- Allowlisted spawned actors

MetaSound, Niagara and Custom need a bridge executor; without one they report `Unsupported`. Cosmetic executors never run on dedicated servers.

**Gameplay responses.** `GameplayEvent` and `GameplayTag` responses run only when all of these hold:

- the request has `bAuthoritativeIntent`
- the call is on the authority
- an `IDocSurfaceGameplayProvider` is installed

A client footstep is never proof of damage. A cosmetic failure never blocks a gameplay response, and a gameplay failure never blocks a cosmetic one.

**Budgets.** These limits apply:

- active sounds, effects and decals
- requests per second
- distance cull from local views
- per-source cooldown
- queue length
- a time-to-live for requests still waiting on asset loads

**Async loading.** Queued loads are cancelled if the source actor unloads or the request goes stale. A late callback never spawns orphaned feedback.

**Continuous responses.** Slide, drag and vehicle responses return a handle with `UpdateContinuous` and `StopContinuous`. The response stops when its source is torn down, and a handle only ever controls its own instance.

**Pooling.** Pooling is **not** in the base. Components are created and destroyed normally. Opt-in pooling with full reset and generation checks belongs in a pooling utility or bridge (see the DocObjectPool backlog).

**Duplicates.** The same `CorrelationId` within `DedupeWindowSeconds` returns NoChange, so a predicted copy and its confirmed copy don't both play.

## Producers

- **Distance steps.** Steps come from grounded travel at `StrideCm`. There is at most one step per update, so a hitch drops the backlog instead of bursting. A teleport resets the stride phase. Nothing is emitted while airborne. A landing fires once per validated airborne-to-grounded transition, above `MinLandingSpeed`.
- **No double firing.** While `bDistanceFootsteps` is on, footstep AnimNotifies for that component are ignored.
- **Tracing.** Traces use explicit channel and simple/complex settings and always ask for physical materials. There is no per-frame trace by default.
- **AnimNotify.** The notify is immutable. It reads the mesh and socket that are executing it, and does nothing in previews that have no producer component.

## Tests

The tests are under `Doc.Surface.*`: PhysicalMaterialResolution, ExactMatch, Fallback, ContextModifier, NoMatch, FootstepProducers, ImpactAndContinuous, BudgetAndPooling (budget, cooldown, cull, TTL), AsyncOwnerLoss, NetworkDedupe and DedicatedServer (simulated via `SetCosmeticsDisabledForTesting`).

A recording executor stands in for the real ones, so the tests prove dispatch and ownership but not audible or visible output. These still need real content and a cooked build:

- trace-based material resolution on complex collision, skeletal physics assets and landscape
- real playback
- network reconciliation
