#include "CIInventoryTile.h"

#include "Engine/Texture.h"
#include "Presentation/CFMagritteBinding.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

UCIInventoryTileTheme::UCIInventoryTileTheme()
{
    Style.Background = FLinearColor(0.f, 0.f, 0.f, .05f);
    Style.FocusedBackground = FLinearColor::FromSRGBColor(FColor(74, 20, 140, 255));
    Style.FocusedBackground.A = .58f;
    Style.BackdropBlur = 24.f;
    Style.Border = FLinearColor(1.f, 1.f, 1.f, .15f);
    TierColors = {
        {1, FLinearColor(0.50f, 0.50f, 0.50f)},
        {2, FLinearColor(0.12f, 0.68f, 0.22f)},
        {3, FLinearColor(0.10f, 0.38f, 0.90f)},
        {4, FLinearColor(0.85f, 0.58f, 0.08f)},
        {5, FLinearColor(0.02f, 0.24f, 0.10f)},
        {6, FLinearColor(0.49f, 0.16f, 0.76f)}
    };
    LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 14);
    CountFont = FCoreStyle::GetDefaultFontStyle("Regular", 16);
}

UCIInventoryTile::UCIInventoryTile()
{
    IconBrush.DrawAs = ESlateBrushDrawType::Image;
}

void UCIInventoryTile::BindItem(const FCFMagritteBinding& Item)
{
    TierId = 1;
    bShowTier = false;
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
                bShowTier = true;
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
    bShowTier = bShowTier && !bShowCount;
    InvalidateLayoutAndVolatility();
}

const UCIInventoryTileTheme& UCIInventoryTile::GetInventoryTheme() const
{
    const auto* InventoryTheme = Cast<UCIInventoryTileTheme>(Theme);
    return InventoryTheme ? *InventoryTheme : *GetDefault<UCIInventoryTileTheme>();
}

const UCFTileTheme& UCIInventoryTile::GetTileTheme() const
{
    return Theme ? static_cast<const UCFTileTheme&>(*Theme) : GetInventoryTheme();
}

TSharedRef<SWidget> UCIInventoryTile::BuildTileContent()
{
    return SNew(SBox)
        .MinDesiredWidth_Lambda([this] { return FOptionalSize(GetInventoryTheme().MinimumSize.X); })
        .MinDesiredHeight_Lambda([this] { return FOptionalSize(GetInventoryTheme().MinimumSize.Y); })
    [
        SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left)
        [
            SNew(SBox).WidthOverride_Lambda([this] { return FOptionalSize(GetInventoryTheme().TierBandWidth); })
            .Visibility_Lambda([this] { return bShowTier && !bShowCount ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [
                SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this] {
                    const auto& Colors = GetInventoryTheme().TierColors;
                    const auto* Tint = Colors.Find(TierId);
                    return FSlateColor(Tint ? *Tint : Colors.FindRef(1));
                })
            ]
        ]
        + SOverlay::Slot()
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
            .Padding_Lambda([this] { return FMargin(GetInventoryTheme().Spacing); })
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
                    .Padding_Lambda([this] { return FMargin(0, 0, 0, GetInventoryTheme().Spacing); })
                    .Visibility_Lambda([this] { return TileLabel().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
                    [
                        SNew(STextBlock).Text_Lambda([this] { return TileLabel(); })
                        .Font_Lambda([this] { return GetInventoryTheme().LabelFont; })
                        .ColorAndOpacity_Lambda([this] { return FSlateColor(GetInventoryTheme().TextTint); })
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.f)
                [
                    SNew(SScaleBox).Stretch(EStretch::ScaleToFit).HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(SImage).Image_Lambda([this] {
                            IconBrush.SetResourceObject(Icon);
                            if (Icon)
                            {
                                IconBrush.SetImageSize(FVector2D(Icon->GetSurfaceWidth(), Icon->GetSurfaceHeight()));
                            }
                            return Icon ? &IconBrush : FCoreStyle::Get().GetBrush("NoBrush");
                        }).ColorAndOpacity_Lambda([this] { return FSlateColor(GetInventoryTheme().IconTint); })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
                    .Padding_Lambda([this] { return FMargin(0, GetInventoryTheme().Spacing, 0, 0); })
                    .Visibility_Lambda([this] { return bShowCount ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                    [
                        SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                        .HAlign(HAlign_Center)
                        [
                            SNew(STextBlock).Text_Lambda([this] { return FText::AsNumber(Count); })
                            .Font_Lambda([this] { return GetInventoryTheme().CountFont; })
                            .ColorAndOpacity_Lambda([this] { return FSlateColor(GetInventoryTheme().TextTint); })
                        ]
                    ]
                ]
            ]
        ]
    ];
}
