# Test Matrix

| Host | Plugins present | Configuration | Scope | Wrapper | Last run |
|---|---|---|---|---|---|
| DocModularDev (repo root) | All 21 DocModular plugins + EnhancedInput | Development Editor Win64 (unity) | Every plugin compiles; CORE-06 and EXP-04 consumer compile checks | `Scripts/Build-Host.ps1` | Not Run |
| DocModularDev | All | Development Editor Win64 (`-NoUnity`) | Hidden-include check (Section 22) | `Scripts/Build-Host.ps1 -NoUnity` | Not Run |
| DocModularDev | All | Development Win64 (`-Target DocModularDev`) | Runtime target compiles without editor modules | `Scripts/Build-Host.ps1` | Not Run |
| DocModularDev | All | Shipping Win64 | Shipping compile | `Scripts/Build-Host.ps1 -Configuration Shipping -Target DocModularDev` | Not Run |
| DocModularDev | All | Editor, NullRHI (logic-only) | `Doc.*` (191 tests authored) | `Scripts/Verify-Suite.ps1` (or `-SkipBuilds -Only <filter>`) | Not Run |
| DocModularDev | All | Editor, NullRHI | `Doc.Core.*` (12 tests authored) | `Scripts/Verify-M1.1.ps1` | Not Run |
| Isolation hosts (Core + one feature) | — | — | EXP-01 / XS-ISOLATION | `Verify-PluginIsolation.ps1` (backlog) | Not created |
| Samples/* clean hosts (2) | — | — | Portability gate (Section 1) | — | Not created |

Authored tests per filter: Doc.Core 12, Doc.Events 8, Doc.Interaction 7, Doc.Regions 5, Doc.Time 6, Doc.Streaming 6, Doc.Save 12, Doc.Audio 8, Doc.Sequences 5, Doc.Inspection 5, Doc.Activation 5, Doc.Surface 11, Doc.Map 10, Doc.Weather 9, Doc.Schedule 9, Doc.Dialogue 13, Doc.Quest 13, Doc.Knowledge 11, Doc.Unlock 12, Doc.Inventory 13, Doc.UI 11.

Exclusions: NullRHI/no-audio runs are logic-only and never count as presentation or audio verification. Tests use native test providers, test clocks and authority overrides; network, cooked, PIE-presentation and profiling evidence need other hosts. Cook/package is not yet scripted (`Package-Host.ps1`, `Verify-PluginIsolation.ps1`, `Validate-Workspace.ps1` remain future deliverables).

Evidence for each run is written to `Scripts/Output/<run-id>/summary.json` with the command, exit code, engine CL, source SHA-256, and log/report paths.
