using UnrealBuildTool;

public class NonCharacterInteractionEditorTarget : TargetRules
{
	public NonCharacterInteractionEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("NonCharacterInteraction");
	}
}
