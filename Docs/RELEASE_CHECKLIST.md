# Release Checklist — 40-Module Base Gameplay Systems Suite

Target engine: Unreal Engine 5.8.3 (CL 58210709, branch `++UE5+Release-5.8`) on Windows x64.
Suite scope: 41 runtime plugins (`DocModularCore` + Modules 1 through 40).
Current profile: base plugins only; no bridges, editor modules, or authored content assets.

## Base Suite Quality Gates

- [x] **4-Target Host Build Matrix Pass**: Development Editor (Unity), Development Editor (Non-Unity), Runtime Game Development, and Runtime Game Shipping Win64 builds compiled with exit code 0.
  - Editor Unity: `Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96`
  - Editor Non-Unity: `Scripts/Output/20260928-144641-DocModularDevEditor-Development-NoUnity-223d59`
  - Game Development: `Scripts/Output/20260928-144851-DocModularDev-Development-c90343`
  - Game Shipping: `Scripts/Output/20260928-144854-DocModularDev-Shipping-cae04b`
- [x] **No Editor Dependency Leakage**: Runtime modules compiled in Game Development and Game Shipping configurations (see the build evidence above).
- [ ] **Zero Sibling Plugin Coupling**: Dependency metadata/source inspection shows only Core and engine dependencies, but Core-plus-one-feature builds with sibling plugins physically absent have not run. See `Docs/DEPENDENCY_MATRIX.md`.
- [x] **Full Automated Test Suite**: All 391 `Doc.*` tests passed, with 0 failures: `Scripts/Output/20260928-144857-Automation-Doc-95b9e5`.
- Source SHA-256 for these runs: `79C613832807F1FF77E90C9D2F715FDB56D0326BCA80521C68CD04294166A448`.
- [ ] **Complete Requirement Traceability**: 344 of 467 requirement IDs are Verified; 11 are Partial, 51 In Progress, and 61 Not Started.

| Scope | Verified | Partial | In Progress | Not Started | Total |
|---|---:|---:|---:|---:|---:|
| Modules 1–10 | 58 | 0 | 18 | 11 | 87 |
| Modules 11–20 | 94 | 3 | 33 | 18 | 148 |
| Modules 21–40 | 192 | 8 | 0 | 32 | 232 |
| **Total** | **344** | **11** | **51** | **61** | **467** |

- [x] **Documentation & Architecture Standards**: Each plugin has a README; architectural decisions are recorded in `Docs/DECISIONS.md` (D-001 through D-050).

## Packaging & Binary Release Gates (Post-Base / Distribution)

- [ ] Cooked package resolves every soft reference, tag, config, and localization entry across a standalone packaged game.
- [ ] Clean extraction install (no developer paths, cached binaries, or host content).
- [ ] Repeated launch, map travel, and shutdown scenario in the packaged build.
- [ ] File manifest and archive checksum produced; extraction verified.
- [ ] Two clean consumer hosts prove portability without sibling plugin existence.
