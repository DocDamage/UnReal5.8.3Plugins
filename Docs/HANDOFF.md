# DocModular Handoff: What Is Left to Finish

Written 2026-09-28. Read with `DEVELOPMENT_STATUS.md` (evidence and run history) and the three
traceability docs (per-requirement state). This file lists remaining work only.

## 1. Where the project stands

- **Code:** DocModularCore plus 40 gameplay plugins, all runtime-only base code, in `Plugins/DocModular`.
  Each plugin depends only on DocModularCore and engine modules.
- **Verified:** all four host builds and the full automation suite (391/391) in the 2026-09-28 run series;
  latest full-suite and Development package source SHA-256 `251DCB2A320E8795764B7AFC8EE3028B89D77F1E5FCA8C65C97EEC11DD66DF7E`.
- **Isolation:** Core-only plus all 40 Core-and-feature hosts passed non-unity physical-absence builds (41/41)
  and headless game-mode startup (41/41). Evidence: `Scripts/Output/20260928-152202-PluginIsolation-1b0a78/summary.json`
  and `Scripts/Output/20260928-174055-IsolatedStartup-c9a914/summary.json`.
- **Consumer portability:** two structurally different clean hosts passed. The broad C++ host built all 41 Runtime
  modules and public headers; the nested project-local host contained only Core + Interaction and completed an
  instant interaction between two ordinary `AActor` subclasses. Evidence: `Scripts/Output/20260928-190206-CH-9d1ed1/summary.json`.
  Full-suite behavior, PIE, and cooked behavior remain unverified; the broad host's runtime startup is not claimed.
- **Packaging:** `Scripts/Package-Host.ps1` completed one Win64 Development package run, archived the executable
  and pak, and has no manual gate fixtures. `Scripts/Record-ManualGate.ps1` has not run; manual editor fixtures
  and cooked gates remain pending. `Scripts/Validate-Workspace.ps1` passed its 8-check read-only preflight;
  `Scripts/Verify-ConsumerHosts.ps1` passed both clean sample hosts.
- **Version control:** `main` publishes to https://github.com/DocDamage/UnReal5.8.3Plugins.
- **Requirement state (all three traceability docs):**

| Doc | Verified | Partial (manual gate) | In Progress | Not Started | Untraced |
|---|---|---|---|---|---|
| `REQUIREMENTS_TRACEABILITY.md` (Modules 1–10, 87 IDs) | 59 | 0 | 17 | 11 | 0 |
| `EXPANSION_TRACEABILITY.md` (Modules 11–20, 148 IDs) | 96 | 3 | 32 | 17 | 0 |
| `MODULES_21_40_TRACEABILITY.md` (Modules 21–40, 232 IDs) | 193 | 8 | 0 | 31 | 0 |

The automated base logic, compile-time physical-absence checks, individual plugin startup checks, and two-host
portability proof are complete for the tested scope. What remains includes editor-authored fixtures,
cooked/packaged runs, PIE validation, real networking, bridges between plugins, performance measurement,
and release paperwork.

## 2. First handoff cleanup (completed 2026-09-28)

1. **Verification:** the full `Scripts/Verify-Suite.ps1` matrix (called by `Run-Verify.cmd`) passed all four
   host builds and 391/391 tests. This includes the INV-11 delta-trim assertions.
2. **Android File Server config:** `SecurityToken` is empty and `bAllowNetworkConnection=False` in
   `Config/DefaultEngine.ini`; no generated token is stored in the current config.
3. **Release checklist:** updated with build/test evidence and incomplete traceability counts; the then-pending
   isolation gate was closed by the later 41-host compile/header and startup runs cited above.
4. **Traceability:** added CROSS21-01..20 and REL21-01..12 from handoff Sections 23.7 and 37 as Not Started.
5. **Publication:** these follow-up corrections were committed and pushed to `origin/main` as one changeset.
6. **Isolation:** `Scripts/Verify-PluginIsolation.ps1` built Core alone and Core plus each feature with every sibling directory absent (41/41 passed); the dependency matrix and affected traceability rows now cite that run.

