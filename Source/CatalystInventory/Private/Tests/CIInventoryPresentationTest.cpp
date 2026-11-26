#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "CIInventoryModel.h"
#include "../CIInventorySidebarReveal.h"
#include "CIInventoryShell.h"
#include "CIInventoryView.h"
#include "Components/NamedSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Interfaces/IPluginManager.h"
#include "Input/Events.h"
#include "Misc/FileHelper.h"
#include "Presentation/CFMenu.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventoryPresentationTest,
    "Catalyst.Inventory.MagrittePresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventorySidebarRevealTest,
    "Catalyst.Inventory.SavedSetSidebarReveal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCIInventorySidebarRevealTest::RunTest(const FString&)
{
    FCIInventorySidebarReveal Reveal;
    Reveal.Update(true, 0.0);
    TestEqual(TEXT("Opening starts at the grid edge"), Reveal.GetProgress(), 0.0);
    Reveal.Update(true, .05);
    TestTrue(TEXT("The first quarter uses the original quartic easing"),
        FMath::IsNearlyEqual(Reveal.GetProgress(), .03125));
    Reveal.Update(true, .10);
    TestEqual(TEXT("The slide is halfway at 100 ms"), Reveal.GetProgress(), .5);

    auto Refreshed = Reveal;
    Refreshed.Update(true, .15);
    TestTrue(TEXT("A replacement view continues the same opening"), FMath::IsNearlyEqual(Refreshed.GetProgress(), .96875));
    Refreshed.Update(true, .20);
    TestEqual(TEXT("Opening is complete at 200 ms"), Refreshed.GetProgress(), 1.0);
    Reveal.Update(false, .10);
    TestEqual(TEXT("Reversing starts at the current position"), Reveal.GetProgress(), .5);
    Reveal.Update(false, .20);
    TestEqual(TEXT("Closing uses the same curve from the current position"), Reveal.GetProgress(), .25);
    Reveal.Update(false, .31);
    TestEqual(TEXT("Closing finishes without a snap"), Reveal.GetProgress(), 0.0);

    const FCIInventorySidebarGeometry Midpoint(600.0, 1000.0, 8.0, .5);
    TestTrue(TEXT("Sidebar width follows the original viewport cap"), FMath::IsNearlyEqual(Midpoint.Width, 200.0 * 2.0 / 3.0));
    TArray<FVector> Strips;
    Midpoint.PaintVisibleStrips([&](double Start, double End, double Alpha) {
        Strips.Emplace(Start * Midpoint.Width, End * Midpoint.Width, Alpha);
    });
    if (TestEqual(TEXT("One solid region and four feather strips"), Strips.Num(), 5))
    {
        TestTrue(TEXT("The painted edge stays at the grid boundary"),
            FMath::IsNearlyEqual(Strips.Last().Y, Midpoint.GetTravel()));
        TestTrue(TEXT("Feather spans half a grid gap at midpoint"),
            FMath::IsNearlyEqual(Strips.Last().Y - Strips[0].Y, 4.0));
        TestEqual(TEXT("The solid region fades with the slide"), Strips[0].Z, .5);
        TestTrue(TEXT("The feather fades toward the fixed edge"), Strips.Last().Z < Strips[1].Z);
    }

    const FCIInventorySidebarGeometry Open(600.0, 1000.0, 8.0, 1.0);
    TestTrue(TEXT("The settled sidebar is two tile gaps from the grid"),
        FMath::IsNearlyEqual(Open.GetTravel() - Open.Width, 16.0));
    Strips.Reset();
    Open.PaintVisibleStrips([&](double Start, double End, double Alpha) { Strips.Emplace(Start, End, Alpha); });
    TestTrue(TEXT("The settled sidebar has its full width and opacity"),
        Strips.Num() == 1 && Strips[0] == FVector(0.0, 1.0, 1.0));
    return true;
}

bool FCIInventoryPresentationTest::RunTest(const FString&)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystInventory"));
    FString Json;
    if (!TestTrue(TEXT("Load the GT-authored Inventory graph"), Plugin &&
        FFileHelper::LoadFileToString(Json, *(Plugin->GetBaseDir() / TEXT("Tests/Fixtures/InventoryPresentation.json")))))
    {
        return false;
    }

    auto Graph = std::make_shared<magritte::Graph>(magritte::Graph::decode(TCHAR_TO_UTF8(*Json)));
    const FCFViewContext Context{*GetTransientPackage(), MakeShared<FCFViewState>()};
    const auto Open = [&]() {
        return Cast<UCIInventoryView>(FCIInventoryModel(
            FCFMagritteBinding::Find(Graph, "inventoryForProfile").GetValue()).AsView(Context));
    };

    TStrongObjectPtr<UCIInventoryView> View(Open());
    if (!TestNotNull(TEXT("A model answers its Inventory view"), View.Get()))
    {
        return false;
    }
    const auto Slate = View->TakeWidget();
    auto* Shell = CastChecked<UCIInventoryShell>(View->WidgetTree->RootWidget);
    auto* Menu = Shell->GetMenu();
    TestEqual(TEXT("The container yields four menu items"), Menu->GetChildrenCount(), 4);
    TestEqual(TEXT("The initial Gear tab follows the first description"), Menu->GetSelectedIndex(), 1);
    const auto ShellSlate = Shell->TakeWidget();
    const auto Press = [&](const FKey& Key) {
        return ShellSlate->OnKeyDown(ShellSlate->GetCachedGeometry(),
            FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled();
    };
    TestTrue(TEXT("Q is routed to the previous inventory tab"), Press(EKeys::Q));
    TestEqual(TEXT("Q opens Apparel"), Menu->GetSelectedIndex(), 0);
    TestTrue(TEXT("E is routed to the next inventory tab"), Press(EKeys::E));
    TestEqual(TEXT("E returns to Gear"), Menu->GetSelectedIndex(), 1);
    bool bClosed = false;
    Shell->SetCloseAction(FSimpleDelegate::CreateLambda([&] { bClosed = true; }));
    TestTrue(TEXT("Tab activates the bound close action"), Press(EKeys::Tab) && bClosed);
    Shell->SetCloseAction({});
    TestTrue(TEXT("Select Resources"), Menu->SelectItem(2));

    TStrongObjectPtr<UCIInventoryView> Reopened(Open());
    if (!TestNotNull(TEXT("The model can be rendered again"), Reopened.Get()))
    {
        return false;
    }
    const auto ReopenedSlate = Reopened->TakeWidget();
    auto* ReopenedShell = CastChecked<UCIInventoryShell>(Reopened->WidgetTree->RootWidget);
    TestEqual(TEXT("The last selected description is restored"), ReopenedShell->GetMenu()->GetSelectedIndex(), 2);
    auto* ActiveSlot = Cast<UNamedSlot>(ReopenedShell->GetMainPanel()->GetActiveWidget());
    auto* Content = ActiveSlot ? Cast<UTextBlock>(ActiveSlot->GetContent()) : nullptr;
    if (TestNotNull(TEXT("The selected relation creates its own view"), Content))
    {
        TestEqual(TEXT("The panel displays the related object's value"), Content->GetText().ToString(),
            FString(TEXT("Owned resources")));
    }
    return true;
}
#endif
