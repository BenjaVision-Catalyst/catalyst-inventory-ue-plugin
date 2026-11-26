#include "CIInventoryView.h"

#include "Engine/LocalPlayer.h"
#include "Blueprint/WidgetTree.h"
#include "CIEquipmentTile.h"
#include "CIInventoryTile.h"
#include "CIResourceTile.h"
#include "Components/NamedSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Model/CFObject.h"
#include "Presentation/CFSpanGrid.h"

void UCIInventoryView::BindToPlayer(TSharedPtr<ICFPlayerMagritteSource> InSource,
    std::string InProfileFieldId, std::string InInventoryFieldId)
{
    if (MainPanel)
    {
        return;
    }
    DetachSource();
    Source = MoveTemp(InSource);
    ProfileFieldId = std::move(InProfileFieldId);
    InventoryFieldId = std::move(InInventoryFieldId);
    if (bConstructed)
    {
        AttachSource();
    }
}

namespace
{
class FCIInventorySection final : public FCFObject
{
public:
    FCIInventorySection(FCFMagritteBinding InBinding, UCIInventoryTileTheme* InTheme,
        const TMap<FString, TObjectPtr<UTexture>>& InIcons, bool bInHalfSize)
        : Binding(MoveTemp(InBinding)), Theme(InTheme), Icons(InIcons), bHalfSize(bInHalfSize) {}

    FCFViewDescription MagritteDescription() const override
    {
        return {&Binding.Describe(), &Build};
    }

private:
    static UWidget* Build(const FCFObject& Object, const magritte::Description& Container, const FCFViewContext& Context)
    {
        const auto& Section = static_cast<const FCIInventorySection&>(Object);
        const auto Layout = Container.property("metadata:ciGrid");
        if (const auto* Kind = std::get_if<magritte::Value::Symbol>(&Layout.data))
        {
            return Section.BuildGrid(Context, Kind->utf8);
        }
        for (const auto& Field : Container.orderedChildren())
        {
            const auto Value = Section.Binding.Read(*Field);
            if (const auto* Text = std::get_if<std::string>(&Value.data))
            {
                auto* Widget = NewObject<UTextBlock>(&Context.Owner);
                Widget->SetText(FText::FromString(UTF8_TO_TCHAR(Text->c_str())));
                return Widget;
            }
        }
        return nullptr;
    }

    UWidget* BuildGrid(const FCFViewContext& Context, const std::string& Kind) const
    {
        const bool bResources = Kind == "resources";
        const bool bApparel = Kind == "apparel";
        const int32 Columns = (bResources ? 4 : 6) * (bHalfSize ? 2 : 1);
        const int32 Span = bResources ? 1 : (bApparel ? 2 : 3);
        const UCIInventoryTileTheme* GridTheme = Theme ? Theme : GetDefault<UCIInventoryTileTheme>();
        auto* Grid = NewObject<UCFSpanGrid>(&Context.Owner);
        const float TileHeight = bResources ? GridTheme->ResourceTileHeight : GridTheme->EquipmentTileHeight;
        Grid->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        Grid->Configure(Columns, TileHeight * (bHalfSize ? .5f : 1.f), GridTheme->TileGap,
            GridTheme->RowGap, GridTheme->GroupGap,
            GridTheme->GroupLabelHeight, GridTheme->GroupLabelPadding, GridTheme->GroupLabelFont,
            GridTheme->GroupLabelTint);

        struct FGroup
        {
            std::string Label;
            TArray<const magritte::Description*> Fields;
        };
        TArray<FGroup> Groups;
        for (const auto& Field : Binding.Describe().orderedChildren())
        {
            if (!Field->isRelation() || !Field->isMultiple() || !Field->flag("visible"))
            {
                continue;
            }
            const auto Group = Field->property("group");
            const auto* Label = std::get_if<std::string>(&Group.data);
            const std::string Name = Label ? *Label : std::string();
            auto* Existing = Groups.FindByPredicate([&Name](const FGroup& Entry) { return Entry.Label == Name; });
            if (!Existing)
            {
                Existing = &Groups.Emplace_GetRef(FGroup{Name, {}});
            }
            Existing->Fields.Add(Field.get());
        }

        for (const FGroup& Group : Groups)
        {
            bool bStartedGroup = false;
            for (const auto* Field : Group.Fields)
            {
                for (const auto& Item : Binding.Related(Field->id, 32))
                {
                    if (!bStartedGroup)
                    {
                        Grid->AddHeading(FText::FromString(UTF8_TO_TCHAR(Group.Label.c_str())));
                        bStartedGroup = true;
                    }
                    UCIInventoryTile* Tile = bResources
                        ? static_cast<UCIInventoryTile*>(NewObject<UCIResourceTile>(Grid))
                        : (bApparel ? static_cast<UCIInventoryTile*>(NewObject<UCIInventoryTile>(Grid))
                                    : static_cast<UCIInventoryTile*>(NewObject<UCIEquipmentTile>(Grid)));
                    Tile->Theme = Theme;
                    Tile->BindItem(Item);
                    Tile->Icon = Icons.FindRef(Tile->GetArtDefinitionIdentifier()).Get();
                    const auto* Equipment = Cast<UCIEquipmentTile>(Tile);
                    const bool bToolsWeapons = Equipment && (Equipment->PresentationKind == TEXT("weapon")
                        || Equipment->PresentationKind == TEXT("tool"));
                    Grid->AddItem(*Tile, bToolsWeapons ? 2 : Span);
                }
            }
        }
        return Grid;
    }

    FCFMagritteBinding Binding;
    UCIInventoryTileTheme* Theme;
    const TMap<FString, TObjectPtr<UTexture>>& Icons;
    bool bHalfSize;
};
}

void UCIInventoryView::PresentSection(int32 Index, UCFMenuItem*)
{
    if (!Inventory || !ViewState || !MainPanel || !SectionDescriptions.IsValidIndex(Index))
    {
        return;
    }

    const auto& Description = *SectionDescriptions[Index];
    ViewState->SelectionByObject.insert_or_assign(Inventory->ObjectId(), Description.id);
    auto* Slot = CastChecked<UNamedSlot>(MainPanel->GetChildAt(Index));
    if (!Slot->GetContent())
    {
        if (auto Section = Inventory->Follow(Description))
        {
            Slot->SetContent(FCIInventorySection(MoveTemp(*Section), TileTheme, ItemIcons, bHalfSize).AsView({*WidgetTree, ViewState.ToSharedRef()}));
        }
    }
}

void UCIInventoryView::SetHalfSize(bool bEnabled)
{
    if (bHalfSize == bEnabled || !MainPanel)
    {
        return;
    }
    bHalfSize = bEnabled;
    for (int32 Index = 0; Index < MainPanel->GetNumWidgets(); ++Index)
    {
        CastChecked<UNamedSlot>(MainPanel->GetChildAt(Index))->SetContent(nullptr);
    }
    PresentSection(MainPanel->GetActiveWidgetIndex(), nullptr);
}

void UCIInventoryView::NativeConstruct()
{
    Super::NativeConstruct();
    bConstructed = true;
    if (InventoryTile && TileTheme)
    {
        InventoryTile->Theme = TileTheme;
    }
    if (!MainPanel)
    {
        AttachSource();
    }
}

void UCIInventoryView::NativeDestruct()
{
    bConstructed = false;
    DetachSource();
    if (Source)
    {
        Inventory.Reset();
    }
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
