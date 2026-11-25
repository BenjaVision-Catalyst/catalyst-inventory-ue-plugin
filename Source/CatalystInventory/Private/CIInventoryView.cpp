#include "CIInventoryView.h"

#include "Engine/LocalPlayer.h"
#include "Blueprint/WidgetTree.h"
#include "CIInventoryTile.h"

void UCIInventoryView::BindToPlayer(TSharedPtr<ICFPlayerMagritteSource> InSource,
    std::string InProfileFieldId, std::string InInventoryFieldId)
{
    DetachSource();
    Source = MoveTemp(InSource);
    ProfileFieldId = std::move(InProfileFieldId);
    InventoryFieldId = std::move(InInventoryFieldId);
    if (bConstructed)
    {
        AttachSource();
    }
}

TSharedRef<SWidget> UCIInventoryView::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        InventoryTile = WidgetTree->ConstructWidget<UCIInventoryTile>();
        WidgetTree->RootWidget = InventoryTile;
    }
    if (InventoryTile && TileTheme)
    {
        InventoryTile->Theme = TileTheme;
    }
    return Super::RebuildWidget();
}

void UCIInventoryView::NativeConstruct()
{
    Super::NativeConstruct();
    bConstructed = true;
    AttachSource();
}

void UCIInventoryView::NativeDestruct()
{
    bConstructed = false;
    DetachSource();
    Inventory.Reset();
    Super::NativeDestruct();
}

void UCIInventoryView::AttachSource()
{
    if (Source)
    {
        SourceChanged = Source->OnChanged().AddUObject(this, &UCIInventoryView::RefreshInventory);
    }
    RefreshInventory();
}

void UCIInventoryView::DetachSource()
{
    if (Source && SourceChanged.IsValid())
    {
        Source->OnChanged().Remove(SourceChanged);
        SourceChanged.Reset();
    }
}

void UCIInventoryView::RefreshInventory()
{
    const ULocalPlayer* Player = GetOwningLocalPlayer();
    Inventory.Reset();
    if (Player && Source)
    {
        const auto User = Source->SignedInUser(*Player);
        if (User)
        {
            const auto Profile = User->Follow(ProfileFieldId);
            if (Profile)
            {
                Inventory = Profile->Follow(InventoryFieldId);
            }
        }
    }
    if (InventoryTile)
    {
        const auto Item = Inventory ? Inventory->FirstRelated("items") : TOptional<FCFMagritteBinding>();
        InventoryTile->SetVisibility(Item ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (Item)
        {
            InventoryTile->Icon = nullptr;
            for (const auto& Field : Item->Describe().orderedChildren())
            {
                if (Field->id == "definitionIdentifier")
                {
                    const auto Value = Item->Read(*Field);
                    if (const auto* Definition = std::get_if<std::string>(&Value.data))
                    {
                        InventoryTile->Icon = ItemIcons.FindRef(UTF8_TO_TCHAR(Definition->c_str()));
                    }
                    break;
                }
            }
            InventoryTile->BindItem(*Item);
        }
    }
    OnInventoryBindingChanged();
}
