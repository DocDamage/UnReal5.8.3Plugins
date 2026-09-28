// DocQuestObjectivesRuntime
// Dependency rule (handoffs 2.2 / expansion 2.1): DocModularCoreRuntime plus
// engine modules essential to this feature's base behaviour. Never a sibling
// DocModular feature or bridge.
using UnrealBuildTool;

public class DocQuestObjectivesRuntime : ModuleRules
{
	public DocQuestObjectivesRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DocModularCoreRuntime",
			"DeveloperSettings"
		});
	}
}
