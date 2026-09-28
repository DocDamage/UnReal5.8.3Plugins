# DocMapNavigation

Spatial data and math for world maps, minimaps, compasses, waypoints, markers, discovery, fog of war, multiple floors and off-screen indicators (Modules 11–20 handoff, Section 3; milestone 5.2).

This plugin does **not** include:

- widget trees
- scene-capture rendering
- navmesh routing
- fast-travel execution

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Shared records | `UDocMapMarkerRegistry` (world). A feature-internal registry: one registration per source per world; detached descriptors |
| Player view | `UDocMapNavigationSubsystem` (local player). Owner scope, filters, tracking, waypoints, discovery, floors, compass, indicators |
| Math | `UDocMapMath`: pure conversions, each with a defined inverse |
| Data | `UDocMapDefinition`, `UDocMapMarkerDefinition`; components `UDocMapMarkerComponent`, `UDocMapDiscoveryComponent` |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | `DocMapNavigationWorldPartition`; Regions, Quest, Save and CommonUI bridges; replication adapter; the editor tools (Map Definition Editor, bounds preview, marker/coordinate/discovery debuggers); GridBased, TextureMask and Custom discovery backends |

The types use the `Doc` prefix. They correspond to the original names: `UMapNavigationSubsystem`, `UMapDefinition`, `UMapMarkerDefinition`, `UMapMarkerComponent`, `UMapDiscoveryComponent`, `FMapMarkerState`, `FMapLayerDefinition` and `FMapCoordinateTransform`. `FMapMarkerHandle` becomes the registry's `FDocRequestHandle` together with the stable `MarkerId`.

## Coordinates

Maps use an authored planar orthographic projection:

- An orthonormal basis `U`, `V`, `N` with origin `O`, where normalized `(0,0)` is at `O` and `(1,1)` is at `O + Lu·U + Lv·V`.
- All maths is done in double precision.
- Degenerate or skewed bases, zero extents and non-finite input are rejected.
- `WorldToNormalized` returns the unclamped coordinates, a height, an Inside/Outside flag and a separate display-clamped value. The clamped value is never used for inversion.
- `NormalizedToWorld` needs an explicit height, because a 2D coordinate has lost it. The result is a point on that declared plane, not a ground hit.
- Map → widget conversion applies centre/pan, zoom, rotation and DPI; `WidgetToMap` is its exact inverse. The UI adapter supplies the geometry, so no UMG is needed.
- Persistent locations are stored in the absolute frame (engine location + world origin offset). They never shift twice when the origin is rebased.

## Markers

`RegisterMarker`, `UnregisterMarker`, `UpdateMarker` (with an optional expected revision), `SetMarkerVisibility`, `RemovePersistentMarker` (authorized), `ProviderUpdate` and `AddCustomMarker`.

- **Duplicate ids.** If a different source registers an existing `(MarkerId, InstanceScope)`, the call returns Conflict. The last writer never silently wins. Repeated level placements use distinct instance scopes.
- **Lifetimes.** What happens to a marker when its source leaves:

  | Lifetime | Behaviour |
  |---|---|
  | ActorLifetime | Removed |
  | PersistentLocation | Retained |
  | LastKnownDynamic | Retained and marked stale; it is not a live feed |
  | ProviderManaged | Marked stale, and the provider is notified. No position is invented |

- **Queries.** `GetVisibleMarkers`, `GetMarkersByTag` and `GetNearestMarker` return immutable, bounded snapshots. Filters apply in this order:
  1. audience (owner-only markers are enforced before a view ever sees them)
  2. map and layer
  3. floor
  4. discovery
  5. tags (exact or hierarchical)
  6. per-player hidden markers and hidden tags
  7. distance (2D or 3D)

  Results are then sorted tracked-first, then by priority, then by stable id. Hash-map iteration order is never used.
- **Updates.** Static markers never tick. Dynamic markers update only when they move past a threshold, at a configured cadence.

## Discovery and fog of war

- **Regions.** `DiscoverLocation` and related calls work on region or location ids and are idempotent. The Regions plugin is not required; a bridge can supply observations.
- **Radius coverage.** Coverage is a chunked bit grid in map space.
  - A cell is revealed when its centre falls inside the radius.
  - Memory grows only with the chunks actually allocated.
  - `ResetRevealedArea` is the only operation that erases coverage.
  - Undiscovering a region does not touch radius coverage.
- **Separation.** Each owner and each map keeps its own discovery state. Discovery is not line-of-sight visibility.

## Floors, compass, off-screen indicators

- **Floors** use half-open altitude bands `[MinZ, MaxZ)`, so a height exactly on a boundary belongs to the floor above.
  - Where bands overlap, the higher priority wins, then the floor id.
  - Hysteresis stops the floor flickering on stairs.
  - An explicit override handles stacked interiors.
  - If no band matches, the result is `NAME_None` (unknown/exterior).
- **Compass.** Bearings are calculated from an explicit heading and the map's authored north yaw. The relative bearing is in `(-180, 180]` and the compass bearing is in `[0, 360)`. Same-position markers are flagged, and an invalid heading yields no entries.
- **Off-screen indicators** use the view of the correct local player.
  - Targets behind the camera are distinguished from targets outside the viewport.
  - Indicators are clamped to the safe rectangle.
  - Zero or invalid geometry is rejected.

## Persistence

`CaptureState` and `RestoreState` store:

- discoveries
- coverage chunks
- waypoints
- tracked markers
- hidden markers

The data is versioned and bounded, and keyed by owner and map. Restore stages everything before applying it, refuses another owner's state, and does not replay discovery events.

If the fog schema has changed (cell size or chunk size), restore fails and nothing is applied, unless the caller explicitly confirms `bDiscardIncompatibleFog`.

`UDocMapMarkerRegistry::CapturePersistent` and `RestorePersistent` handle persistent and custom markers. A marker whose definition is unknown is quarantined (kept but not shown).

## Tests

`Doc.Map.*`:

- CoordinateConversion
- MarkerRegistration
- MarkerFiltering
- MultiFloorResolution
- Discovery
- PersistentMarkerWithoutActor (with instance scopes)
- CompassWrap
- WidgetTransform
- SaveRestore
- OffscreenAndWaypoint

Still open:

- A real world-origin rebase run (MAP-09 is only partly covered).
- An indexed stress-query budget (MAP-12).
- The editor tools.
