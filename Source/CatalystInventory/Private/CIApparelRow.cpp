#include "CIApparelRow.h"
#include "CIInventoryRowData.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

UCIApparelRowTheme::UCIApparelRowTheme()
{
    RowHeight = 76.f;
    ThumbnailWidth = 114.f;
    ThumbnailInset = FMargin(9.f, 6.f);
}

void UCIApparelRow::BindItem(const FCFMagritteBinding& Item)
{
    const FString PreviousDefinition = GetArtDefinitionIdentifier();
    Super::BindItem(Item);
    if (PreviousDefinition != GetArtDefinitionIdentifier())
    {
        Icon = nullptr;
    }

    Manufacturer = CIInventoryRowData::Manufacturer(Item);
    InvalidateLayoutAndVolatility();
}

const UCFTileTheme& UCIApparelRow::GetTileTheme() const
{
    return Theme ? Super::GetTileTheme() : *GetDefault<UCIApparelRowTheme>();
}

const UCIEquipmentRowTheme& UCIApparelRow::GetRowTheme() const
{
    const auto* Style = Cast<UCIEquipmentRowTheme>(&GetTileTheme());
    return Style ? *Style : *GetDefault<UCIApparelRowTheme>();
}

TSharedRef<SWidget> UCIApparelRow::BuildInventoryContent()
{
    const auto* DividerBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
    return SNew(SBox)
        .HeightOverride_Lambda([this] { return FOptionalSize(GetRowTheme().RowHeight); })
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride_Lambda([this] { return FOptionalSize(GetRowTheme().ThumbnailWidth); })
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
                    .Padding_Lambda([this] { return GetRowTheme().ThumbnailInset; })
                    [BuildTileIcon()]
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride_Lambda([this] { return FOptionalSize(GetRowTheme().DividerThickness); })
                [
                    SNew(SBorder).Padding(0).BorderImage(DividerBrush)
                    .BorderBackgroundColor_Lambda([this] { return GetRowTheme().DividerTint; })
                ]
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            .Padding(TAttribute<FMargin>::CreateLambda([this] { return FMargin(GetRowTheme().ContentInset, 0.f); }))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [BuildTileLabel()]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text_Lambda([this] { return Manufacturer; })
                    .Font_Lambda([this] { return GetInventoryTheme().CountFont; })
                    .ColorAndOpacity_Lambda([this] { return GetInventoryTheme().TextTint; })
                    .Visibility_Lambda([this] { return Manufacturer.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    .ToolTipText_Lambda([this] { return Manufacturer; })
                ]
            ]
        ];
}
