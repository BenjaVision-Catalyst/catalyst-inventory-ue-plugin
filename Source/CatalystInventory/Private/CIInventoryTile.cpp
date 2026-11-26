#include "CIInventoryTile.h"

#include "Engine/Texture.h"
#include "Fonts/CompositeFont.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Presentation/CFMagritteBinding.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
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
    GroupLabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 16);
    LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 14);
    CountFont = FCoreStyle::GetDefaultFontStyle("Regular", 16);
    if (const auto Foundation = IPluginManager::Get().FindPlugin(TEXT("CatalystFoundation")))
    {
        static const auto DefaultFont = MakeShared<FCompositeFont>(NAME_None, Foundation->GetBaseDir()
            / TEXT("Resources/Fonts/Font Awesome 5 Pro-Light-300.otf"), EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
        SlotIconFont = FSlateFontInfo(DefaultFont, 28.f);
    }
    SlotIconGlyphs = {
        {TEXT("MeleeWeapon"), 0xf6cb}, {TEXT("LeftHandWeapon"), 0xf140}, {TEXT("RangedWeapon"), 0xf05b},
        {TEXT("Mask"), 0xf6ee}, {TEXT("Backpack"), 0xf5d4}, {TEXT("Chest"), 0xe085},
        {TEXT("Gloves"), 0xf438}, {TEXT("KneePads"), 0xf3ed}, {TEXT("ModStorage"), 0xf552},
        {TEXT("GadgetLeft"), 0xf2db}, {TEXT("GadgetRight"), 0xf2db},
        {TEXT("Hat"), 0xf7a8}, {TEXT("Neck"), 0xf7c1}, {TEXT("Shirt"), 0xf553},
        {TEXT("Pants"), 0xf554}, {TEXT("Shoes"), 0xf782}, {TEXT("Glasses"), 0xf530},
        {TEXT("BackpackTrophy"), 0xf559}, {TEXT("Patches"), 0xf335}, {TEXT("Watch"), 0xf2e1},
        {TEXT("Emotes"), 0xf630}, {TEXT("Uniforms"), 0xe086}
    };
}

UCIInventoryTileTheme* UCIInventoryTileTheme::CreateProjectedSurfaceTheme(UObject* Outer) const
{
    auto* SurfaceTheme = DuplicateObject<UCIInventoryTileTheme>(this, Outer);
    SurfaceTheme->Style.BackdropBlur = 0.f;
    return SurfaceTheme;
}

const UCIInventoryTierStyle& UCIInventoryTileTheme::GetTierStyle() const
{
    return TierStyle ? *TierStyle : *GetDefault<UCIInventoryTierStyle>();
}

UCIInventoryTile::UCIInventoryTile()
{
    IconBrush.DrawAs = ESlateBrushDrawType::Image;
}

