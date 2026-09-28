# DocPlaytestRecorder

**Module 40 — Bounded Diagnostic Capture and User-Controlled Evidence Export**

## Purpose and Scope
`DocPlaytestRecorder` provides bounded, privacy-aware diagnostic capture around player-marked issues, maintaining ring buffers of gameplay events, correlating issue markers with pre- and post-windows, capturing contextual state snapshots from registered providers, and exporting local self-contained evidence bundles.

The plugin adheres to strict architectural bounds:
- Strictly bounded in-memory ring buffer (enforcing maximum event count, byte caps, and retention windows).
- Automatic redaction of sensitive fields (passwords, auth tokens, secrets) before retention.
- Provider failure fault isolation: failing or slow evidence providers produce bounded omission records without disrupting gameplay.
- Transactional staged export: exports stage in temporary directories; faults cleanly clean staging without orphan files or corrupting existing bundles.
- Cryptographic hash verification and manifest integrity.
- World teardown / travel context boundary isolation.
- Untrusted viewer input validation against directory traversal and external path escapes.
- Zero-overhead when disabled.
- Strictly local user-controlled exports; no automatic telemetry or network uploads.
- Operates with zero sibling plugin dependencies, verified via `UDocSamplePlaytestProvider`.

## Architectural Rules and Invariants
- **Bounded Ring Buffer (`DBG-01`)**: Count, time, and byte caps are enforced under flood conditions; evicted events are tracked.
- **Marker Window Correlation (`DBG-02`)**: Issue markers capture temporal windows (`PreWindow`, `PostWindow`) of events.
- **Redaction Before Retention (`DBG-03`)**: Sensitive tokens are replaced with `[REDACTED]` prior to entering buffer memory.
- **Provider Failure Tolerance (`DBG-04`)**: Provider faults are logged as omissions in the export manifest.
- **Transactional Staging (`DBG-05`)**: Exports stage atomically; failures roll back cleanly.
- **Manifest Integrity (`DBG-06`)**: All files, sizes, hashes, and omissions are recorded in `manifest.json`.
- **World Teardown Boundary (`DBG-07`)**: Map travel inserts boundary events and notifies providers.
- **Untrusted Input Protection (`DBG-08`)**: External paths and bundle imports are strictly sanitized.
- **Disabled Overhead (`DBG-09`)**: Cheap boolean query skips serialization when capture is disabled.
- **Local & Isolated (`DBG-10`)**: Operates locally without sibling plugin coupling.
