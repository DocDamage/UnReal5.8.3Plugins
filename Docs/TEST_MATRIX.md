# Test Matrix

| Host | Plugins present | Configuration | Scope | Wrapper | Last run |
|---|---|---|---|---|---|
| DocModularDev (repo root) | Core + 40 features + EnhancedInput | Development Editor Win64 (unity) | All plugins compile; Core and feature consumer compile checks | `Scripts/Build-Host.ps1` | `Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96` |
| DocModularDev | All | Development Editor Win64 (`-NoUnity`) | Non-unity compile / hidden-include check | `Scripts/Build-Host.ps1 -NoUnity` | `Scripts/Output/20260928-144641-DocModularDevEditor-Development-NoUnity-223d59` |
| DocModularDev | All | Development Win64 (`-Target DocModularDev`) | Runtime target compiles without editor modules | `Scripts/Build-Host.ps1` | `Scripts/Output/20260928-144851-DocModularDev-Development-c90343` |
| DocModularDev | All | Shipping Win64 | Shipping compile | `Scripts/Build-Host.ps1 -Configuration Shipping -Target DocModularDev` | `Scripts/Output/20260928-144854-DocModularDev-Shipping-cae04b` |
| DocModularDev | All | Editor, NullRHI (logic-only) | `Doc.*` (391 tests authored) | `Scripts/Verify-Suite.ps1` (or `-SkipBuilds -Only <filter>`) | `Scripts/Output/20260928-144857-Automation-Doc-95b9e5` (391/391) |
| DocModularDev | All | Editor, NullRHI | `Doc.Core.*` (12 tests authored; included in the full suite above) | `Scripts/Verify-M1.1.ps1` | `Scripts/Output/20260928-144857-Automation-Doc-95b9e5` |
| Isolation hosts (Core + one feature) | — | — | EXP-01 / XS-ISOLATION | `Verify-PluginIsolation.ps1` (backlog) | Not created |
| Samples/* clean hosts (2) | — | — | Portability gate (Section 1) | — | Not created |

Authored tests per filter: Doc.Core 12, Doc.Events 8, Doc.Interaction 7, Doc.Regions 5, Doc.Time 6, Doc.Streaming 6, Doc.Save 12, Doc.Audio 8, Doc.Sequences 5, Doc.Inspection 5, Doc.Activation 5, Doc.Surface 11, Doc.Map 10, Doc.Weather 9, Doc.Schedule 9, Doc.Dialogue 13, Doc.Quest 13, Doc.Knowledge 11, Doc.Unlock 12, Doc.Inventory 13, Doc.UI 11, Doc.Puzzle 10, Doc.Queue 10, Doc.Power 10, Doc.Evidence 10, Doc.Rhythm 10, Doc.Optics 10, Doc.Acoustics 10, Doc.Paint 10, Doc.Material 10, Doc.Photo 10, Doc.Broadcast 10, Doc.Terminal 10, Doc.Gesture 10, Doc.Fluid 10, Doc.Mechanical 10, Doc.Assembly 10, Doc.Ghost 10, Doc.Race 10, Doc.Mod 10, Doc.Playtest 10 (391 total).

Exclusions: NullRHI/no-audio runs are logic-only and do not count as presentation or audio verification. Tests use native test providers, test clocks and authority overrides; network, cooked, PIE-presentation and profiling evidence need other hosts. `Package-Host.ps1` and `Record-ManualGate.ps1` exist but have not run; `Verify-PluginIsolation.ps1`, `Validate-Workspace.ps1`, and clean sample hosts are still planned.

Evidence for each run is written to `Scripts/Output/<run-id>/summary.json` with the command, exit code, engine CL, source SHA-256, and log/report paths. The source fingerprint excludes generated output and mutable run/status records as specified by D-050.
