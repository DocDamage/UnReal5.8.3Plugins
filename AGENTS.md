# Repository instructions for coding agents

This repository builds the **DocModular** reusable Unreal Engine 5.8.3 gameplay plugin suite. It is a library, not a game.

Two specifications, in this order:
1. `Docs/UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff.md` — Core + modules 1–10 (Revision 2). The expansion document calls this file `..._IDE_Handoff_v2.md`; it is the same document. Read Sections 0–4 and 27–29.
2. `Docs/UE5_8_3_Modular_Gameplay_Systems_Modules_11_20_IDE_Handoff.md` — modules 11–20 (expansion). Read Sections 0–2, 13–15 and 24–27, and `Docs/FOUNDATION_COMPATIBILITY.md`.

Read `Docs/DEVELOPMENT_STATUS.md` before editing. The build order across both documents is fixed in `Docs/DECISIONS.md` D-011.

Rules that are easy to break:
- One bounded milestone at a time, in the D-011 order: Core → Events → Interaction → Regions → Time → Streaming → Save → Audio → Sequences → Inspection → Activation → SurfaceFeedback → MapNavigation → Weather → NPCSchedules → Dialogue → QuestObjectives → KnowledgeCodex → UnlocksProgression → InventoryItems → GameFrameworkUI.
- A feature plugin depends only on `DocModularCoreRuntime` and essential engine modules. Never on a sibling feature. Cross-feature behaviour goes in a bridge plugin.
- The installed engine at `C:\Program Files\UE_5.8` (5.8.3, CL 58210709) is the authority for API signatures. Check headers before using an API.
- Never write text into `.uasset`/`.umap` files. Never report a build or test as passed without a run record under `Scripts/Output/`.
- States: Not Started → In Progress → Implemented / Unverified → Verified, or Blocked with cause and next action.
- Update `Docs/DEVELOPMENT_STATUS.md` and the traceability file (`REQUIREMENTS_TRACEABILITY.md` for modules 1–10, `EXPANSION_TRACEABILITY.md` for 11–20) after every meaningful slice.
- Shared primitives (owner scope, condition outcome, effect key) enter Core only with their first consumer (D-015). Bridges go in `Plugins/DocModularBridges/` (D-013). Record design choices in `Docs/DECISIONS.md`.
- Do not push, publish, or install marketplace content without explicit authorization.

Build and test (PowerShell, repository root):
```powershell
.\Scripts\Build-Host.ps1     -EngineRoot 'C:\Program Files\UE_5.8'
.\Scripts\Build-Host.ps1     -EngineRoot 'C:\Program Files\UE_5.8' -NoUnity
.\Scripts\Run-Automation.ps1 -EngineRoot 'C:\Program Files\UE_5.8' -Filter 'Doc.Core' -ExpectedMinimumTests 12
.\Scripts\Verify-Suite.ps1    -EngineRoot 'C:\Program Files\UE_5.8'   # all plugins: 4 builds + 191 Doc.* tests
```
