// Development host game target. Hosts DocModular plugins for build/test only.
using UnrealBuildTool;

public class DocCppConsumerTarget : TargetRules
{
	public DocCppConsumerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("DocCppConsumer");
	}
}
