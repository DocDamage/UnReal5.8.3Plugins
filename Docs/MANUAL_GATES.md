# Manual Gates

Eleven requirements have a part that automation cannot prove: it needs a cooked or packaged
build, real audio, a real graphics device, or a second cooked culture. Their automated tests
pass and cover the headless part only. Until a gate below has a recorded result, its row stays
**Partial (manual gate pending)** in the traceability docs.

## How a gate becomes Verified

1. **Fixture.** Each gate needs a small fixture map or assets in the host project (see the table).
   None exist yet: the suite ships base code only, with no content assets. Fixtures are authored
   in the Unreal Editor by a person; the agent does not create or edit `.uasset`/`.umap` files.
2. **Package.** Run `Scripts\Package-Host.ps1 -EngineRoot 'C:\Program Files\UE_5.8'`.
   It writes `Scripts/Output/<run>-Package-Win64-Development-*/summary.json` with the source hash.
3. **Observe.** Launch the packaged executable from that run's `Archive` folder and perform the check.
4. **Record.** Run `Scripts\Record-ManualGate.ps1 -Gate <ID> -Result Passed|Failed|Blocked
   -PackageRun <run dir> -Observer <name> -Notes <what you saw> -Attachment <files>`.
   A pass with no attachment is recorded as `PassedWithoutEvidence` and does not count.
5. **Update docs.** The row moves to Verified only when the record is `Passed`, has evidence
   attached, and its source hash matches the package run.

`Package-Host.ps1` completed a bare Win64 Development package on 2026-09-28:
`Scripts/Output/20260928-170040-Package-Win64-Development-62eadc/summary.json`. The archive contains the
standalone executable and a `.pak`, but no authored gate fixtures, so it does not satisfy any of these gates.
`Record-ManualGate.ps1` has not been run. A person must author the fixtures in Unreal Editor before observing
and recording each gate.

## Gates

| Gate | Plugin | What automation already covers | Fixture needed | What to observe and record |
|---|---|---|---|---|
| ACO-09 | DocAcousticSpaces | Reference adapter records the applied path-policy parameters | Map with one listener, one native emitter, two acoustic spaces with different path policies | Moving the listener across the boundary audibly changes the emitter as the policy says. Attach a capture or parameter log. |
| OPT-09 | DocOpticalBeams | Presentation consumes committed segments; no per-frame rebuild | Map with a source, a mirror and a receiver using the generic beam presentation | Beam segments render correctly; moving the mirror redraws once. Attach screenshots before/after. |
| PNT-10 | DocSurfacePainting | Canonical paint logic works headless; presentation never runs ahead of canonical state | Mesh with a paintable material that samples the paint mask | Painting and cleaning show on the surface in the packaged build. Attach screenshots. |
| PHO-10 | DocPhotography | NullRHI run reports `Unsupported` and invents no pixels | Map with a camera actor and some scene content | A capture produces a real image with readback and encoding. Attach the saved photo. |
| BRC-10 | DocBroadcastChannels | Scheduling, restore and interruptions; `MeasureLocalSyncTolerance` returns `Unsupported` | A bundled audio clip and a channel program that plays it | Bundled playback is audible; measured local sync drift over a fixed period. Attach the drift log. |
| TRM-10 | DocWorldTerminals | Headless API and component path | A generic terminal front-end widget in a map | The cooked front end opens, runs a command and closes independently of the headless API. Attach screenshots. |
| RHY-10 | DocRhythmChallenges | Timing, windows, calibration, audio-loss fault with a mock clock | A short chart plus its audio clip, using `UDocRhythmAudioComponentPlayback` | A real tap/hold run is audible and its timing results are recorded. Attach the timing log. |
| GHO-10 | DocReplayGhosts | Concurrent playback, isolation (no collision, damage or ticking effects) | A map with a player pawn and a recorded ghost file plus a ghost visual | The ghost plays visibly alongside unchanged live gameplay. Attach a capture. |
| DIA-10 | DocDialogue | Missing voice fallback and subtitle timing | Dialogue asset with localized text in a second culture | Switching to the second cooked culture shows the localized lines. Attach screenshots. |
| KNO-08 | DocKnowledgeCodex | Search/sort cache invalidates on culture change | Codex entries localized in a second culture | Cooked localized entries stay valid and sorted after switching culture. Attach screenshots. |
| UI-10 | DocGameFrameworkUI | Keyboard/gamepad navigation, focus, text scale, reduced motion | UI fixture localized in a second culture | The cooked culture fixture displays correctly with navigation working. Attach screenshots. |
