#include "CIInventoryView.h"

#include "Engine/LocalPlayer.h"
#include "Blueprint/WidgetTree.h"
#include "CIInventoryShell.h"
#include "CIInventoryTile.h"
#include "Presentation/CFMenu.h"

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
        auto* Shell = WidgetTree->ConstructWidget<UCIInventoryShell>();
        auto* Menu = WidgetTree->ConstructWidget<UCFMenu>();
        Menu->Theme = MenuTheme;
        Menu->InitialSelectedIndex = 1;
        const TPair<FName, FText> Tabs[]{
            {TEXT("Apparel"), NSLOCTEXT("CatalystInventory", "ApparelTab", "Apparel")},
            {TEXT("Gear"), NSLOCTEXT("CatalystInventory", "GearTab", "Gear")},
            {TEXT("Resources"), NSLOCTEXT("CatalystInventory", "ResourcesTab", "Resources")},
            {TEXT("Stats"), NSLOCTEXT("CatalystInventory", "StatsTab", "Stats")}
        };
        for (const auto& Tab : Tabs)
        {
            auto* Item = WidgetTree->ConstructWidget<UCFMenuItem>();
            Item->SetLabel(Tab.Value);
            if (const auto* Icon = MenuIcons.Find(Tab.Key))
            {
                Item->SetIcon(*Icon);
            }
            Menu->AddChild(Item);
        }
        Shell->SetContent(Menu);
        WidgetTree->RootWidget = Shell;
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
            InventoryTile->BindItem(*Item);
            const FString& ArtId = InventoryTile->GetArtDefinitionIdentifier();
            InventoryTile->Icon = ArtId.IsEmpty() ? nullptr : ItemIcons.FindRef(ArtId).Get();
        }
    }
    OnInventoryBindingChanged();
}
