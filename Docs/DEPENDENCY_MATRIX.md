# Dependency Matrix

Module dependencies (`*.Build.cs`) and plugin descriptor dependencies (`*.uplugin`) are listed separately. This matrix was refreshed from all 41 plugin sources after the isolated build run below. The run's source fingerprint is recorded as evidence; this dependency table was updated afterward as a derived status record.

## Implemented plugins

| Plugin | Module (Runtime) | Public module dependencies | Private module dependencies | Descriptor plugin dependencies |
|---|---|---|---|---|
| DocAcousticSpaces | DocAcousticSpacesRuntime | Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, DocModularCoreRuntime | none | DocModularCore |
| DocAdaptiveAudio | DocAdaptiveAudioRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocAssemblyMaintenance | DocAssemblyMaintenanceRuntime | Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, DocModularCoreRuntime | none | DocModularCore |
| DocBroadcastChannels | DocBroadcastChannelsRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime | DeveloperSettings | DocModularCore |
| DocDialogue | DocDialogueRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocEvents | DocEventsRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocEvidenceDeduction | DocEvidenceDeductionRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocFluidNetworks | DocFluidNetworksRuntime | Core, CoreUObject, Engine, DeveloperSettings, DocModularCoreRuntime | none | DocModularCore |
| DocGameFrameworkUI | DocGameFrameworkUIRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings, InputCore | none | DocModularCore |
| DocGestureRecognition | DocGestureRecognitionRuntime | Core, CoreUObject, Engine, DeveloperSettings, DocModularCoreRuntime | none | DocModularCore |
| DocInspection | DocInspectionRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings, EnhancedInput, InputCore | none | DocModularCore, EnhancedInput |
| DocInteraction | DocInteractionRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, EnhancedInput | none | DocModularCore, EnhancedInput |
| DocInventoryItems | DocInventoryItemsRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocKnowledgeCodex | DocKnowledgeCodexRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocMapNavigation | DocMapNavigationRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocMaterialReactions | DocMaterialReactionsRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocMechanicalNetworks | DocMechanicalNetworksRuntime | Core, CoreUObject, Engine, DeveloperSettings, DocModularCoreRuntime | none | DocModularCore |
| DocModContent | DocModContentRuntime | Core, CoreUObject, Engine, DeveloperSettings, Json, JsonUtilities, DocModularCoreRuntime | none | DocModularCore |
| DocModularCore | DocModularCoreRuntime | Core, CoreUObject, Engine, GameplayTags | none | none |
| DocNPCSchedules | DocNPCSchedulesRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocOpticalBeams | DocOpticalBeamsRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocPhotography | DocPhotographyRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | RHI, RenderCore | DocModularCore |
| DocPlaytestRecorder | DocPlaytestRecorderRuntime | Core, CoreUObject, Engine, DeveloperSettings, Json, JsonUtilities, DocModularCoreRuntime | none | DocModularCore |
| DocPowerNetworks | DocPowerNetworksRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocPuzzleMechanisms | DocPuzzleMechanismsRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocQuestObjectives | DocQuestObjectivesRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocRaceTiming | DocRaceTimingRuntime | Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, DocModularCoreRuntime | none | DocModularCore |
| DocRegions | DocRegionsRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime | none | DocModularCore |
| DocReplayGhosts | DocReplayGhostsRuntime | Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, DocModularCoreRuntime | none | DocModularCore |
| DocRhythmChallenges | DocRhythmChallengesRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocSave | DocSaveRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocSequences | DocSequencesRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings, LevelSequence, MovieScene | none | DocModularCore |
| DocServiceQueues | DocServiceQueuesRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocStreaming | DocStreamingRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime | none | DocModularCore |
| DocSurfaceFeedback | DocSurfaceFeedbackRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings, PhysicsCore | none | DocModularCore |
| DocSurfacePainting | DocSurfacePaintingRuntime | Core, CoreUObject, Engine, GameplayTags, DeveloperSettings, DocModularCoreRuntime | none | DocModularCore |
| DocTime | DocTimeRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocUnlocksProgression | DocUnlocksProgressionRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocWeather | DocWeatherRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocWorldActivation | DocWorldActivationRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime, DeveloperSettings | none | DocModularCore |
| DocWorldTerminals | DocWorldTerminalsRuntime | Core, CoreUObject, Engine, GameplayTags, DocModularCoreRuntime | DeveloperSettings | DocModularCore |
There are no sibling feature dependencies in the 40 feature plugin descriptors. The isolated builds also succeeded with all other DocModular plugin directories physically absent. Public and private module dependencies shown above resolve to the engine or the explicitly declared Core runtime module.

No Editor module exists in any plugin.

## Host project

| Host | Module | Depends on | Purpose |
|---|---|---|---|
| DocModularDev | DocModularDev (Runtime, primary game module) | Core, CoreUObject, Engine (public); GameplayTags, InputCore and all 41 DocModular runtime modules (private) | Build/test host; separate-consumer compile checks for Core (CORE-06) and features (EXP-04) |

`DocModularDev.uproject` enables DocModularCore, all 40 features, and EnhancedInput.

## Isolation evidence

Engine: Unreal Engine 5.8.3, CL 58210709. Non-unity `DocIsolationHostEditor Win64 Development` builds compiled each public header in its own external consumer translation unit.

| Check | Result | Evidence |
|---|---|---|
| Core alone, with no feature plugin present | Passed | `Scripts/Output/20260928-152202-PluginIsolation-1b0a78/summary.json` |
| Core + exactly one of the 40 features, every other DocModular plugin absent | Passed, 40/40 | `Scripts/Output/20260928-152202-PluginIsolation-1b0a78/summary.json` |
| Total isolated hosts | Passed, 41/41; zero failed builds | `Scripts/Output/20260928-152202-PluginIsolation-1b0a78/summary.json` |
| Expected physical plugin set in every host | Passed; Core only or Core plus target | `presentDocModularPlugins` in the run summary |
| Sibling descriptor dependency check | Passed; 0 sibling references | `siblingDescriptorDependencies` in the run summary |
| Runtime startup, Core-only + each Core-and-feature host | Passed, 41/41 | `Scripts/Output/20260928-174055-IsolatedStartup-c9a914/summary.json` (runtime modules loaded; world reached play; clean shutdown; staged plugin source matched current repository source) |
| PIE, cooked behavior, second-host portability | Not Run | Startup smoke proves module/world startup only; no feature behavior or portability beyond generated isolation hosts |

The verifier writes each staged host, logs and the incremental summary under `Scripts/Output/<run-id>/`. Only the summary is intended for version control.

## Planned bridges

None created. Bridges from both handoffs remain backlog items (D-013: `Plugins/DocModularBridges/`), including the Framework UI CommonUI bridge, Save/Time/Events/Quest/Inventory/Unlock/Knowledge adapters, World Partition streaming, Quartz, Smart Objects, Mass/Activation, renderer weather adapters and Inspection media.
