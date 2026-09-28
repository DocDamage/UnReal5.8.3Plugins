// Host module. Acts as the "separate consumer module" for CORE-06 and EXP-04: it
// compiles against every DocModular plugin's public headers only.
using UnrealBuildTool;

public class DocModularDev : ModuleRules
{
	public DocModularDev(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// Every DocModular runtime module, so the host is a separate consumer of each
		// plugin's public headers (CORE-06 / EXP-04 compile checks).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayTags",
			"InputCore",
			"DocModularCoreRuntime",
			"DocEventsRuntime",
			"DocInteractionRuntime",
			"DocRegionsRuntime",
			"DocTimeRuntime",
			"DocStreamingRuntime",
			"DocSaveRuntime",
			"DocAdaptiveAudioRuntime",
			"DocSequencesRuntime",
			"DocInspectionRuntime",
			"DocWorldActivationRuntime",
			"DocSurfaceFeedbackRuntime",
			"DocMapNavigationRuntime",
			"DocWeatherRuntime",
			"DocNPCSchedulesRuntime",
			"DocDialogueRuntime",
			"DocQuestObjectivesRuntime",
			"DocKnowledgeCodexRuntime",
			"DocUnlocksProgressionRuntime",
			"DocInventoryItemsRuntime",
			"DocGameFrameworkUIRuntime"
		});
	}
}
