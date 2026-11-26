#include "CIInventorySessionView.h"

#include "Blueprint/WidgetTree.h"
#include "CIInventoryModel.h"
#include "CIInventoryView.h"
#include "Components/SizeBox.h"

void UCIInventorySessionView::BindToPlayer(TSharedPtr<ICFPlayerMagritteSource> InSource,
    TSubclassOf<UCIInventoryView> InViewClass)
{
    Unsubscribe();
    Source = MoveTemp(InSource);
    ViewClass = InViewClass;
    ViewState.Reset();
    if (Panel && Source)
    {
        Changed = Source->OnChanged().AddUObject(this, &UCIInventorySessionView::Refresh);
    }
    Refresh();
}

UCIInventoryView* UCIInventorySessionView::GetInventoryView() const
{
    return Panel ? Cast<UCIInventoryView>(Panel->GetContent()) : nullptr;
}

TSharedRef<SWidget> UCIInventorySessionView::RebuildWidget()
{
    if (!Panel)
    {
        Panel = WidgetTree->ConstructWidget<USizeBox>();
        WidgetTree->RootWidget = Panel;
    }
    return Super::RebuildWidget();
}

void UCIInventorySessionView::NativeConstruct()
{
    Super::NativeConstruct();
    if (Source && !Changed.IsValid())
    {
        Changed = Source->OnChanged().AddUObject(this, &UCIInventorySessionView::Refresh);
    }
    Refresh();
}

void UCIInventorySessionView::NativeDestruct()
{
    Unsubscribe();
    Panel->ClearChildren();
    ViewState.Reset();
    Super::NativeDestruct();
}

void UCIInventorySessionView::Unsubscribe()
{
    if (Source && Changed.IsValid())
    {
        Source->OnChanged().Remove(Changed);
        Changed.Reset();
    }
}

void UCIInventorySessionView::Refresh()
{
    if (!Panel)
    {
        return;
    }
    Panel->ClearChildren();
    const auto* Player = GetOwningLocalPlayer();
    const auto User = Player && Source ? Source->SignedInUser(*Player) : TOptional<FCFMagritteBinding>();
    const auto Profile = User ? User->Follow("profile") : TOptional<FCFMagritteBinding>();
    auto Inventory = Profile ? Profile->Follow("inventory") : TOptional<FCFMagritteBinding>();
    if (!Inventory)
    {
        ViewState.Reset();
        return;
    }
    if (!ViewState)
    {
        ViewState = MakeShared<FCFViewState>();
    }
    FCIInventoryModel Model(MoveTemp(*Inventory), ViewClass);
    auto* Owner = GetOwningPlayer();
    Panel->SetContent(Model.AsView({Owner ? static_cast<UObject&>(*Owner) : static_cast<UObject&>(*this),
        ViewState.ToSharedRef()}));
}
