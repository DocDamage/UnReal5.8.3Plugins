// Development host editor target. Hosts DocModular plugins for build/test only.
using UnrealBuildTool;

public class DocModularDevEditorTarget : TargetRules
{
	public DocModularDevEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("DocModularDev");
	}
}
