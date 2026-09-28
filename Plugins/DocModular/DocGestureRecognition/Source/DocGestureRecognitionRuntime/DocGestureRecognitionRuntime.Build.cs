using UnrealBuildTool;

public class DocGestureRecognitionRuntime : ModuleRules
{
	public DocGestureRecognitionRuntime(ReadOnlyTargetRules Target) : base(Target)
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
