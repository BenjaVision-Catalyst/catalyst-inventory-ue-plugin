using UnrealBuildTool;

public class CatalystInventory : ModuleRules
{
    public CatalystInventory(ReadOnlyTargetRules Target) : base(Target)
    {
        bEnableExceptions = true;
        PrivateDependencyModuleNames.AddRange(new[] { "InputCore", "Projects", "RenderCore" });
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UMG", "CatalystFoundation", "CatalystConnectObjects", "Slate", "SlateCore" });
    }
}
