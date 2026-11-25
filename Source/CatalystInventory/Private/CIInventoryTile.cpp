#include "CIInventoryTile.h"

#include "Engine/Texture2D.h"
#include "Presentation/CFMagritteBinding.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

UCIInventoryTile::UCIInventoryTile()
{
    Style.Background = FLinearColor(0.f, 0.f, 0.f, .05f);
    Style.FocusedBackground = FLinearColor::FromSRGBColor(FColor(74, 20, 140, 255));
    Style.FocusedBackground.A = .58f;
    Style.Border = FLinearColor(1.f, 1.f, 1.f, .15f);
    TierColors = {
        {1, FLinearColor(0.50f, 0.50f, 0.50f)},
        {2, FLinearColor(0.12f, 0.68f, 0.22f)},
        {3, FLinearColor(0.10f, 0.38f, 0.90f)},
        {4, FLinearColor(0.85f, 0.58f, 0.08f)},
        {5, FLinearColor(0.02f, 0.24f, 0.10f)},
        {6, FLinearColor(0.49f, 0.16f, 0.76f)}
    };
    IconBrush.DrawAs = ESlateBrushDrawType::Image;
}

void UCIInventoryTile::BindItem(const FCFMagritteBinding& Item)
{
    TierId = 1;
    Label = FText::GetEmpty();
    bShowCount = false;
    Count = 0;
    for (const auto& Field : Item.Describe().orderedChildren())
    {
        const auto Value = Item.Read(*Field);
        if (Field->id == "tierId")
        {
            if (const auto* Tier = std::get_if<std::int64_t>(&Value.data); Tier && *Tier >= 1 && *Tier <= 6)
            {
                TierId = static_cast<int32>(*Tier);
            }
        }
        else if (Field->id == "displayName")
        {
            if (const auto* Name = std::get_if<std::string>(&Value.data))
            {
                Label = FText::FromString(UTF8_TO_TCHAR(Name->c_str()));
            }
        }
        else if (Field->id == "count")
        {
            if (const auto* Quantity = std::get_if<std::int64_t>(&Value.data))
            {
                Count = *Quantity;
                bShowCount = true;
            }
        }
    }
    InvalidateLayoutAndVolatility();
}

TSharedRef<SWidget> UCIInventoryTile::BuildTileContent()
{
    return SNew(SBox).MinDesiredWidth(72.f).MinDesiredHeight(72.f)
    [
        SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left)
        [
            SNew(SBox).WidthOverride_Lambda([this] { return FOptionalSize(TierBandWidth); })
            [
                SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this] {
                    const auto* Tint = TierColors.Find(TierId);
                    return FSlateColor(Tint ? *Tint : TierColors.FindRef(1));
                })
            ]
        ]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(SBox).WidthOverride(62.f * .76f).HeightOverride(62.f * .76f)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                [
                    SNew(SImage).Image_Lambda([this] {
                        IconBrush.SetResourceObject(Icon);
                        if (Icon)
                        {
                            IconBrush.SetImageSize(FVector2D(Icon->GetSizeX(), Icon->GetSizeY()));
                        }
                        return Icon ? &IconBrush : FCoreStyle::Get().GetBrush("NoBrush");
                    }).ColorAndOpacity_Lambda([this] { return FSlateColor(IconTint); })
                ]
            ]
        ]
        + SOverlay::Slot().Padding(5.f).VAlign(VAlign_Top)
        [
            SNew(STextBlock).Text_Lambda([this] { return Label; })
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
            .ColorAndOpacity_Lambda([this] { return FSlateColor(TextTint); })
            .Visibility_Lambda([this] { return Label.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
        ]
        + SOverlay::Slot().Padding(5.f).VAlign(VAlign_Bottom)
        [
            SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            .Visibility_Lambda([this] { return bShowCount ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [
                SNew(STextBlock).Text_Lambda([this] { return FText::AsNumber(Count); })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
                .ColorAndOpacity_Lambda([this] { return FSlateColor(TextTint); })
            ]
        ]
    ];
}
