// DocModularCoreRuntime
// Allowed dependencies (handoff 2.2): Core, CoreUObject, Engine, GameplayTags.
// This module must never depend on any DocModular feature plugin.
using UnrealBuildTool;

public class DocModularCoreRuntime : ModuleRules
{
	public DocModularCoreRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public headers expose FGameplayTag / FGameplayTagContainer, UWorld/AActor
		// weak references, and UINTERFACEs, so these are public dependencies.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags"
		});
	}
}
