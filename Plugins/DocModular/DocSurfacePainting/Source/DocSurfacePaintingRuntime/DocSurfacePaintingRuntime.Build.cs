using UnrealBuildTool;

public class DocSurfacePaintingRuntime : ModuleRules
{
	public DocSurfacePaintingRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"GameplayTags",
				"DeveloperSettings",
				"DocModularCoreRuntime"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
