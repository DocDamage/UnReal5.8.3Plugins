# DocInspection

Per-player inspection of 3D objects, documents, images, books, audio logs, world details and custom content (Rev 2 handoff, Section 12).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Session owner | `UDocInspectionSubsystem` (a `ULocalPlayerSubsystem`) |
| Content | `UDocInspectionDefinition` (shared and immutable) and `UDocInspectableComponent` (placed on the live actor) |
| Depends on | DocModularCore; the EnhancedInput plugin; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, EnhancedInput, InputCore |
| Not included | DocInspectionMedia bridge (video, media transport, INS-06), CommonUI bridge, UMG example front end (INS-02 visual verification), Interaction/Events bridges |

## Modes

- **World.** Observes the live object in place. The view model exposes `WorldViewTransform`, an orbit around the inspectable that the camera provider applies. The actor is never moved, attached, disabled or rotated.
- **Preview.** Spawns an isolated `ADocInspectionPreviewStage` far from gameplay space.
  - The stage holds the definition's `PreviewMesh`, or else a copy of the inspectable's static mesh. It never uses the live actor's class, so no gameplay BeginPlay runs.
  - It adds its own light and a `USceneCaptureComponent2D` that renders only the stage (ShowOnly list) into a session-owned render target of bounded size.
  - Capture happens on demand, only when the view changes, and no more than `MaxCapturesPerSecond`.
  - No UnrealEd or editor preview-scene code is used.

## Session

`Opening → Loading → Active → Closing → Closed`, or `Failed / Cancelled → Cleanup`.

- **One session per local player.** When a player opens a second one, `ReplaceExisting` (default) closes the first, and `RejectWhileActive` refuses the new one.
- **No player fallback.** A null player is an explicit error. The subsystem never falls back to player 0.
- **Loading.** Assets load asynchronously. Closing during a load gives Cancelled, and a late completion is ignored. A missing asset or a load timeout gives Failed.
- **Cleanup.** Every end path releases what the session acquired: close, failure, cancel, the inspected actor being destroyed, a player-controller change (travel), and player removal. That covers:
  - control claims
  - the input context (only if this session added it)
  - the pushed input component
  - the preview stage
  - load handles
  - audio
  - media sessions
- **Input.** Only the temporary `InputContext` from settings is added, and only if it isn't already installed. It is removed only if this session added it. `ClearAllMappings` is never called. Actions from settings are bound on an owned `UEnhancedInputComponent` pushed onto the player controller: Rotate, Pan, Zoom, Next, Previous, Accept and Back. You can also call the same functions from UI.
- **Control.** `ClaimedCapabilities` (for example `Control.Camera`, `Control.Input.Movement`) are claimed through the player's Core control provider.
  - World pause is opt-in per definition, as a `Control.Pause` claim.
  - Closing one player's inspection never resumes another owner's pause and never disrupts a sequence's claims.

## Content, focus points, audio

- **Pages.** Localized, accessible text plus an optional image per page, with page navigation. Text scale and high contrast settings flow through the view model.
- **Focus points.** Each has a stable id, reveal text and a list of dependencies.
  - Discovery is idempotent and per player: `GetDiscoveries` / `RestoreDiscoveries` go through a save bridge keyed by the project's profile id.
  - Grant tags go through an authorized `IDocInspectionRewardProvider` or not at all. Inspecting on a client never grants authoritative state.
- **Audio logs.** Play, pause, resume, and start at an offset (seek support depends on the source). Transcripts are included.
  - `bAudioContinuesAfterClose` hands playing audio to a longer-lived owner inside the subsystem, which is stopped on travel or player removal.
  - A plain close or failure stops the audio.
- **Video.** Video needs an `IDocInspectionMediaProvider` (the DocInspectionMedia bridge). Without one, opening returns Unsupported.
- **View model.** `FDocInspectionViewModel` plus `OnViewModelChanged` describe the whole presentation. No UI framework is required.

## Tests

The tests are under `Doc.Inspection.*`: WorldAndPreview, DocumentsAndPages, IndependentPlayers, LoadFailureAndCleanup and FocusPoints.

They run on bare `ULocalPlayer`s, so they don't include a controller, the Enhanced Input subsystem or a control provider. That means input-context ownership, rendering and the INS-08 coexistence with sequences need a PIE or cooked run.
