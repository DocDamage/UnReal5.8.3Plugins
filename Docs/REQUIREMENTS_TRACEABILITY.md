# Requirements Traceability

Source of requirement IDs: `Docs/UE5_8_3_Modular_Gameplay_Systems_IDE_Handoff.md` Sections 4–14 and 19.3. Modules 11–20 (148 IDs: EXP, MAP, WEA, SFC, DIA, OBJ, SCH, KNO, INV, UNL, UI, CROSS, REL) are tracked in `EXPANSION_TRACEABILITY.md`.

States use the handoff vocabulary: Not Started → In Progress → Implemented / Unverified → Verified, plus Blocked. A test listed here is **authored**, not executed, unless the Evidence column cites a run.

Updated 2026-09-28 after the full host matrix and `Doc.*` automation run `Scripts/Output/20260928-144857-Automation-Doc-95b9e5` (391/391; source SHA-256 `79C613832807F1FF77E90C9D2F715FDB56D0326BCA80521C68CD04294166A448`). Of 87 foundation IDs, 58 are Verified, 18 are In Progress, and 11 are Not Started. In Progress means base code exists but the requirement also needs a PIE, cooked, Blueprint, network, or bridge fixture, or has no dedicated test. Core also has tests beyond CORE-01…06 for its expansion primitives: `Doc.Core.OwnerScope`, `Doc.Core.ReceiptLedger`, `Doc.Core.ControlArbiter`, `Doc.Core.ConditionCombine` and `Doc.Core.FeatureRecordSerialization`.

