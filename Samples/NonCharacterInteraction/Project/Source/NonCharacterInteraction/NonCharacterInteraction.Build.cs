using UnrealBuildTool;

public class NonCharacterInteraction : ModuleRules
{
	public NonCharacterInteraction(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DocModularCoreRuntime",
			"DocInteractionRuntime"
		});
	}
}
