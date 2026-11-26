#include "CIInventoryView.h"

#include "Engine/LocalPlayer.h"
#include "Blueprint/WidgetTree.h"
#include "CIInventoryTile.h"
#include "Components/NamedSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Model/CFObject.h"

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
/** Minimal section form for the Inventory scaffold milestone. */
class FCIInventorySection final : public FCFObject
{
public:
    explicit FCIInventorySection(FCFMagritteBinding InBinding) : Binding(MoveTemp(InBinding)) {}

    FCFViewDescription MagritteDescription() const override
    {
        return {&Binding.Describe(), &Build};
    }

private:
    static UWidget* Build(const FCFObject& Object, const magritte::Description& Container, const FCFViewContext& Context)
    {
        const auto& Section = static_cast<const FCIInventorySection&>(Object);
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

    FCFMagritteBinding Binding;
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
            Slot->SetContent(FCIInventorySection(MoveTemp(*Section)).AsView({*WidgetTree, ViewState.ToSharedRef()}));
        }
    }
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