| Requirement | Capability | Implementation | Test name(s) | Evidence | State |
|---|---|---|---|---|---|
| CORE-01 | BASE | Plugins/DocModular/DocModularCore (descriptor, DocModularCoreRuntime) | Build-Host (DocModularDevEditor, Core only) | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| CORE-02 | BASE | DocCoreTags.h/.cpp | Doc.Core.TagRegistration | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| CORE-03 | BASE | DocRequestHandle.h/.cpp | Doc.Core.Handle.Lifecycle; Doc.Core.Handle.NotDurable | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| CORE-04 | BASE | DocPersistentObjectId.h/.cpp | Doc.Core.Identity.RepeatedInstances; Doc.Core.Identity.Serialization | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| CORE-05 | BASE | DocGameplayContext.*, TDocHandleTable scope keys | Doc.Core.WorldIsolation (Core portion; per-feature two-PIE-world tests pending) | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| CORE-06 | BASE | Source/DocModularDev/DocCoreConsumerCompileCheck.cpp | Build-Host (host module compile) + non-unity build | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| CORE-07 | BASE | Not created (no Core editor module yet; deferred until a feature editor panel exists) | Not authored | None | Not Started |
| EVT-01 | BASE | Plugins/DocModular/DocEvents | Doc.Events.Broadcast; Doc.Events.HierarchicalSubscription | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| EVT-02 | BASE | Plugins/DocModular/DocEvents — two-PIE-world part needs a PIE run | Doc.Events.ScopeIsolation | Scripts/Output/20260927-121807-Automation-Doc.Events-e6ad48 (base verified) | In Progress |
| EVT-03 | BASE | Plugins/DocModular/DocEvents | Doc.Events.Unsubscribe | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| EVT-04 | BASE | Plugins/DocModular/DocEvents | Doc.Events.Reentrancy | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| EVT-05 | BASE | Plugins/DocModular/DocEvents | Doc.Events.ScopeIsolation; Doc.Events.Unsubscribe | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| EVT-06 | BASE | Plugins/DocModular/DocEvents | Doc.Events.Scheduled | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| EVT-07 | BASE | Plugins/DocModular/DocEvents | Doc.Events.PersistentPayload | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| EVT-08 | BASE | Plugins/DocModular/DocEvents | Doc.Events.PayloadSchema | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INT-01 | BASE | Plugins/DocModular/DocInteraction — Blueprint-only target needs a Blueprint fixture | Doc.Interaction.BasicInteraction | Scripts/Output/20260927-122318-Automation-Doc.Interaction-77ae67 (base verified) | In Progress |
| INT-02 | BASE | Plugins/DocModular/DocInteraction | Doc.Interaction.CandidateSelection | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INT-03 | BASE | Plugins/DocModular/DocInteraction | Doc.Interaction.ConditionFailure; Doc.Interaction.HoldInteraction | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INT-04 | BASE | Plugins/DocModular/DocInteraction | Doc.Interaction.HoldInteraction; Doc.Interaction.ContinuousAndRepeated | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INT-05 | BASE | Plugins/DocModular/DocInteraction | Doc.Interaction.Concurrency | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INT-06 | BASE | Plugins/DocModular/DocInteraction | Doc.Interaction.Concurrency | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INT-07 | BASE | Plugins/DocModular/DocInteraction | Doc.Interaction.PartialFailure | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INT-08 | NETWORK | Not created — no RPC transport (network) | Not authored | None | Not Started |
| INT-09 | BASE | Not created — no bridge plugins exist yet | Not authored | None | Not Started |
| REG-01 | BASE | Plugins/DocModular/DocRegions | Doc.Regions.NestedPriority; Doc.Regions.PrimaryRegion | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| REG-02 | BASE | Plugins/DocModular/DocRegions | Doc.Regions.EnterExit | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| REG-03 | BASE | Plugins/DocModular/DocRegions — moving volume / teleport need a PIE fixture | Doc.Regions.EnterExit | Scripts/Output/20260927-122612-Automation-Doc.Regions-e73c88 (base verified) | In Progress |
| REG-04 | BASE | Plugins/DocModular/DocRegions | Doc.Regions.UnloadReload | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| REG-05 | BASE | Plugins/DocModular/DocRegions | Doc.Regions.TagQueries | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| REG-06 | BASE | Plugins/DocModular/DocRegions — parent-cycle validation | Doc.Regions.TagQueries | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| REG-07 | BASE | Plugins/DocModular/DocRegions — dependency rule holds in source | Build-Host (non-unity) | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| STR-01 | BASE | Plugins/DocModular/DocStreaming | Doc.Streaming.ReferenceCounting | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| STR-02 | BASE | Plugins/DocModular/DocStreaming | Doc.Streaming.DependencyLoad; Doc.Streaming.DependencyCycle | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| STR-03 | BASE | Plugins/DocModular/DocStreaming | Doc.Streaming.CancelStaleCompletion | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| STR-04 | BASE | Plugins/DocModular/DocStreaming | Doc.Streaming.RepeatedInstances | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| STR-05 | BASE | Plugins/DocModular/DocStreaming | Doc.Streaming.WorldState | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| STR-06 | BASE | Plugins/DocModular/DocStreaming — level-instance backend present; needs level assets and a cooked Win64 host | Not authored | None | In Progress |
| STR-07 | BRIDGE | Not created — World Partition bridge not created | Not authored | None | Not Started |
| STR-08 | BASE | Plugins/DocModular/DocStreaming — release-on-teardown code present; travel fixture not authored | Not authored | None | In Progress |
| TIM-01 | BASE | Plugins/DocModular/DocTime | Doc.Time.Advancement | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| TIM-02 | BASE | Plugins/DocModular/DocTime | Doc.Time.SaveRestore; Doc.Time.TimeScale | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| TIM-03 | BASE | Plugins/DocModular/DocTime | Doc.Time.CalendarRollover | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| TIM-04 | BASE | Plugins/DocModular/DocTime | Doc.Time.EventBoundary | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| TIM-05 | BASE | Plugins/DocModular/DocTime | Doc.Time.TimeScale | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| TIM-06 | BASE | Plugins/DocModular/DocTime — native schedule queries tested; Blueprint parity needs a Blueprint fixture | Doc.Time.Schedules | Scripts/Output/20260927-122654-Automation-Doc.Time-69507b (base verified) | In Progress |
| TIM-07 | NETWORK | Not created — network clock adapter not created | Not authored | None | Not Started |
| AUD-01 | BASE | Plugins/DocModular/DocAdaptiveAudio | Doc.Audio.PriorityAndChannels; Doc.Audio.RemovedRequestsNotRestored | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| AUD-02 | BASE | Plugins/DocModular/DocAdaptiveAudio | Doc.Audio.SupersededAndFailedLoads | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| AUD-03 | BASE | Plugins/DocModular/DocAdaptiveAudio | Doc.Audio.LayersOneShotsLimits; Doc.Audio.EmitterBudget | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| AUD-04 | BASE + ORIGINAL (MetaSound playback) | Plugins/DocModular/DocAdaptiveAudio — playback path present; needs an audio-enabled run with real assets | Not authored | None | In Progress |
| AUD-05 | BRIDGE | Plugins/DocModular/DocAdaptiveAudio — base quantization tested; Quartz bridge not created | Doc.Audio.Quantization | Scripts/Output/20260927-124832-Automation-Doc.Audio-897983 | In Progress |
| AUD-06 | BASE | Plugins/DocModular/DocAdaptiveAudio | Doc.Audio.TeardownAndChurn | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| AUD-07 | BASE | Plugins/DocModular/DocAdaptiveAudio — shared-output policy implemented; needs a real device | Not authored | None | In Progress |
| SEQ-01 | BASE | Plugins/DocModular/DocSequences | Doc.Sequences.PlaybackControls | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SEQ-02 | BASE | Plugins/DocModular/DocSequences | Doc.Sequences.Arbitration | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SEQ-03 | BASE | Plugins/DocModular/DocSequences | Doc.Sequences.FailuresAndWaiting | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SEQ-04 | BASE | Plugins/DocModular/DocSequences | Doc.Sequences.EffectsSkipReplay | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SEQ-05 | BASE | Plugins/DocModular/DocSequences | Doc.Sequences.ControlAndPrerequisites | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SEQ-06 | BRIDGE | Not created — Streaming bridge not created | Not authored | None | Not Started |
| SEQ-07 | NETWORK | Not created — network replication not created | Not authored | None | Not Started |
| INS-01 | BASE | Plugins/DocModular/DocInspection | Doc.Inspection.WorldAndPreview | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INS-02 | BASE | Plugins/DocModular/DocInspection — logic tested; example front end not created | Doc.Inspection.DocumentsAndPages | Scripts/Output/20260927-125205-Automation-Doc.Inspection-66a715 (base verified) | In Progress |
| INS-03 | BASE | Plugins/DocModular/DocInspection | Doc.Inspection.IndependentPlayers | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INS-04 | BASE | Plugins/DocModular/DocInspection | Doc.Inspection.LoadFailureAndCleanup | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INS-05 | BASE | Plugins/DocModular/DocInspection | Doc.Inspection.FocusPoints | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| INS-06 | BRIDGE | Not created — DocInspectionMedia bridge not created | Not authored | None | Not Started |
| INS-07 | BASE | Plugins/DocModular/DocInspection — dependency rule holds in source; isolated build not run | Not authored | None | In Progress |
| INS-08 | BASE | Plugins/DocModular/DocInspection — shared control provider used; sequence coexistence needs a PIE run | Not authored | None | In Progress |
| SAV-01 | BASE | Plugins/DocModular/DocSave — identity code verified in automation; editor save/reopen and stream reload need an editor run | Doc.Save.RoundTrip | Scripts/Output/20260927-124621-Automation-Doc.Save-3fa08d | In Progress |
| SAV-02 | BASE | Plugins/DocModular/DocSave — repeated level instances need level fixtures | Doc.Save.RoundTrip | Scripts/Output/20260927-124621-Automation-Doc.Save-3fa08d | In Progress |
| SAV-03 | BASE | Plugins/DocModular/DocSave | Doc.Save.RuntimeSpawnRestore; Doc.Save.Tombstone; Doc.Save.StreamedOutRetention | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SAV-04 | BASE | Plugins/DocModular/DocSave — two-phase reference resolution code tested | Doc.Save.RoundTrip | Scripts/Output/20260927-124621-Automation-Doc.Save-3fa08d | In Progress |
| SAV-05 | BASE | Plugins/DocModular/DocSave | Doc.Save.Migration | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SAV-06 | BASE | Plugins/DocModular/DocSave | Doc.Save.Revisions | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SAV-07 | BASE | Plugins/DocModular/DocSave | Doc.Save.LocalBackend.FaultInjection; Doc.Save.BadFileKeepsProgress | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SAV-08 | BASE | Plugins/DocModular/DocSave | Doc.Save.Envelope.RejectsBadFiles; Doc.Save.SlotNames; Doc.Save.PayloadAllowlist | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| SAV-09 | BASE | Plugins/DocModular/DocSave — travel handling present; travel fixture not authored | Not authored | None | In Progress |
| SAV-10 | BASE | Plugins/DocModular/DocSave — restore path present; roundtrip restore verified | Doc.Save.RoundTrip | Scripts/Output/20260927-124621-Automation-Doc.Save-3fa08d | In Progress |
| ACT-01 | BASE | Plugins/DocModular/DocWorldActivation | Doc.Activation.DistanceAndManual | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| ACT-02 | BASE | Plugins/DocModular/DocWorldActivation | Doc.Activation.HysteresisAndDwell | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| ACT-03 | BASE | Plugins/DocModular/DocWorldActivation | Doc.Activation.SourcesAndPins | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| ACT-04 | BASE | Plugins/DocModular/DocWorldActivation | Doc.Activation.OwnedStateAndAdapters | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| ACT-05 | BASE | Plugins/DocModular/DocWorldActivation — budget logic tested; load measurement not run | Doc.Activation.BudgetsAndUnload | Scripts/Output/20260927-125850-Automation-Doc.Activation-72de48 (base verified) | In Progress |
| ACT-06 | BRIDGE | Not created — DocWorldActivationISM bridge not created | Not authored | None | Not Started |
| ACT-07 | UTILITY | Not created — DocObjectPool utility not created | Not authored | None | Not Started |
| ACT-08 | BASE | Plugins/DocModular/DocWorldActivation | Doc.Activation.BudgetsAndUnload | Scripts/Output/20260928-144638-DocModularDevEditor-Development-19ca96; Scripts/Output/20260928-144857-Automation-Doc-95b9e5 | Verified |
| ACT-09 | NETWORK | Not created — network replication not created | Not authored | None | Not Started |

## Cross-system gates (Section 19.3)

| Gate | Scope | Implementation | Test name(s) | Evidence | State |
|---|---|---|---|---|---|
| XS-ISOLATION | Core alone; Core + exactly one feature with all other feature directories absent; each bridge on/off | Not created | Not authored | None | Not Started |
| XS-LIFETIME | Two PIE worlds, travel, local-player removal, owner destruction, cancel at each async stage, teardown with pending callbacks | Not created | Not authored | None | Not Started |
| XS-OWNERSHIP | Overlapping audio/streaming/control/pause/activation claims; releasing one preserves others | Not created | Not authored | None | Not Started |
| XS-PERSISTENCE | Repeated level instances, streamed-out dirty state, tombstones, runtime spawns, cross-refs, migration fixtures | Not created | Not authored | None | Not Started |
| XS-DISTRIBUTION | Clean extraction/install, missing optional assets, cooked soft refs, generated BP assets, native deps | Not created | Not authored | None | Not Started |
| XS-ABUSE | Invalid handles, untrusted payloads, oversized saves, event storms, repeated RPCs, bounded queues | Not created | Not authored | None | Not Started |
