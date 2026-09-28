# DocGameFrameworkUI

DocGameFrameworkUI provides presentation-neutral screen stacks, dialogs, loading leases, notifications, input-device presentation and remapping, and a typed settings authority (Modules 11–20 handoff, Section 12; Module 20).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Per local player | `UDocUIManagerSubsystem` (screens, focus, dialogs, loading), `UDocNotificationSubsystem`, `UDocInputPresentationSubsystem` |
| Shared (GameInstance) | `UDocSettingsSubsystem` (settings authority), `UDocUIPauseSubsystem` (global pause leases) |
| Data | `UDocUIScreenDefinition`, `UDocNotificationDefinition` (primary data assets). Layers are `UI.Layer.*` tags |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, InputCore |
| Not included | The `DocGameFrameworkUICommonUI` bridge (activatable widgets, action bars, CommonUI input routing), a UMG reference presenter, Enhanced Input user-settings adapter, Sound Class/Submix audio provider, native loading-screen adapter, Streaming bridge, debugger UI |

The base has no UMG or CommonUI types and no widget classes. A screen definition carries a `PresentationKey`; a presenter (`IDocUIPresenter`) maps it to real widgets. Without a presenter the manager runs headless and changes logical state only.

Handoff names map to Doc-prefixed types:

| Handoff | This plugin |
|---|---|
| UIManagerSubsystem | `UDocUIManagerSubsystem` |
| NotificationSubsystem | `UDocNotificationSubsystem` |
| UIScreenDefinition / UIScreenRequest | `UDocUIScreenDefinition` / `FDocUIScreenRequest` |
| NotificationDefinition / NotificationRequest | `UDocNotificationDefinition` / `FDocNotificationRequest` |

## Screens and layers

**Layers.** `UI.Layer.Game`, `HUD`, `Menu`, `Modal`, `Popup`, `Loading` and `Debug` each have a policy in `UDocUISettings`:

- stack or exclusive behaviour (exclusive: only the top screen is Active, the rest Suspended)
- relative priority, used for back handling and focus
- whether the layer blocks lower layers
- capacity

**Lifecycle.** A screen moves through `Requested → LoadingPresentation → Activating → Active ↔ Suspended → Deactivating → Closed`, or ends as `Failed` or `Cancelled`. State changes are published after each stack mutation completes.

**API.** `PushScreen`, `PopScreen`, `PopTopScreen`, `ReplaceScreen`, `ClearLayer`, `CancelScreenRequest`, `GetActiveScreen`, `IsScreenActive`, `GetScreenInfo` and `GetScreenState`. Requests carry an optional expected revision; a stale one is refused with `Conflict`.

**Duplicates** follow the definition's policy:

| Policy | Behaviour |
|---|---|
| Reject | The second push fails with `Conflict` |
| FocusExisting | Brings the existing instance to the top and returns its handle |
| ReplaceExisting | Stages a replacement of the existing instance |
| AllowDistinctInstances | Each push gets its own handle and instance id |

**Safety rules:**

- **Stale callbacks.** A load callback for a cancelled, closed or removed screen is rejected and counted; it never resurrects a menu.
- **Staged replace.** `ReplaceScreen` closes the old screen only after the new one has loaded. A failed load leaves the old screen usable. A destructive replace is opt-in.
- **Reentrancy.** Requests made from presenter or provider callbacks during a mutation are queued and run after it.
- **Scope.** `ClearLayer` closes only this player's screens on that layer.

## Focus, input, pause and control

