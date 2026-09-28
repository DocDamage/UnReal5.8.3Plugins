# DocModular Handoff: What Is Left to Finish

Written 2026-09-28. Read with `DEVELOPMENT_STATUS.md` (evidence and run history) and the three
traceability docs (per-requirement state). This file lists remaining work only.

## 1. Where the project stands

- **Code:** DocModularCore plus 40 gameplay plugins, all runtime-only base code, in `Plugins/DocModular`.
  Each plugin depends only on DocModularCore and engine modules.
- **Verified:** all four host builds (Editor unity, Editor non-unity, Game Development, Game Shipping)
  and the full automation suite, 391/391, in `Scripts/Output/20260928-125758-Automation-Doc-d52e2e`
  (source SHA-256 `4B97CBB1…2D5C42`).
- **Not yet run on the current tree:** INV-11 delta-trim assertions, `Scripts/Package-Host.ps1`,
  `Scripts/Record-ManualGate.ps1`, `.gitignore` changes, and these docs. The next `Run-Verify.cmd` covers the first.
- **Version control:** local git on `main`, remote `origin` = https://github.com/DocDamage/UnReal5.8.3Plugins.
  One local commit ahead of GitHub; **not pushed**.
- **Requirement state (all three traceability docs):**

| Doc | Verified | Partial (manual gate) | In Progress | Not Started | Untraced |
|---|---|---|---|---|---|
| `REQUIREMENTS_TRACEABILITY.md` (Modules 1–10, 87 IDs) | 58 | 0 | 18 | 11 | 0 |
| `EXPANSION_TRACEABILITY.md` (Modules 11–20, 148 IDs) | 94 | 3 | 33 | 18 | 0 |
| `MODULES_21_40_TRACEABILITY.md` (Modules 21–40) | 192 | 8 | 0 | 0 | **32** (CROSS21-01..20, REL21-01..12) |

The automated base logic is essentially done. What is left is the work automation cannot reach:
editor-authored fixtures, cooked/packaged runs, real networking, bridges between plugins,
isolation/portability proof, performance measurement, and release paperwork.

## 2. Do first (small, unblocks everything else)

1. **Run `Run-Verify.cmd`** on the committed tree. Expect 391/391; the INV-11 test now also checks
   that delta-log trimming never serves part of a revision. Fix anything that fails before continuing.
2. **Remove the Android File Server token** from `Config/DefaultEngine.ini` (`SecurityToken=…`, editor-generated)
   or regenerate it, before the GitHub repo is public.
3. **Push** `main` (`git push origin main` on Windows). If git reports "dubious ownership", run
   `git config --global --add safe.directory "F:/Reusable Unreal Modules"`. Commits are authored as
   DocDamage <thectproducer@gmail.com>; change `git config user.email` first if GitHub should link a different address.
4. **Correct `RELEASE_CHECKLIST.md`.** Its ticked "Base Suite Quality Gates" overstate the state: they cite
   Sep 27 runs, claim "100% traceability" (there are 51 In Progress, 29 Not Started, 11 Partial and 32 untraced IDs),
   and tick "Zero Sibling Plugin Coupling" although no isolation build has ever run.
5. **Trace the missing IDs.** Add CROSS21-01..20 (handoff Section 23.7) and REL21-01..12 (Section 37) to
   `MODULES_21_40_TRACEABILITY.md` as Not Started, so they stop being invisible.

## 3. Remaining work by workstream

Sizes are rough: S = a session, M = a few sessions, L = a week or more. "Editor" means a person must
author `.uasset`/`.umap` content in the Unreal Editor; agents in this project do not write those files.

### A. Manual cooked/packaged gates — 11 IDs (M, needs Editor)
ACO-09, OPT-09, PNT-10, PHO-10, BRC-10, TRM-10, RHY-10, GHO-10, DIA-10, KNO-08, UI-10.
The procedure, fixture list and what to observe are in `MANUAL_GATES.md`. Steps: author each fixture,
run `Scripts/Package-Host.ps1` (first run will also prove packaging works at all), then record each gate with
`Scripts/Record-ManualGate.ps1` with attachments. Both scripts are authored but have never run.

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
- Write `Scripts/Verify-PluginIsolation.ps1`: build a host with Core plus one plugin, siblings physically absent,
  for each of the 40 plugins. Covers INS-07, EXP-01, MAP-12, WEA-12, SFC-12, DIA-12 (base part), OBJ-12, SCH-12,
  KNO-12, INV-12, UNL-12, UI-12 (base part), REL-02, REL21-02.
- Build the two structurally different clean sample hosts (`Samples/` is empty) for portability (REL-02, REL21-02).
- Write `Scripts/Validate-Workspace.ps1` (planned in the status doc, never created).
- EXP-09: content and localization load from a real cooked Win64 build (after workstream A's packaging works).

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

1. Section 2 items (verify, token, push, checklist, trace missing IDs).
2. C: isolation script first. It is pure scripting, runs unattended, and closes the most rows.
3. A: first packaged build, then the 11 manual gates.
4. B: PIE/Blueprint fixtures, in the same editor session as A where possible.
5. Choose the release profile: which networking (D) and which bridges (E) are in scope. Everything else is deferred explicitly.
6. D/E for the chosen profile, then G, then H.

A realistic first release is "Base profile, local-only, no bridges": it needs Section 2, C, A, B, G and the
base parts of H. Networking and bridges can follow as separate profiles.

## 5. Working rules that held during this project

- Never claim a build or test passed without a `Scripts/Output/<run>/summary.json` that says so; cite the run.
- A requirement is Verified only with fresh evidence on the current source; manual parts need a recorded gate.
- Do not write text into `.uasset`/`.umap`; fixtures are authored in the editor.
- Plugins depend only on DocModularCore and engine modules; cross-feature behavior goes in bridges.
- Back up files (and note their md5) before overwriting; do not delete user files.
- Do not push, publish or install marketplace content without the owner's explicit go-ahead.
