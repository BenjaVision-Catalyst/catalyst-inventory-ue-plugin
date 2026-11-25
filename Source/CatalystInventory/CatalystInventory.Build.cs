using UnrealBuildTool;

public class CatalystInventory : ModuleRules
{
    public CatalystInventory(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "UMG" });
    }
}