**Control claims.** Active screens claim input, focus and cursor through the shared `IDocPlayerControlProvider` (the player's `UDocPlayerControlSubsystem`, or an override):

- Claims are owner-scoped.
- Closing one modal releases only its own claim, so stale input is never restored over another modal, an inspection or a sequence.
- An outranked screen does not take navigation focus.

**Focus restoration.** When a screen becomes the top again, its saved focus target is restored if the presenter says it is still focusable. Otherwise the definition's default target inside that screen is used.

**Pause policies:**

| Policy | Behaviour |
|---|---|
| GlobalPause | Takes a lease from `UDocUIPauseSubsystem`. The world stays paused while any local player holds a lease. Refused in networked worlds unless listen-server pause is allowed |
| UIOnlyPause | Local UI and input only; the simulation keeps running |
| MultiplayerNonPause | Never pauses |

**Back.** `HandleBack` routes to the top screen of the highest-priority interactive layer. A dialog resolves as Cancelled.

## Dialogs

`ShowDialog` opens a modal on `UI.Layer.Modal`. The callback runs exactly once with one of: Confirmed, Cancelled, ThirdAction, OwnerDestroyed or Failed. That holds through double input, back, timeout, parent destruction, travel and player removal.

- **Destructive prompts** focus Cancel by default and never resolve affirmatively on close, back or timeout.
- **Visibility.** Confirm is refused until the prompt is visible.

## Loading leases

`AcquireLoading`, `SetLoadingLeaseProgress`, `SetLoadingLeaseIndeterminate`, `SetLoadingLeaseStatus` and `ReleaseLoading` are owner-scoped leases. One operation finishing never hides another's loading display.

- **Progress** is indeterminate unless every lease reports a real denominator; the shown value is the minimum.
- **Errors.** Failure and cancellation stay visible until `DismissLoadingError`, distinct from completion.
- **Legacy API.** `ShowLoading`, `HideLoading`, `SetLoadingProgress` and `SetLoadingStatus` operate on one manager-owned lease.
- **Travel.** `NotifyTravel` ends leases whose owner is gone.

This covers in-game overlays only. Blocking map-load presentation needs a separately validated native loading-screen adapter.

## Notifications

- **Fields.** Requests keep the handoff fields plus owner scope, request id, payload version and TTL.
- **Queue policies:** Stack, Replace, Merge, PriorityInterrupt, Persistent and Timed.
- **Bounds.** Visible and queued counts are bounded.
- **Ordering.** Entries order by priority, then FIFO. Queued entries age, so higher-priority traffic cannot starve them.
- **Overflow** drops the lowest-priority, oldest droppable entry. Persistent entries are never dropped.
- **Merging** updates presentation only and never repeats the grant.
- **Timing.** Durations and TTLs run on real time.
- **Actions** resolve an approved handler by key, revalidate, and execute once.

## Input devices, glyphs and remapping

**Device detection** is per local player:

- Analog noise and small mouse movements never switch devices.
- Brand presentation comes from a per-player override, then verified metadata, then the configured generic presentation. It is never inferred from a key.
- A disconnect falls back to the configured device.

**Presentation.** `GetActionPresentation` resolves the player's effective (remapped) bindings. It covers alternates, chords, hold, missing art (text label fallback) and unknown keys. Rebinding, device switches and culture changes invalidate the cache.

**Remapping** is a transaction: Begin, edits, then Apply or Cancel.

- **Conflicts** are declared, and replaced only on request.
- **Reserved navigation.** Keys used by confirm/back actions are reserved, and those actions always keep a binding in both scopes.
- **Persistence.** `CaptureRemaps` and `RestoreRemaps` persist overrides only.

## Settings

**Descriptors** carry: stable id, localized label and help, type, default, range or options, scope, apply policy, provider and dependency. `RegisterStandardSettings` adds the Video, Audio, Input, Accessibility and Language sets.

**Scopes.**

- **Machine settings** have one authority with per-field revisions. Two local players editing the same field get `Conflict`; independent fields merge.
- **Profile settings** are keyed by `FDocOwnerScope`.

**Edit transaction.**

1. `BeginEdit`
2. `SetPending`, which validates type, range, options, capability and dependency
3. `ApplyEdit`, which revalidates everything first
4. For PreviewConfirm settings only: `ConfirmEdit` or `RevertEdit`

Immediate fields are verified against the provider's reported state and rolled back together if one fails.

**PreviewConfirm** settings (resolution, window mode):

- The recovery marker is stored before the preview is applied.
- The confirmation deadline runs on real time, including while the world is paused.
- Timeout or losing the editing screen reverts.
- A failed revert is `RecoveryRequired`, never success.
- `RecoverOnStartup` restores confirmed values after an interrupted preview.

**Providers:**

- **Video:** `FDocGameUserSettingsProvider`. Previews without `ApplySettings`, which would also save. `Confirm` calls `ConfirmVideoMode` and `Persist` calls `SaveSettings`. It reads back the applied `GSystemResolution`.
- **Culture:** `FDocCultureSettingsProvider` refuses unknown cultures.
- **Preferences:** `FDocPreferenceSettingsProvider` stores values. Consumers prove application with `AcknowledgeSetting`.
- **Missing providers.** Audio channels and upscaling are unavailable, with a reason, until a project provider is registered.

**Persistence.** Saved preferences that no registered setting understands are kept and written back untouched. Saves always write the current state, never an old snapshot.

## Tests

The tests are under `Doc.UI.*`:

- PushPop, including headless operation (UI-01, UI-12 base)
- ModalBlocking (UI-02)
- InputDeviceSwitch (UI-03)
- NotificationQueue (UI-04)
- SettingsApply (UI-05)
- SettingsRevert (UI-06)
- GlyphAndRemap (UI-07)
- DialogExactlyOnce (UI-08)
- LoadingLeases (UI-09)
- AccessibilityAndCulture (UI-10, base part)
- SplitScreenAndControlInterop (UI-11, base part)

These still need a real presenter or a packaged run:

- the CommonUI bridge in packaged Win64 (UI-12)
- visible focus and text layout, pseudo-localized and RTL fixtures, and a cooked culture fixture (UI-10)
- the real Enhanced Input and CommonUI routing composition
