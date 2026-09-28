using UnrealBuildTool;

public class DocPhotographyRuntime : ModuleRules
{
	public DocPhotographyRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"DocModularCoreRuntime",
				"DeveloperSettings"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RHI",
				"RenderCore"
			}
		);
	}
}
