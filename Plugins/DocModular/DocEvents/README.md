# DocEvents

World-scoped, typed Gameplay Tag event bus (Rev 2 handoff, Section 6).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Engine | Unreal Engine 5.8.3, Win64 |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Optional bridges (not implemented) | Region/Interaction/Time → Events mirrors; Save persistence of retained values; network transport |
| Network | Local only. There is no client→server broadcast endpoint by design |

## What it does

- `UDocGameplayEventSubsystem` (one per Game/PIE world): `BroadcastEvent`, `BroadcastNextFrame`, `BroadcastAfterDelay` (+ `CancelScheduledEvent`), `Subscribe` / `SubscribeNative` / `Unsubscribe` / `UnsubscribeAllForOwner`.
- Exact or include-children tag matching; each subscription receives an event at most once.
- Scopes: World, Actor, Component (weak object), Region, Team, Custom (value key). Scope filters compare kind **and** key.
- Delivery: game thread, priority desc then registration order, dispatch snapshot per event. Nested broadcasts queue and drain after the current event. `MaxEventsPerDrain` bounds storms (the rest is deferred to the next tick); `MaxQueuedEvents` bounds the queue (overflow drops with a warning).
- Owner-bound subscriptions are removed when the owner dies; one-shot subscriptions remove themselves.
- Retained values: latest event per exact tag + scope with a revision, `UpdateRetainedValue` / `GetLatestPayload` / `ClearRetainedValue`, optional replay on subscribe, bounded count and TTL. Payloads holding strong object references are refused. **Runtime only — not disk persistence.**
- `HasEventOccurred` answers from a bounded history ring: Yes / No / **Unknown** when the tag is not tracked or entries may have been evicted.
- Payload schemas per tag (settings or `RegisterPayloadSchema`); mismatches are rejected with InvalidInput.
- Blueprint async node **Wait For Gameplay Event** (listen or wait-once, optional timeout; exactly one terminal pin).
- Implements `IDocWorldStateProvider` from retained World-scope values.
- Diagnostics: `GetStats()`, opt-in `GetDebugHistory()` (tag, scope, sender/target names, payload type — never payload contents).

## Setup

1. Enable **Doc Modular Core** and **Doc Events**.
2. Configure limits under Project Settings → Plugins → Doc Events (optional).
3. Define your own event tags (e.g. `Event.Door.Opened`) in your project's tag tables.

```cpp
UDocGameplayEventSubsystem* Bus = UDocGameplayEventSubsystem::Get(this);
FDocEventListenOptions Options; Options.Owner = this;
Handle = Bus->SubscribeNative(MyTag, Options, FDocGameplayEventNativeDelegate::CreateUObject(this, &UMyThing::OnEvent));
// ...
Bus->Unsubscribe(Handle);
```

## Tests

`Doc.Events.*`: Broadcast, HierarchicalSubscription, Unsubscribe, Reentrancy, ScopeIsolation, PersistentPayload, PayloadSchema, Scheduled.

## Known limitations

- The event monitor editor panel is not implemented (debug data is available through `GetDebugHistory`).
- Delays support WorldGameplay and RealTime clocks only.
- Performance workload (1,000 listeners / 1,000 events per second) not measured.
