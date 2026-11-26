#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../CIInventoryNavigation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventoryNavigationTest, "Catalyst.Inventory.Navigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCIInventoryNavigationTest::RunTest(const FString&)
{
    const TArray<FCIInventoryNavigationRow> Grid{
        {6, {{2}, {2}, {2}}},
        {6, {{3}, {3}}},
        {6, {{2}}},
    };
    TestEqual(TEXT("Right advances a cell"), StepInventoryGrid(Grid, {0, 0}, {1, 0}), FIntPoint(1, 0));
    TestEqual(TEXT("Horizontal navigation clamps at the row edge"),
        StepInventoryGrid(Grid, {2, 0}, {1, 0}), FIntPoint(2, 0));
    TestEqual(TEXT("Down maps the middle of a three-column row to the right of a two-column row"),
        StepInventoryGrid(Grid, {1, 0}, {0, 1}), FIntPoint(1, 1));
    TestEqual(TEXT("Up maps the right gear column to the right weapon"),
        StepInventoryGrid(Grid, {1, 1}, {0, -1}), FIntPoint(2, 0));
    TestEqual(TEXT("A partial row falls back to its last actual tile"),
        StepInventoryGrid(Grid, {1, 1}, {0, 1}), FIntPoint(0, 2));
    TestEqual(TEXT("The first column of a partial row keeps its position when stepping up"),
        StepInventoryGrid(Grid, {0, 2}, {0, -1}), FIntPoint(0, 1));

    FCIInventoryNavigationInput Input;
    Input.SetStickAxis(true, .9);
    TestEqual(TEXT("An already held stick must center first"), Input.Step(1.0), FIntPoint(0, 0));
    Input.SetStickAxis(true, .25);
    TestEqual(TEXT("Stick drift re-arms navigation without moving"), Input.Step(1.1), FIntPoint(0, 0));
    Input.SetStickAxis(true, .65);
    Input.SetStickAxis(false, -.9);
    TestEqual(TEXT("The dominant stick axis moves one row"), Input.Step(1.2), FIntPoint(0, 1));
    TestEqual(TEXT("A held direction waits for the original first repeat delay"), Input.Step(1.53), FIntPoint(0, 0));
    TestEqual(TEXT("A held direction repeats after 340 ms"), Input.Step(1.55), FIntPoint(0, 1));
    TestEqual(TEXT("The next repeat waits another 140 ms"), Input.Step(1.68), FIntPoint(0, 0));
    TestEqual(TEXT("The shorter repeat interval continues while held"), Input.Step(1.70), FIntPoint(0, 1));

    Input.SetPad({1, 0}, true);
    TestEqual(TEXT("D-pad input overrides a held stick"), Input.Step(1.71), FIntPoint(1, 0));
    Input.SetPad({1, 0}, false);
    TestEqual(TEXT("Releasing the D-pad returns to the stick"), Input.Step(1.72), FIntPoint(0, 1));

    Input.TriggerAxis = .9f;
    TestTrue(TEXT("The trigger axis opens the sidebar"), Input.UpdateTrigger());
    Input.bTriggerButton = true;
    TestFalse(TEXT("The same press does not toggle twice when the digital event arrives"), Input.UpdateTrigger());
    Input.bTriggerButton = false;
    Input.TriggerAxis = .45f;
    TestFalse(TEXT("The trigger stays held below the activation threshold"), Input.UpdateTrigger());
    Input.TriggerAxis = .25f;
    TestFalse(TEXT("Releasing the trigger does not toggle"), Input.UpdateTrigger());
    Input.TriggerAxis = .9f;
    TestTrue(TEXT("The next press toggles again"), Input.UpdateTrigger());
    return true;
}
#endif
