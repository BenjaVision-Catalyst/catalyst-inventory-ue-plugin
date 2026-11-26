#include "CIInventorySessionView.h"

#include "Blueprint/WidgetTree.h"
#include "CIInventoryModel.h"
#include "CIInventoryShell.h"
#include "CIInventoryView.h"
#include "Components/SizeBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"

void UCIInventorySessionView::BindToPlayer(TSharedPtr<ICFPlayerMagritteSource> InSource,
    TSubclassOf<UCIInventoryView> InViewClass, TFunction<void(bool)> InCreateSavedSet,
    TFunction<void(FString)> InEquipSavedSet, TFunction<void(FString)> InOverwriteSavedSet)
{
    Unsubscribe();
    Source = MoveTemp(InSource);
    ViewClass = InViewClass;
    CreateSavedSet = MoveTemp(InCreateSavedSet);
    EquipSavedSet = MoveTemp(InEquipSavedSet);
    OverwriteSavedSet = MoveTemp(InOverwriteSavedSet);
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
    const auto* Player = GetOwningLocalPlayer();
    const auto User = Player && Source ? Source->SignedInUser(*Player) : TOptional<FCFMagritteBinding>();
    const auto Profile = User ? User->Follow("profile") : TOptional<FCFMagritteBinding>();
    auto Inventory = Profile ? Profile->Follow("inventory") : TOptional<FCFMagritteBinding>();
    if (!Inventory)
    {
        Panel->ClearChildren();
        ViewState.Reset();
        return;
    }
    const auto* Previous = ViewState ? GetInventoryView() : nullptr;
    if (!ViewState)
    {
        ViewState = MakeShared<FCFViewState>();
    }
    const auto* PreviousShell = Previous && Previous->GetInventoryBinding()
        && Previous->GetInventoryBinding()->ObjectId() == Inventory->ObjectId() ? Previous->GetInventoryShell() : nullptr;
    const auto PreviousWidget = PreviousShell ? PreviousShell->GetCachedWidget() : TSharedPtr<SWidget>();
    const bool bRestoreFocus = PreviousWidget && PreviousWidget->HasKeyboardFocus();
    FCIInventoryModel Model(MoveTemp(*Inventory), ViewClass);
    auto* Owner = GetOwningPlayer();
    auto* Content = Cast<UCIInventoryView>(Model.AsView({Owner ? static_cast<UObject&>(*Owner) : static_cast<UObject&>(*this),
        ViewState.ToSharedRef()}));
    if (auto* Shell = Content ? Content->GetInventoryShell() : nullptr)
    {
        if (CreateSavedSet)
        {
            Shell->SetCreateSavedSetAction([WeakThis = TWeakObjectPtr<UCIInventorySessionView>(this)](bool bOutfit) {
                if (auto* Self = WeakThis.Get(); Self && Self->CreateSavedSet)
                {
                    Self->CreateSavedSet(bOutfit);
                }
            });
        }
        if (EquipSavedSet)
        {
            Shell->SetEquipSavedSetAction([WeakThis = TWeakObjectPtr<UCIInventorySessionView>(this)](FString Identifier) {
                if (auto* Self = WeakThis.Get(); Self && Self->EquipSavedSet)
                {
                    Self->EquipSavedSet(MoveTemp(Identifier));
                }
            });
        }
        if (OverwriteSavedSet)
        {
            Shell->SetOverwriteSavedSetAction([WeakThis = TWeakObjectPtr<UCIInventorySessionView>(this)](FString Identifier) {
                if (auto* Self = WeakThis.Get(); Self && Self->OverwriteSavedSet)
                {
                    Self->OverwriteSavedSet(MoveTemp(Identifier));
                }
            });
        }
        if (PreviousShell)
        {
            Shell->RestoreSavedSetsState(*PreviousShell);
        }
    }
    Panel->SetContent(Content);
    if (bRestoreFocus && Content && FSlateApplication::IsInitialized())
    {
        if (auto* Shell = Content->GetInventoryShell())
        {
            FSlateApplication::Get().SetKeyboardFocus(Shell->TakeWidget(), EFocusCause::SetDirectly);
        }
    }
}
