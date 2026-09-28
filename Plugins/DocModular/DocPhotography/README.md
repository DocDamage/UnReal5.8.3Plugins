# DocPhotography

Module 31 of the DocModular gameplay systems suite (Phase 11: Player Activities and Media).

## Purpose
Player photography: capture an image, evaluate which registered subjects it contains, and keep an owner-scoped, bounded gallery whose image bytes are stored before the record that points at them.

## Pipeline
1. `RequestPhoto(World, Request, Profile, OutRequestId)` validates the request (world alive, dimensions within `MaxImageDimension`, profile valid, quota headroom) and snapshots subject poses. It returns a request id; `QueryCapture` exposes the status and ticket.
2. The configured `UDocPhotoImageSource` produces pixels. Synthetic sources may deliver immediately or deferred; `UDocSceneCapturePhotoSource` renders to a render target and reads back after a render fence.
3. `SubmitPixels(Ticket, …)` / `FailCapture` deliver the result. Stale or cancelled tickets are ignored.
4. `EncodeAndCommit` runs in this order: PNG encode + SHA-1, coherence check, evaluation, quota check, blob write, record commit, broadcast. A failed blob write commits nothing.

`CancelCapture` succeeds before commit and returns Conflict afterwards. `Pump` (also driven by tick) advances deferred sources and time-outs.

## Evaluation
- Projection uses the horizontal FOV and aspect = width / height, with a 1 cm near clip. Subjects crossing the near plane or behind the camera are flagged.
- Frame coverage is measured from projected world-bounds corners (`BoundsInFrameFraction`); it is a bounds estimate, not pixel coverage.
- Visibility is sampled (bounded by `MaxSamplesPerSubject`) through the replaceable visibility tester and reported as sample counts.
- Off-axis and facing angles feed the profile. `UDocPhotoEvaluationProfile::ValidateProfile` rejects bad ranges; weights are normalized and each rejection carries a reason code. Stale subjects never qualify.
- Coherence: if a subject moved more than `PoseToleranceCm` between request and capture, or the capture arrived too late, the record is marked Unavailable (`SubjectMovedDuringCapture` / `CaptureTooLate`) rather than scored against the wrong pose.

## Subjects
`UDocPhotographableComponent` registers with a per-world `UDocPhotoSubjectRegistry` (world subsystem). There is no global static list, so PIE worlds and test worlds never see each other's subjects. `SubjectVersion` bumps on changes.

## Gallery and storage
- Records are schema 2 and carry request id, world, timestamps, subject snapshot revision, image format, byte count, blob key, profile id/version and evaluation status.
- Blob stores: `UDocPhotoMemoryBlobStore` (tests) and `UDocPhotoFileBlobStore` (Saved/DocPhotography, temp file then move). Keys are validated.
- `SetGalleryQuota` never evicts existing photos; new captures over quota are rejected.
- All queries (`QueryPhoto`, `ListPhotos`, `LoadPhotoImage`, `EvaluatePhotoRecord`, `DeletePhoto`) are owner-checked.
- `LoadPhotoImage` verifies the SHA-1; a mismatch or missing blob marks the record ImageUnavailable.
- `CaptureGalleryMetadata` / `StageRestore` round-trip metadata only. Restored records whose blobs are missing are marked ImageUnavailable, not dropped.

## Tests (Doc.Photo.*)
| ID | Test | Covers |
|----|------|--------|
| PHO-01 | CameraProjection | FOV/aspect projection, near plane, behind camera |
| PHO-02 | OcclusionEstimate | sampled visibility counts |
| PHO-03 | FrameCoherence | pose tolerance, late capture |
| PHO-04 | CaptureVsEvaluation | capture stored even when evaluation fails |
| PHO-05 | AsyncCancellation | cancel before/after commit, stale tickets, world loss |
| PHO-06 | StorageCommit | blob before record, failed write commits nothing |
| PHO-07 | GalleryBounds | quota, no eviction |
| PHO-08 | OwnerIsolation | owner-checked queries |
| PHO-09 | RestoreMissingBlob | restore marks ImageUnavailable |
| PHO-10 | CookedRenderedCapture | returns Unsupported under NullRHI |

## Known limits
- PHO-10 only proves the Unsupported path in headless automation. A real rendered capture needs a graphics-enabled run (manual gate) and is not claimed as verified.
- Coverage is bounds-based; pixel-accurate masks are out of scope.