## 3. Remaining work by workstream

Sizes are rough: S = a session, M = a few sessions, L = a week or more. "Editor" means a person must
author `.uasset`/`.umap` content in the Unreal Editor; agents in this project do not write those files.

### A. Manual cooked/packaged gates — 11 IDs (M, needs Editor)
ACO-09, OPT-09, PNT-10, PHO-10, BRC-10, TRM-10, RHY-10, GHO-10, DIA-10, KNO-08, UI-10.
The procedure, fixture list and what to observe are in `MANUAL_GATES.md`. A bare Development package passed
(`Scripts/Output/20260928-170040-Package-Win64-Development-62eadc/summary.json`), proving the build/cook/stage/pak/archive pipeline; it contains no gate fixtures. Next: author each fixture, package and observe it, then record each gate with `Scripts/Record-ManualGate.ps1` and attachments.

### B. Editor-run, PIE and Blueprint fixtures (foundation + expansion, In Progress) (M, needs Editor)
Base code exists and passes headless tests; each needs a fixture or a run type automation does not do today:
- **PIE / two worlds / travel:** EVT-02, REG-03, STR-08, INS-08, SAV-09, EXP-02, EXP-03, EXP-11, MAP-09.
- **Blueprint consumers:** INT-01, TIM-06, EXP-04.
- **Level assets / editor save-reopen:** STR-06, SAV-01, SAV-02, SAV-04, SAV-10.
- **Real audio device / assets:** AUD-04, AUD-07.
- **Example front end:** INS-02.
- **Shared invariants across the ten Modules 11–20 bases:** EXP-05 (definitions unchanged across sessions/restore),
  EXP-06 (missing providers give Unsupported/Unavailable), EXP-07 (identities/revisions/epochs reject stale commands),
  EXP-08 (idempotent restore), EXP-10 (optional adapters have present/absent tests). Mostly an audit: map each base's
  existing tests to each invariant, add the missing ones, and cite the run. EXP-12 (work logs separate source, build,
  functional, network and packaging evidence) is largely met by the run record in `DEVELOPMENT_STATUS.md`; review and close.
- Note: SAV-04 and SAV-10 say "code tested / roundtrip verified" but are still In Progress; decide what is missing or promote them with evidence.

### C. Isolation, portability and cooked content (L)
- **Compile/header isolation complete:** `Verify-PluginIsolation.ps1` passed Core-only and all 40 Core-plus-feature builds with sibling directories physically absent; each public header compiled in a separate consumer translation unit. Evidence: `Scripts/Output/20260928-152202-PluginIsolation-1b0a78/summary.json`.
- **Headless runtime startup complete:** `Verify-IsolatedStartup.ps1` launched Core-only and all 40 Core-plus-feature hosts (41/41); expected runtime modules loaded, a world reached play, and each process shut down cleanly. Evidence: `Scripts/Output/20260928-174055-IsolatedStartup-c9a914/summary.json`. Feature behavior, PIE, bridge-on tests, and performance rows remain. The startup evidence closes INS-07 and EXP-01; the separate two-host sample gate above closes REL-02 and REL21-02 for portability.
- **Two clean consumer hosts passed:** `CppConsumer` built all 41 runtime modules using an external grouped plugin root; the nested `NonCharacterInteraction` project staged only Core + Interaction into a standard project-local plugin tree and completed a headless interaction between plain actors. Both plugin snapshots matched source. Evidence: `Scripts/Output/20260928-190206-CH-9d1ed1/summary.json`. This closes the second-host portability criteria REL-02 and REL21-02.
- `Scripts/Validate-Workspace.ps1` passed its static preflight (`Scripts/Output/20260928-181720-WorkspaceValidation-282fbf/summary.json`).
- EXP-09: authored content and localization load from a real cooked Win64 build; the bare-host package run proves the pipeline only.
- The broad C++ host is build-only in the final sample-host gate. An exploratory headless launch failed while loading Dialogue automation types with a duplicate `DocDialogueParticipant` default-object fatal; this is recorded by the superseded `20260928-181813-ConsumerHosts-6a8ab5` summary. The final gate claims its broad build and the focused local-host runtime flow, not broad all-plugin startup through the external root.

