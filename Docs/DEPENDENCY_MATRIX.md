# Dependency Matrix

Module dependencies (`*.Build.cs`) and plugin descriptor dependencies (`*.uplugin`) are recorded separately; one does not replace the other. Read from source 2026-09-27; nothing has been built.

## Implemented plugins

Every feature module's public dependencies are Core, CoreUObject, Engine, GameplayTags and DocModularCoreRuntime, plus the extras shown. No module has private dependencies. No feature references a sibling feature (source inspection only; an automated check is backlog).

| Plugin | Module (Runtime) | Public module deps | Descriptor plugin deps |
|---|---|---|---|
| DocModularCore | DocModularCoreRuntime | Core, CoreUObject, Engine, GameplayTags | none |
| DocEvents | DocEventsRuntime | + DeveloperSettings | DocModularCore |
| DocInteraction | DocInteractionRuntime | + EnhancedInput | DocModularCore, EnhancedInput |
| DocRegions | DocRegionsRuntime | (Core set only) | DocModularCore |
| DocTime | DocTimeRuntime | + DeveloperSettings | DocModularCore |
| DocStreaming | DocStreamingRuntime | (Core set only) | DocModularCore |
| DocSave | DocSaveRuntime | + DeveloperSettings | DocModularCore |
| DocAdaptiveAudio | DocAdaptiveAudioRuntime | + DeveloperSettings | DocModularCore |
| DocSequences | DocSequencesRuntime | + DeveloperSettings, LevelSequence, MovieScene | DocModularCore |
| DocInspection | DocInspectionRuntime | + DeveloperSettings, EnhancedInput, InputCore | DocModularCore, EnhancedInput |
| DocWorldActivation | DocWorldActivationRuntime | + DeveloperSettings | DocModularCore |
| DocSurfaceFeedback | DocSurfaceFeedbackRuntime | + DeveloperSettings, PhysicsCore | DocModularCore |
| DocMapNavigation | DocMapNavigationRuntime | + DeveloperSettings | DocModularCore |
| DocWeather | DocWeatherRuntime | + DeveloperSettings | DocModularCore |
| DocNPCSchedules | DocNPCSchedulesRuntime | + DeveloperSettings | DocModularCore |
| DocDialogue | DocDialogueRuntime | + DeveloperSettings | DocModularCore |
| DocQuestObjectives | DocQuestObjectivesRuntime | + DeveloperSettings | DocModularCore |
| DocKnowledgeCodex | DocKnowledgeCodexRuntime | + DeveloperSettings | DocModularCore |
| DocUnlocksProgression | DocUnlocksProgressionRuntime | + DeveloperSettings | DocModularCore |
| DocInventoryItems | DocInventoryItemsRuntime | + DeveloperSettings | DocModularCore |
| DocGameFrameworkUI | DocGameFrameworkUIRuntime | + DeveloperSettings, InputCore | DocModularCore |

Why the extras are public: settings objects derive from `UDeveloperSettings`; Inspection and Interaction expose `UInputAction`/`UInputMappingContext` properties (EnhancedInput) and `FKey` (InputCore); Framework UI exposes `FKey` in input structs; Sequences exposes `ULevelSequence`; SurfaceFeedback exposes `EPhysicalSurface`/`UPhysicalMaterial` (PhysicsCore).

No Editor module exists in any plugin.

## Host project

| Host | Module | Depends on | Purpose |
|---|---|---|---|
| DocModularDev | DocModularDev (Runtime, primary game module) | Core, CoreUObject, Engine (public); GameplayTags, InputCore and every DocModular runtime module (private) | Build/test host; separate-consumer compile checks for Core (CORE-06) and every feature (EXP-04) |

`DocModularDev.uproject` enables all 21 DocModular plugins and EnhancedInput.

## Isolation evidence

| Check | Result |
|---|---|
| Core builds with no feature plugin present | Not Run |
| Core + exactly one feature, all others absent (EXP-01 / XS-ISOLATION) | Not Run — needs `Verify-PluginIsolation.ps1` (backlog) |
| No forbidden sibling includes | Source inspection only |
| Unique reflected type names across plugins | Source inspection: one collision found and fixed (`FDocNPCScheduleEntry`, D-027) |

## Planned bridges

None created. Bridges from both handoffs remain backlog items (D-013: `Plugins/DocModularBridges/`), including the Framework UI CommonUI bridge, Save/Time/Events/Quest/Inventory/Unlock/Knowledge adapters, World Partition streaming, Quartz, Smart Objects, Mass/Activation, renderer weather adapters and Inspection media.
