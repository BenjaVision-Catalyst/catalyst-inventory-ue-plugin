#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "CIInventoryModel.h"
#include "CIInventoryShell.h"
#include "CIInventoryView.h"
#include "Components/NamedSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Presentation/CFMenu.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventoryPresentationTest,
    "Catalyst.Inventory.MagrittePresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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