### D. Networking profiles (L)
No transport exists. Requirements that need real server/client, late join or dedicated-server evidence:
INT-08, TIM-07, SEQ-07, ACT-09, WEA-09, WEA-11, SFC-10, DIA-11, OBJ-11, SCH-11, INV-11, UNL-11, CROSS-08, REL21-09.
Several have authority-aware base tests already; the network half is untouched. Decide which plugins will
actually advertise a network profile; label the rest local-only (REL21-09 allows that if stated).

### E. Bridges and utilities (L)
`Bridges/` is empty; no bridge plugin exists.
- **Foundation bridges/utilities:** STR-07 (World Partition), SEQ-06 (Streaming), INS-06 (DocInspectionMedia),
  ACT-06 (DocWorldActivationISM), ACT-07 (DocObjectPool), INT-09, AUD-05 (Quartz).
- **Expansion bridges:** WEA-10 (renderer adapters), SCH-09 (Smart Objects), SCH-10 (Mass/WorldActivation),
  DIA-12, UI-11 (Inspection/Sequences), UI-12 (CommonUI).
- **Cross-feature requirements:** CROSS-01..07 (Modules 11–20) and CROSS21-01..20 with the five integration
  scenarios in handoff Section 23.6 (machine room, optical mechanism, investigation, timed activity, maintenance terminal).
- Pick a "selected integration profile" first (handoff milestone 13.5). Unselected bridges stay listed as deferred, not failed.

### F. Editor tooling (M)
CORE-07 (editor menu registration) is deferred by decision D-008 until a feature editor panel exists.
No plugin has an editor module; REL-05 expects authoring tools where the handoff calls for them.

### G. Performance (M)
`PERFORMANCE_BASELINE.md` has no measurements. Needed for ACT-05, the "stress/budget" halves of the
isolation rows in C, REL-08 and REL21-10. Record hardware, RHI, workload and budget for each.

### H. Release (after A–G for the chosen profile) (M)
REL-01..08 (Modules 1–20) and REL21-01..12 (Modules 21–40). The unticked packaging gates in
`RELEASE_CHECKLIST.md` (cooked soft references, clean extraction, launch/travel/shutdown, manifest and checksum,
two clean consumer hosts) are the concrete form of these. Per handoff Section 37, a release names its capability
profile and lists deferred work; it must not be called complete because descriptors exist.

## 4. Suggested order

1. Author the 11 editor fixtures, package and observe them, then record the manual cooked gates.
2. Complete B's PIE, Blueprint and editor-run fixtures.
3. Choose the release profile: which networking (D) and which bridges (E) are in scope. Everything else is deferred explicitly.
4. Complete D/E for the chosen profile, then G, then H.

A realistic first release is "Base profile, local-only, no bridges": it needs Section 2, C, A, B, G and the
base parts of H. Networking and bridges can follow as separate profiles.

## 5. Working rules that held during this project

- Never claim a build or test passed without a `Scripts/Output/<run>/summary.json` that says so; cite the run.
- A requirement is Verified only with fresh evidence on the current source; manual parts need a recorded gate.
- Do not write text into `.uasset`/`.umap`; fixtures are authored in the editor.
- Plugins depend only on DocModularCore and engine modules; cross-feature behavior goes in bridges.
- Back up files (and note their md5) before overwriting; do not delete user files.
- Do not push, publish or install marketplace content without the owner's explicit go-ahead.
