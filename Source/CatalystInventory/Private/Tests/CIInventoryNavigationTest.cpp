#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../CIInventoryNavigation.h"
#include "CIInventoryShell.h"
#include "CIInventoryTile.h"
#include "Components/WidgetSwitcher.h"
#include "Input/Events.h"
#include "Misc/App.h"
#include "Presentation/CFMenu.h"
#include "Presentation/CFMagritteBinding.h"
#include "Presentation/CFSpanGrid.h"
#include "UObject/StrongObjectPtr.h"

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

    TStrongObjectPtr<UCIInventoryShell> Shell(NewObject<UCIInventoryShell>());
    Shell->SetActionSection(TEXT("gear"));
    Shell->AddChild(NewObject<UCFMenu>(Shell.Get()));
    auto* Panel = NewObject<UWidgetSwitcher>(Shell.Get());
    Shell->AddChild(Panel);
    auto* UnpaintedGrid = NewObject<UCFSpanGrid>(Panel);
    UnpaintedGrid->Configure(2, 30.f, 0.f, 0.f, 0.f, 0.f, FMargin(), FSlateFontInfo(), FLinearColor::White);
    const auto Card = std::make_shared<magritte::Description>();
    Card->id = "equipmentCard";
    Card->className = "MAPriorityContainer";
    for (const auto* Id : {"displayName", "categoryIdentifier"})
    {
        auto Field = std::make_shared<magritte::Description>();
        Field->id = Id;
        Field->className = "MAStringDescription";
        Card->children.push_back(Field);
    }
    const auto Graph = std::make_shared<magritte::Graph>();
    const auto AddTile = [&](const char* Category, const char* ItemName) {
        auto& Object = Graph->objects[Category];
        Object.id = Category;
        Object.description = Card;
        Object.values = {{"categoryIdentifier", magritte::Value(Category)},
            {"displayName", magritte::Value(ItemName)}};
        auto* Tile = NewObject<UCIInventoryTile>(UnpaintedGrid);
        Tile->BindItem(FCFMagritteBinding::Find(Graph, Object.id).GetValue());
        TestTrue(TEXT("An equipment tile still hides the label on its front"), Tile->Label.IsEmpty());
        UnpaintedGrid->AddItem(*Tile, 1);
    };
    AddTile("MeleeWeapon", "Training sword");
    AddTile("LeftHandWeapon", "");
    Panel->AddChild(UnpaintedGrid);
    const auto Widget = Shell->TakeWidget();
    TestFalse(TEXT("The grid is still unpainted"), UnpaintedGrid->GetCachedWidget().IsValid());
    Widget->OnKeyDown(Widget->GetCachedGeometry(), FKeyEvent(EKeys::Right, FModifierKeysState(), 0, false, 0, 0));
    TestEqual(TEXT("The first navigation input finds tiles before Slate has built them"),
        Shell->GetFocusedGridCellForTest(), FIntPoint(1, 0));
    TestEqual(TEXT("The inspector uses the slot name when the model has no item name"),
        Shell->GetInspectorLabelForTest().ToString(), FString(TEXT("Left Hand Weapon")));
    Widget->OnKeyDown(Widget->GetCachedGeometry(), FKeyEvent(EKeys::Left, FModifierKeysState(), 0, false, 0, 0));
    TestEqual(TEXT("The inspector follows focus and displays the model's item name"),
        Shell->GetInspectorLabelForTest().ToString(), FString(TEXT("Training sword")));

    if (FApp::CanEverRender())
    {
        const auto Geometry = FGeometry::MakeRoot({1280.0, 720.0}, FSlateLayoutTransform());
        Widget->Tick(Geometry, 0.0, .2f);
        const double SelectedLift = Shell->GetGridTileLiftForTest({0, 0});
        TestTrue(TEXT("Selection lifts the entire tile off the grid"), SelectedLift > 0.0);
        Widget->OnKeyDown(Geometry, FKeyEvent(EKeys::Right, FModifierKeysState(), 0, false, 0, 0));
        Widget->Tick(Geometry, .07, .07f);
        const double LeavingLift = Shell->GetGridTileLiftForTest({0, 0});
        const double EnteringLift = Shell->GetGridTileLiftForTest({1, 0});
        TestTrue(TEXT("The deselected tile lowers while the new tile rises"),
            LeavingLift > 0.0 && LeavingLift < SelectedLift && EnteringLift > 0.0 && EnteringLift < SelectedLift);
        Widget->Tick(Geometry, .27, .2f);
        TestEqual(TEXT("The deselected tile settles into the grid"), Shell->GetGridTileLiftForTest({0, 0}), 0.0);
        TestEqual(TEXT("The new tile reaches the same selection lift"), Shell->GetGridTileLiftForTest({1, 0}), SelectedLift);
    }

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
