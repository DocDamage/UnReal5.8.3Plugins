# DocSurfacePainting

Persistent paint, dirt, cleaning and reveal masks on prepared surfaces (Modules 21–40 handoff, Section 11; Module 29). This is accumulated surface state, not a decal effect. Arbitrary meshes are **not** automatically compatible.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocSurfacePaintingSubsystem` (registry by instance id) |
| Component | `UDocPaintableSurfaceComponent` (self-registers; one canonical mask per instance) |
| Assets | `UDocPaintSurfaceDefinition`, `UDocPaintBrushDefinition` |
| Mapping | `IDocSurfaceCoordinateProvider`; reference `UDocCollisionUVCoordinateProvider` |
| Depends on | DocModularCore; engine modules only |
| Not included | Skeletal/runtime meshes, seam repair, multi-layer blends, world-meter brushes, network replication, collaborative undo, editor overlays |

## Canonical state

- **Source of truth:** a bounded CPU mask of `uint8` texels, where the value is the amount of layer. The mask is made up of a **checkpoint** plus an **ordered stroke journal**.
- **Instances:** each component has its own instance `SurfaceId`, which defaults to the owner's name. Two actors using one definition never share paint, and a duplicate instance id is refused (`Conflict`).
- **Render texture:** optional, and only a presentation copy. It is tagged with the canonical revision it shows. Headless runs and dedicated servers never create one.

## Mapping

- **Overlapping or mirrored UVs** (`bHasOverlappingUVs`) are refused (`OverlappingUVs` → `Unsupported`) unless `bAcceptSharedOverlappingUVs` is set.
- **Invalid UVs are never written:**
  - Non-finite UVs, and UVs outside [0,1] in `Reject` address mode, return `InvalidInput`.
  - A hit without a UV returns `InvalidMapping` or `Unsupported`.
  - Nothing is ever written at (0,0) by default.
- **`Wrap` address mode** wraps the points, but brush footprints do not wrap across the edge.
- **Seams and jumps:** consecutive points further apart than `MaxBridgeGapUV` are not bridged, so a drag never paints across a UV seam or a jump between UV islands.
- **Collision-UV provider:** the reference provider needs the project's *Support UV From Hit Results* physics setting. When the setting is off it reports `Unsupported` and never changes it.
- **Brush radius** is in UV units. A world-meter radius is not supported by the base.

## Strokes

Every stroke is validated before any allocation or pixel change:

- A stroke id is required.
- A stroke already applied returns `NoChange`. Dedup survives compaction and save/restore, and older ids are also rejected by the sequence rule.
- `SequenceNumber` must follow the last accepted one (`Conflict` otherwise). Zero means "assign the next".
- Checked limits: the point count (`MaxStrokePoints`), radius in (0,1], strength in [0,1], and the resampled dab count (`MaxResampledDabs`, counted before any work is done).

**Rasterizer version 1:**

- `a = floor(255 · Strength · (1 − d/R) + 0.5)`, where `d` is measured to the texel centre and `d ≤ R`.
- Resampling steps are `max(R/2, 1 texel)`.

| Operation | Rule |
|---|---|
| Paint | `m = max(m, a)` |
| Clean | `m = min(m, 255 − a)` |
| Reveal | Same monotonic rule as Clean |

Paint followed by clean differs from clean followed by paint. The journal keeps the one authoritative order.

## Coverage

- **Denominator:** only the authored eligible texels (`EligibleTexelMask`; empty means all).
- **Polarity:** `CoverageMode` selects painting goals (at or above the threshold) or cleaning goals (below the threshold).
- **Result:** reports the denominator, covered count, threshold, required ratio, precision and revision.
- **Zero eligible texels never meet the goal**, even with a 0% requirement.
- **`OnCoverageGoalChanged`** fires once per transition, from canonical state. Neither render lag nor repeated strokes fire it again, and restores never fire it.
- **Texel coverage, not area:** coverage is texel coverage, not physical surface area.

## Presentation revisions

- `CreateRenderUploadTicket` returns the canonical revision and the dirty rectangle since the last upload.
- `CommitRenderUpload` rejects a late (older) upload with `Conflict`, and one ahead of canonical with `InvalidInput`.
- The optional texture path copies the mask into a transient `PF_G8` texture on a per-instance dynamic material. It never edits the shared material asset. It runs only when a mesh with a material in `MaterialSlot` is set.

## Journal, checkpoints, persistence, undo

- **Compaction:** when the journal exceeds `MaxJournalStrokes`, it is folded into a new checkpoint. At most 4096 ids are remembered.
- **`CaptureSnapshot` saves:**
  - the zlib-compressed checkpoint
  - the residual journal
  - the folded ids
  - the fingerprint and dimensions
  - the rasterizer version
  - a CRC32 of the full mask
- **`RestoreSnapshot` validates everything before allocating:**
  - schema and rasterizer version
  - **fingerprint** (a mismatch returns `Conflict`, "IncompatibleSurface", and no pixels are guessed onto a new layout)
  - dimensions, the declared decompressed size, the compressed size bound, and journal length and order
- It then decompresses and replays into a scratch copy and verifies the CRC. Any failure leaves the current state untouched.
- **Undo** (`bAllowLocalUndo`, single-owner local profile) removes the newest journal stroke only if it belongs to the caller. It never silently undoes someone else's later stroke. An undone id stays deduplicated.

## Tests

`Doc.Paint.*`, 10 tests: PNT-01 to PNT-10.

PNT-10 covers the headless half of the requirement. A real cooked material displaying the mask remains a manual gate.
