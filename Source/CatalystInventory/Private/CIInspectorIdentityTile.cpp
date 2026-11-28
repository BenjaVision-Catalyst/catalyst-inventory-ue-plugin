#include "CIInspectorIdentityTile.h"
#include "CIInventoryRowData.h"
#include "CIResourceTile.h"

#include "SCIInspectorConsumableIcon.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FText IdentityValue(const magritte::Value& Value)
{
    if (const auto* Text = std::get_if<std::string>(&Value.data))
    {
        return FText::FromString(UTF8_TO_TCHAR(Text->c_str()));
    }
    if (const auto* Number = std::get_if<std::int64_t>(&Value.data))
    {
        return FText::AsNumber(*Number);
    }
    if (const auto* Number = std::get_if<double>(&Value.data))
    {
        return FText::AsNumber(*Number);
    }
    return FText::GetEmpty();
}
}

void UCIInspectorIdentityTile::InitializeFrom(const UCIInventoryTile& Source)
{
    if (const auto& Item = Source.GetInspectorItem())
    {
        BindItem(*Item);
    }
    TierId = Source.TierId;
    bShowTier = Source.bShowTier;
    Label = Source.GetItemLabel();
    Icon = Source.GetInspectorIcon();
    FallbackIconIdentifier = Source.FallbackIconIdentifier;
    bShowCount = Source.IsA<UCIResourceTile>() && Source.bShowCount;
    Count = Source.Count;
}

TSharedRef<SWidget> UCIInspectorIdentityTile::BuildInventoryContent()
{
    const auto& Style = GetInventoryTheme();
    const auto Text = [&Style](const FText& Value, const FSlateFontInfo& Font, bool bWrap = true)
    {
        return SNew(STextBlock).Text(Value).Font(Font).ColorAndOpacity(FSlateColor(Style.TextTint)).AutoWrapText(bWrap);
    };
    auto Identity = SNew(SVerticalBox);
    Identity->AddSlot().AutoHeight()[Text(Label, Style.InspectorTitleFont)];

    FText Manufacturer, Category, Armour, ConsumableIcon;
    if (const auto& Item = GetInspectorItem())
    {
        for (const auto& Field : Item->Describe().orderedChildren())
        {
            if (Field->id == "inspectorManufacturer")
            {
                Manufacturer = IdentityValue(Item->Read(*Field));
            }
            else if (Field->id == "inspectorCategory")
            {
                Category = IdentityValue(Item->Read(*Field));
            }
            else if (Field->id == "inspectorArmour")
            {
                Armour = CIInventoryRowData::FormatArmour(Item->Read(*Field));
            }
            else if (Field->id == "inspectorConsumableIcon")
            {
                ConsumableIcon = IdentityValue(Item->Read(*Field));
            }
        }
    }
    if (!Manufacturer.IsEmpty())
    {
        Identity->AddSlot().AutoHeight()
        [
            SNew(STextBlock).Text(Manufacturer).Font(Style.GroupLabelFont)
            .ColorAndOpacity(FSlateColor(Style.GroupLabelTint)).AutoWrapText(true)
        ];
    }
    if (!Armour.IsEmpty())
    {
        const FText ArmourLine = FText::Format(NSLOCTEXT("Inventory", "InspectorArmour", "{0} Armour"), Armour);
        Identity->AddSlot().AutoHeight()[Text(ArmourLine, Style.GroupLabelFont)];
    }
    if (bShowCount)
    {
        Identity->AddSlot().AutoHeight()
        [Text(FText::Format(NSLOCTEXT("Inventory", "ResourceQuantity", "Quantity {0}"), FText::AsNumber(Count)),
            Style.GroupLabelFont)];
    }
    const FString IconId = ConsumableIcon.ToString();
    const bool bFuel = IconId == TEXT("fuel");
    const bool bHasConsumableIcon = bFuel || IconId == TEXT("cartridge");
    if (!Category.IsEmpty() || bHasConsumableIcon)
    {
        auto TypeLine = SNew(SHorizontalBox);
        if (!Category.IsEmpty())
        {
            TypeLine->AddSlot().AutoWidth().VAlign(VAlign_Center)[Text(Category, Style.LabelFont, false)];
        }
        if (bHasConsumableIcon)
        {
            TypeLine->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(5.f, 0.f)
            [
                SNew(SCIInspectorConsumableIcon).Tint(Style.TextTint).Fuel(bFuel)
            ];
        }
        Identity->AddSlot().AutoHeight()[TypeLine];
    }

    auto Header = SNew(SHorizontalBox);
    if (Icon || !FallbackIconIdentifier.IsNone())
    {
        Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
        [
            SNew(SBox).WidthOverride(Style.IconSize).HeightOverride(Style.IconSize)[BuildTileIcon()]
        ];
    }
    Header->AddSlot().FillWidth(1.f)[Identity];
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(8.f)[Header];
}
