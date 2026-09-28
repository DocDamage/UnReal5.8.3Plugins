using UnrealBuildTool;

public class DocMechanicalNetworksRuntime : ModuleRules
{
	public DocMechanicalNetworksRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
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
