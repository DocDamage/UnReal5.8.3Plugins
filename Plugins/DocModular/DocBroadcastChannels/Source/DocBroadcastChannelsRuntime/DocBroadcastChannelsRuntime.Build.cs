using UnrealBuildTool;

public class DocBroadcastChannelsRuntime : ModuleRules
{
	public DocBroadcastChannelsRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"DocModularCoreRuntime"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DeveloperSettings"
			}
		);
	}
}
