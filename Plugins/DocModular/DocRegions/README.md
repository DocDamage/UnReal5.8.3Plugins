# DocRegions

Logical spatial regions and tracked membership (Rev 2 handoff, Section 7). Regions answer **where**, never **what happens there**.

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags |
| Not included | Events/Audio/Streaming/World Partition bridges; spline/polygon shapes (use `IDocRegionShapeProvider` on the owner meanwhile) |

## Pieces

- `UDocRegionDefinition` (data asset): RegionTag, DisplayName, Priority, Traits, optional ExplicitParent (cycle-checked), generic Metadata and Extensions. Shared by many placed instances.
- `UDocRegionComponent` (scene component on any actor): definition + Box / Sphere / Capsule / Custom shape; stable authored `InstanceId` (new on editor duplicate/paste, preserved on load and PIE).
- `ADocRegionActor` (box region, no brush authoring) and `ADocRegionVolume` (brush containment).
- `UDocRegionObserverComponent` or `RegisterObserver(Actor, Options)`: only registered observers are tracked. Reference-point (default, boundary-inclusive with tolerance) or bounds-overlap test.
- `UDocRegionSubsystem`: grid index for static regions, brute force for movable/huge ones; bounded update interval; events `OnRegionEntered`, `OnRegionExited`, `OnPrimaryRegionChanged` with reasons (Initial, Movement, Teleport, RegionAdded, RegionUnloaded, ObserverRemoved).
- Queries: `GetRegionAtLocation`, `GetRegionsAtLocation`, `GetRegionsForActor`, `GetPrimaryRegionForActor`, `IsActorInRegion`, `GetActorsInRegion` (tracked, loaded observers only), `FindRegionsByTag`, `FindRegionByTag` (NotFound / Conflict when ambiguous).

Primary selection: priority → explicit-hierarchy depth → tag specificity → smaller bounds volume → InstanceId. Registration order never matters.

## Tests

`Doc.Regions.*`: EnterExit, NestedPriority, PrimaryRegion, UnloadReload, TagQueries.

## Known limitations

- Moving (bMovable) regions are brute-forced each update; fine for tens, not thousands.
- Debug drawing and the region debugger panel are not implemented.
- Region membership is local; no network authority policy is implemented.
