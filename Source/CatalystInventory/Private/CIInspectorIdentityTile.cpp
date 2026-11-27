#include "CIInspectorIdentityTile.h"

#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
class SCIInspectorConsumableIcon final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCIInspectorConsumableIcon) : _Fuel(false) {}
        SLATE_ARGUMENT(FLinearColor, Tint)
        SLATE_ARGUMENT(bool, Fuel)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Tint = Args._Tint;
        bFuel = Args._Fuel;
    }

    FVector2D ComputeDesiredSize(float) const override { return {14.0, 16.0}; }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        const auto Paint = [&](const TArray<FVector2D>& Points)
        {
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points,
                ESlateDrawEffect::NoPixelSnapping, Tint, true, 1.25f);
        };
        if (bFuel)
        {
            Paint({{3, 5}, {5, 5}, {5, 2}, {9, 2}, {9, 5}, {11, 5}, {11, 14}, {3, 14}, {3, 5}});
            Paint({{5, 8}, {9, 12}});
        }
        else
        {
            Paint({{4, 14}, {4, 6}, {7, 2}, {10, 6}, {10, 14}, {4, 14}});
            Paint({{4, 7}, {10, 7}});
            Paint({{4, 12}, {10, 12}});
        }
        return Layer;
    }

private:
    FLinearColor Tint = FLinearColor::White;
    bool bFuel = false;
};

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

    FText Manufacturer, Category, Armour, Consumable, ConsumableIcon;
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
                Armour = IdentityValue(Item->Read(*Field));
            }
            else if (Field->id == "inspectorConsumable")
            {
                Consumable = IdentityValue(Item->Read(*Field));
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
    if (!Category.IsEmpty() || !Consumable.IsEmpty())
    {
        auto TypeLine = SNew(SHorizontalBox);
        if (!Category.IsEmpty())
        {
            TypeLine->AddSlot().AutoWidth().VAlign(VAlign_Center)[Text(Category, Style.LabelFont, false)];
        }
        const FString IconId = ConsumableIcon.ToString();
        if (IconId == TEXT("cartridge") || IconId == TEXT("fuel"))
        {
            TypeLine->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(5.f, 0.f)
            [
                SNew(SCIInspectorConsumableIcon).Tint(Style.TextTint).Fuel(IconId == TEXT("fuel"))
            ];
        }
        if (!Consumable.IsEmpty())
        {
            TypeLine->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)[Text(Consumable, Style.LabelFont)];
        }
        Identity->AddSlot().AutoHeight()[TypeLine];
    }

    auto Header = SNew(SHorizontalBox);
    if (Icon)
    {
        Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
        [
            SNew(SBox).WidthOverride(Style.IconSize).HeightOverride(Style.IconSize)[BuildTileIcon()]
        ];
    }
    Header->AddSlot().FillWidth(1.f)[Identity];
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(8.f)[Header];
}
