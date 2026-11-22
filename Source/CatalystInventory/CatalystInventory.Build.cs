using UnrealBuildTool;

public class CatalystInventory : ModuleRules
{
    public CatalystInventory(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.Add("Core");
    }
}
