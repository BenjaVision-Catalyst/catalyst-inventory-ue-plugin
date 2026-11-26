#include "CIInventoryModel.h"

#include "Blueprint/WidgetTree.h"
#include "CIInventoryShell.h"
#include "CIInventoryView.h"
#include "Components/NamedSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Presentation/CFMagritteMenuBuilder.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

class FCIInventoryBuilder
{
public:
    static UWidget* Build(const FCFObject& Object, const magritte::Description& Container, const FCFViewContext& Context)
    {
        const auto& Inventory = static_cast<const FCIInventoryModel&>(Object);
        if (!Container.isContainer())
        {
            return nullptr;
        }

        UClass* ViewClass = Inventory.ViewClass.Get();
        auto* View = NewObject<UCIInventoryView>(&Context.Owner, ViewClass ? ViewClass : UCIInventoryView::StaticClass());
        if (const auto* Player = Cast<APlayerController>(&Context.Owner); Player && Player->GetLocalPlayer())
        {
            View->SetPlayerContext(FLocalPlayerContext(Player->GetLocalPlayer()));
        }
        View->Initialize();
        View->Inventory = Inventory.Binding;
        View->ViewState = Context.State;
        auto& Tree = *View->WidgetTree;
        auto* Shell = Tree.ConstructWidget<UCIInventoryShell>();
        Shell->SetLayoutTheme(View->TileTheme);

        const auto& Selection = Context.State->SelectionByObject;
        const auto SavedSelection = Selection.find(Inventory.Binding.ObjectId());
        const std::string SelectedId = SavedSelection != Selection.end()
            ? SavedSelection->second : TCHAR_TO_UTF8(*View->DefaultSectionId);
        auto* Menu = FCFHorizontalMenuBuilder::Build(Tree, Container, View->MenuTheme,
            SelectedId, View->SectionDescriptions);

        View->MainPanel = Tree.ConstructWidget<UWidgetSwitcher>();
        for (int32 Index = 0; Index < View->SectionDescriptions.Num(); ++Index)
        {
            View->MainPanel->AddChild(Tree.ConstructWidget<UNamedSlot>());
        }
        Shell->AddChild(Menu);
        Shell->AddChild(View->MainPanel);
        Tree.RootWidget = Shell;

        Menu->OnSelectionChanged.AddUniqueDynamic(View, &UCIInventoryView::PresentSection);
        View->PresentSection(Menu->InitialSelectedIndex, nullptr);
        return View;
    }
};

FCIInventoryModel::FCIInventoryModel(FCFMagritteBinding InBinding, TSubclassOf<UCIInventoryView> InViewClass)
    : FCFIdentifiableObject(UTF8_TO_TCHAR(InBinding.ObjectId().c_str())),
      Binding(MoveTemp(InBinding)), ViewClass(InViewClass)
{
}

FCFViewDescription FCIInventoryModel::MagritteDescription() const
{
    return {&Binding.Describe(), &FCIInventoryBuilder::Build};
}
