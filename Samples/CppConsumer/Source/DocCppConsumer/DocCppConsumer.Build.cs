using UnrealBuildTool;

public class DocCppConsumer : ModuleRules
{
	public DocCppConsumer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayTags",
			"InputCore",
			"DocAcousticSpacesRuntime",
			"DocAdaptiveAudioRuntime",
			"DocAssemblyMaintenanceRuntime",
			"DocBroadcastChannelsRuntime",
			"DocDialogueRuntime",
			"DocEventsRuntime",
			"DocEvidenceDeductionRuntime",
			"DocFluidNetworksRuntime",
			"DocGameFrameworkUIRuntime",
			"DocGestureRecognitionRuntime",
			"DocInspectionRuntime",
			"DocInteractionRuntime",
			"DocInventoryItemsRuntime",
			"DocKnowledgeCodexRuntime",
			"DocMapNavigationRuntime",
			"DocMaterialReactionsRuntime",
			"DocMechanicalNetworksRuntime",
			"DocModContentRuntime",
			"DocModularCoreRuntime",
			"DocNPCSchedulesRuntime",
			"DocOpticalBeamsRuntime",
			"DocPhotographyRuntime",
			"DocPlaytestRecorderRuntime",
			"DocPowerNetworksRuntime",
			"DocPuzzleMechanismsRuntime",
			"DocQuestObjectivesRuntime",
			"DocRaceTimingRuntime",
			"DocRegionsRuntime",
			"DocReplayGhostsRuntime",
			"DocRhythmChallengesRuntime",
			"DocSaveRuntime",
			"DocSequencesRuntime",
			"DocServiceQueuesRuntime",
			"DocStreamingRuntime",
			"DocSurfaceFeedbackRuntime",
			"DocSurfacePaintingRuntime",
			"DocTimeRuntime",
			"DocUnlocksProgressionRuntime",
			"DocWeatherRuntime",
			"DocWorldActivationRuntime",
			"DocWorldTerminalsRuntime",
		});
	}
}