void UCIInventoryTile::BindItem(const FCFMagritteBinding& Item)
{
    const FString PreviousDefinition = DefinitionIdentifier;
    FString EquippedDefinition;
    FName CategoryIdentifier;
    bool bHasCategory = false;
    bool bEmptyEquipmentSlot = false;
    EmptySlotIdentifier = NAME_None;
    TierId = 1;
    bShowTier = false;
    Label = FText::GetEmpty();
    DefinitionIdentifier.Reset();
    bShowCount = false;
    Count = 0;
    InventoryTypeCount = 0;
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
        else if (Field->id == "definitionIdentifier")
        {
            if (const auto* Definition = std::get_if<std::string>(&Value.data))
            {
                DefinitionIdentifier = UTF8_TO_TCHAR(Definition->c_str());
            }
        }
        else if (Field->id == "categoryIdentifier")
        {
            bHasCategory = true;
            if (const auto* Category = std::get_if<std::string>(&Value.data))
            {
                CategoryIdentifier = FName(UTF8_TO_TCHAR(Category->c_str()));
            }
        }
        else if (Field->id == "equippedDefinitionIdentifier")
        {
            if (const auto* Equipped = std::get_if<std::string>(&Value.data))
            {
                EquippedDefinition = UTF8_TO_TCHAR(Equipped->c_str());
            }
        }
        else if (Field->id == "isEmptySlot")
        {
            const auto* Empty = std::get_if<bool>(&Value.data);
            bEmptyEquipmentSlot = Empty && *Empty;
        }
        else if (Field->id == "inventoryTypeCount")
        {
            if (const auto* Quantity = std::get_if<std::int64_t>(&Value.data); Quantity && *Quantity > 0)
            {
                InventoryTypeCount = *Quantity;
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
    if (bHasCategory)
    {
        Label = FText::GetEmpty();
        DefinitionIdentifier = MoveTemp(EquippedDefinition);
        EmptySlotIdentifier = DefinitionIdentifier.IsEmpty() ? CategoryIdentifier : NAME_None;
        if (PreviousDefinition != DefinitionIdentifier)
        {
            Icon = nullptr;
        }
        SetVisibility(CategoryIdentifier.IsNone() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    }
    else if (bEmptyEquipmentSlot)
    {
        EmptySlotIdentifier = FName(*DefinitionIdentifier);
        DefinitionIdentifier.Reset();
    }
    bShowTier = bShowTier && !bShowCount;
    InvalidateLayoutAndVolatility();
}

const UCIInventoryTileTheme& UCIInventoryTile::GetInventoryTheme() const
{
    const auto* InventoryTheme = Cast<UCIInventoryTileTheme>(&GetTileTheme());
    return InventoryTheme ? *InventoryTheme : *GetDefault<UCIInventoryTileTheme>();
}

const UCFTileTheme& UCIInventoryTile::GetTileTheme() const
{
    return Theme ? static_cast<const UCFTileTheme&>(*Theme) : *GetDefault<UCIInventoryTileTheme>();
}

float UCIInventoryTile::GetVisibleTierBandWidth() const
{
    return bShowTier && !bShowCount ? FMath::Max(0.f, GetInventoryTheme().GetTierStyle().BandWidth) : 0.f;
}

TSharedRef<SWidget> UCIInventoryTile::BuildTileContent()
{
    return SNew(SBox)
        .MinDesiredWidth_Lambda([this] { return FOptionalSize(GetInventoryTheme().MinimumSize.X); })
        .MinDesiredHeight_Lambda([this] { return FOptionalSize(GetInventoryTheme().MinimumSize.Y); })
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SBox).WidthOverride_Lambda([this] { return FOptionalSize(GetVisibleTierBandWidth()); })
            .Visibility_Lambda([this] { return HasTierBand() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [
                SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this] {
                    const auto& Colors = GetInventoryTheme().TierColors;
                    const auto* Tint = Colors.Find(TierId);
                    return FSlateColor(Tint ? *Tint : Colors.FindRef(1));
                })
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [BuildInventoryContent()]
    ];
}

TSharedRef<SWidget> UCIInventoryTile::BuildInventoryContent()
{
    auto Content = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
            .Padding_Lambda([this] { return FMargin(0, 0, 0, GetInventoryTheme().Spacing); })
            .Visibility_Lambda([this] { return TileLabel().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
            [BuildTileLabel()]
        ];

    Content->AddSlot().FillHeight(1.f).Padding(TAttribute<FMargin>::CreateLambda([this] { return GetIconPadding(); }))
    [BuildTileIcon()];
    Content->AddSlot().AutoHeight()
    [
        SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
        .Padding_Lambda([this] { return FMargin(0, GetInventoryTheme().Spacing, 0, 0); })
        .Visibility_Lambda([this] { return bShowCount && Count > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
        [
            SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            .HAlign(HAlign_Center)
            [
                SNew(STextBlock).Text_Lambda([this] { return Count > 0 ? FText::AsNumber(Count) : FText::GetEmpty(); })
                .Font_Lambda([this] { return GetInventoryTheme().CountFont; })
                .ColorAndOpacity_Lambda([this] { return FSlateColor(GetInventoryTheme().TextTint); })
            ]
        ]
    ];

    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
        .Padding_Lambda([this] { return FMargin(GetInventoryTheme().Spacing); })
        [
            SNew(SOverlay)
            + SOverlay::Slot()[Content]
            + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
            [
                SNew(STextBlock).Tag(TEXT("InventoryTileTypeCount"))
                .Text_Lambda([this] {
                    return InventoryTypeCount > 0 && ShowsTypeCount() ? FText::AsNumber(InventoryTypeCount) : FText::GetEmpty();
                })
                .Font_Lambda([this] { return GetInventoryTheme().CountFont; })
                .ColorAndOpacity_Lambda([this] { return FSlateColor(GetInventoryTheme().TextTint); })
                .ToolTipText(NSLOCTEXT("CatalystInventory", "TypeCount", "Items of this type in Inventory"))
                .Visibility_Lambda([this] {
                    return InventoryTypeCount > 0 && ShowsTypeCount() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                })
            ]
        ];
}

TSharedRef<SWidget> UCIInventoryTile::BuildTileLabel()
{
    return SNew(STextBlock).Text_Lambda([this] { return TileLabel(); })
        .Font_Lambda([this] { return TileLabelFont(); })
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
        .ToolTipText_Lambda([this] { return TileLabel(); })
        .ColorAndOpacity_Lambda([this] { return FSlateColor(GetInventoryTheme().TextTint); });
}

UTexture* UCIInventoryTile::ResolveTileIcon() const
{
    return EmptySlotIdentifier.IsNone() ? GetTileIcon() : GetInventoryTheme().SlotIcons.FindRef(EmptySlotIdentifier).Get();
}

TSharedRef<SWidget> UCIInventoryTile::BuildTileIcon()
{
    const auto IconSize = [this] { return FOptionalSize(FMath::Max(1.f, GetInventoryTheme().IconSize)); };
    return SNew(SScaleBox)
        .Stretch(EStretch::ScaleToFit)
        .StretchDirection(EStretchDirection::DownOnly)
        .HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(SBox).Tag(TEXT("InventoryTileIcon"))
            .WidthOverride_Lambda(IconSize)
            .HeightOverride_Lambda(IconSize)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit).HAlign(HAlign_Center).VAlign(VAlign_Center)
                [
                    SNew(SOverlay)
                    + SOverlay::Slot()
                    [
                        SNew(SImage).Image_Lambda([this]() -> const FSlateBrush* {
                            UTexture* Texture = ShowsItemIcon() ? ResolveTileIcon() : nullptr;
                            IconBrush.SetResourceObject(Texture);
                            if (Texture)
                            {
                                IconBrush.SetImageSize(FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight()));
                                return &IconBrush;
                            }
                            return FCoreStyle::Get().GetBrush("NoBrush");
                        }).ColorAndOpacity_Lambda([this] {
                            const auto& Style = GetInventoryTheme();
                            return FSlateColor(EmptySlotIdentifier.IsNone() ? Style.IconTint : Style.EmptySlotIconTint);
                        })
                    ]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Visibility_Lambda([this] {
                            return ShowsItemIcon() && !EmptySlotIdentifier.IsNone() && !ResolveTileIcon()
                                ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                        })
                        .Text_Lambda([this] {
                            const int32* Glyph = GetInventoryTheme().SlotIconGlyphs.Find(EmptySlotIdentifier);
                            return FText::AsCultureInvariant(FString::Chr(Glyph ? *Glyph : 0xf12e));
                        })
                        .Font_Lambda([this] { return GetInventoryTheme().SlotIconFont; })
                        .ColorAndOpacity_Lambda([this] { return FSlateColor(GetInventoryTheme().EmptySlotIconTint); })
                    ]
                ]
            ]
        ];
}
