# DocGestureRecognition

Module 35 of the DocModular suite. Recognizes locally drawn single-stroke 2D symbols against authored templates. It returns data only and uses no machine learning, UI or AI dependencies (DocModularCore + engine only).

## Templates
- `UDocGestureTemplate`: canonical points, resample count (8–256), scale policy, rotation invariance with a bounded search range, direction invariance, similarity threshold, runner-up margin, minimum path length, and an accessible-alternative flag. `ValidateTemplate` rejects empty, non-finite or zero-length shapes and out-of-range settings.
- `UDocGestureTemplateSet`: SetId, Version, up to 64 templates with unique ids. `FindCollisions` is an authoring aid that lists template pairs that recognize each other under their own settings.
- Templates are treated as immutable. Registration compiles a private normalized snapshot, cached by the template content hash and algorithm version. Editing an asset afterwards never changes a registered model, and a stroke keeps the model that was active when it began.

## Capture
Adapters convert device coordinates into a local drawing plane: origin top-left, +X right, +Y down, in surface units after DPI scaling.
- `SetCaptureSettings` defines the surface size, the leave policy (`Clamp`, `EndStroke`, `Cancel`) and the analog-stick model (dead zone and speed).
- `BeginStroke` starts a new generation. `AppendPoints` refuses non-finite points and negative time. Going over the point limit (hard cap 5000) or the duration limit cancels the stroke.
- `AppendAnalogSample` integrates stick input into coarse samples (`bCoarseSampling`).
- A device change cancels the capture.

## Recognition
1. Drop non-finite points and near-duplicates.
2. Reject strokes with too little path.
3. Resample by arc length (the loop is bounded and cannot divide by zero).
4. Translate to the centroid.
5. Optionally align rotation.
6. Scale: uniform by default. Non-uniform is opt-in, and near-1D strokes fall back to uniform.
7. Compare by mean point distance. Reversal is tried only for direction-invariant templates. Rotation uses a golden-section search over ±`RotationSearchDegrees` with a fixed 12 iterations.

Scores are labelled **similarity** (bounded 0–1), not probability. Ties break by GestureId. A result needs both the threshold and the runner-up margin; otherwise it is `NotRecognized` or `Ambiguous`. `WorkUnits` reports the distance evaluations spent.

## Consumer path
`DispatchResult` is the only route to `OnGestureDispatched`. It refuses unrecognized results, stale generations (a newer stroke, cancel or alternative), results from a replaced model, and repeats. Dispatch consumes the generation. `SelectAccessibleAlternative` produces the same semantic result, labelled `AccessibleAlternative`, and goes through the same path. Gameplay decides what the intent is allowed to do.

## Isolation
Each local player has its own subsystem, model, generation and capture state. Raw strokes are transient and never persisted.

## Tests (Doc.Gesture.*)
| ID | Test | Covers |
|----|------|--------|
| GES-01 | ResampleDegenerate | duplicates, stationary points, NaN, zero length |
| GES-02 | ScaleAndAspect | uniform keeps distinctions; non-uniform opt-in; near-1D guard |
| GES-03 | RotationPolicy | directional arrow rejects rotation; opt-in invariance |
| GES-04 | StrokeDirection | reversal only where allowed |
| GES-05 | RejectionMargin | scribble rejected, jittered match accepted, near-match ambiguous, collisions |
| GES-06 | TemplateVersion | in-flight stroke keeps its model; edited set is a new model |
| GES-07 | CancelGeneration | stale/cancelled/repeated results never reach the consumer |
| GES-08 | InputBounds | point/duration/template/search limits, leave policies |
| GES-09 | AccessibleAlternative | same consumer path, labelled, validated |
| GES-10 | LocalIsolation | two players, separate sets/generations/consumers, private snapshots |

## Known limits
Single-stroke only. The editor preview and export tools are not implemented beyond `FindCollisions`.
