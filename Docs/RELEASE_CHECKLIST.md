# Release Checklist — 40-Module Base Gameplay Systems Suite

Target engine: Unreal Engine 5.8.3 (CL 58210709, branch `++UE5+Release-5.8`) on Windows x64.
Suite scope: 41 runtime plugins (`DocModularCore` + Modules 1 through 40).
Current profile: base plugins only; no bridges, editor modules, or authored content assets.

## Base Suite Quality Gates

- [x] **4-Target Host Build Matrix Pass**: Development Editor (Unity), Development Editor (Non-Unity), Runtime Game Development, and Runtime Game Shipping Win64 builds compiled with exit code 0.
  - Editor Unity: `Scripts/Output/20260928-165742-DocModularDevEditor-Development-b4f3aa`
  - Editor Non-Unity: `Scripts/Output/20260928-165744-DocModularDevEditor-Development-NoUnity-4a7475`
  - Game Development: `Scripts/Output/20260928-170002-DocModularDev-Development-70ecee`
  - Game Shipping: `Scripts/Output/20260928-170005-DocModularDev-Shipping-164490`
- [x] **No Editor Dependency Leakage**: Runtime modules compiled in Game Development and Game Shipping configurations (see the build evidence above).
- [x] **Zero Sibling Plugin Coupling**: Core-only and Core-plus-one-feature non-unity builds passed for all 40 features with every sibling directory absent. Evidence: `Scripts/Output/20260928-152202-PluginIsolation-1b0a78/summary.json`. This gate covers compilation and public-header isolation; runtime startup and second-host portability remain open.
- [x] **Full Automated Test Suite**: All 391 `Doc.*` tests passed, with 0 failures: `Scripts/Output/20260928-170007-Automation-Doc-eee4f4`.
- Latest full-suite and Development package source SHA-256: `251DCB2A320E8795764B7AFC8EE3028B89D77F1E5FCA8C65C97EEC11DD66DF7E`. The isolation matrix summary records its earlier source snapshot `8A54FD0208FA36599DAF1419E439EC2E6ED778BB4EA8E64EB6A83429B5749351`. Subsequent edits update only status wording in documentation; no implementation, configuration, descriptor, or build-script inputs changed.
- [ ] **Complete Requirement Traceability**: 344 of 467 requirement IDs are Verified; 11 are Partial, 53 In Progress, and 59 Not Started.

| Scope | Verified | Partial | In Progress | Not Started | Total |
|---|---:|---:|---:|---:|---:|
| Modules 1–10 | 58 | 0 | 18 | 11 | 87 |
| Modules 11–20 | 94 | 3 | 34 | 17 | 148 |
| Modules 21–40 | 192 | 8 | 1 | 31 | 232 |
| **Total** | **344** | **11** | **53** | **59** | **467** |

- [x] **Documentation & Architecture Standards**: Each plugin has a README; architectural decisions are recorded in `Docs/DECISIONS.md` (D-001 through D-052).

## Packaging & Binary Release Gates (Post-Base / Distribution)

- [x] **Development Win64 package pipeline**: build, cook, stage, pak and archive completed; evidence: `Scripts/Output/20260928-170040-Package-Win64-Development-62eadc/summary.json`. The archive contains `DocModularDev.exe` and a `.pak`, with no authored manual-gate fixtures.
- [ ] Cooked package resolves every soft reference, tag, config, and localization entry across a standalone packaged game.
- [ ] Clean extraction install (no developer paths, cached binaries, or host content).
- [ ] Repeated launch, map travel, and shutdown scenario in the packaged build.
- [ ] File manifest and archive checksum produced; extraction verified.
- [ ] Two clean consumer hosts prove portability without sibling plugin existence.
