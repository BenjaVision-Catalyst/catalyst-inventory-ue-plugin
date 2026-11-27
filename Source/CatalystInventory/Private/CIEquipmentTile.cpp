#include "CIEquipmentTile.h"
#include "CIInventoryAttributeIcon.h"

#include "Engine/Texture.h"
#include "Presentation/CFMagritteBinding.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString Identifier(const magritte::Value& Value)
{
    const auto* String = std::get_if<std::string>(&Value.data);
    return String ? FString(UTF8_TO_TCHAR(String->c_str())) : FString();
}

void ReadIdentifiers(const magritte::Value& Value, TArray<FString>& Out)
{
    if (Value.isCollection())
    {
        for (const auto& Element : Value.elements())
        {
            FString Id = Identifier(Element);
            if (!Id.IsEmpty())
            {
                Out.Add(MoveTemp(Id));
            }
            if (Out.Num() >= 16)
            {
                break;
            }
        }
    }
}

const FSlateBrush* ImageBrush(FSlateBrush& Brush, UTexture* Texture)
{
    Brush.SetResourceObject(Texture);
    Brush.DrawAs = ESlateBrushDrawType::Image;
    if (Texture)
    {
        Brush.SetImageSize(FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight()));
    }
    return Texture ? &Brush : FCoreStyle::Get().GetBrush("NoBrush");
}
}

const UCIEquipmentTileTheme& UCIEquipmentTile::GetEquipmentTheme() const
{
    const auto* EquipmentTheme = Cast<UCIEquipmentTileTheme>(&GetTileTheme());
    return EquipmentTheme ? *EquipmentTheme : *GetDefault<UCIEquipmentTileTheme>();
}

void UCIEquipmentTile::BindItem(const FCFMagritteBinding& Item)
{
    Super::BindItem(Item);
    PresentationKind.Reset();
    ExpertiseLevel.Reset();
    ModSlots.Reset();
    OffensiveAttributes.Reset();
    DefensiveAttributes.Reset();
    UtilityAttributes.Reset();
    BrandId.Reset();
    TalentId.Reset();
    for (const auto& Field : Item.Describe().orderedChildren())
    {
        const auto Value = Item.Read(*Field);
        if (Field->id == "presentationKind") { PresentationKind = Identifier(Value); }
        else if (Field->id == "brandIdentifier") { BrandId = Identifier(Value); }
        else if (Field->id == "talentIdentifier") { TalentId = Identifier(Value); }
        else if (Field->id == "offensiveAttributes") { ReadIdentifiers(Value, OffensiveAttributes); }
        else if (Field->id == "defensiveAttributes") { ReadIdentifiers(Value, DefensiveAttributes); }
        else if (Field->id == "utilityAttributes") { ReadIdentifiers(Value, UtilityAttributes); }
        else if (Field->id == "expertiseLevel")
        {
            if (const auto* Number = std::get_if<std::int64_t>(&Value.data); Number && *Number >= 0)
            {
                ExpertiseLevel = *Number;
            }
        }
    }
    if (PresentationKind == TEXT("weapon") || PresentationKind == TEXT("tool") ||
        PresentationKind == TEXT("gear") || PresentationKind == TEXT("gadget"))
    {
        bShowCount = false;
        bShowTier = bShowTier && PresentationKind != TEXT("gadget");
        for (const auto& Slot : Item.Related("modSlots", 16))
        {
            FString SlotId, ModId;
            for (const auto& Field : Slot.Describe().orderedChildren())
            {
                if (Field->id == "slotIdentifier") { SlotId = Identifier(Slot.Read(*Field)); }
                else if (Field->id == "equippedModIdentifier") { ModId = Identifier(Slot.Read(*Field)); }
            }
            if (!SlotId.IsEmpty())
            {
                ModSlots.Emplace(MoveTemp(SlotId), MoveTemp(ModId));
            }
        }
    }
    else
    {
        PresentationKind.Reset();
    }
    RefreshMarkers();
    InvalidateLayoutAndVolatility();
}

void UCIEquipmentTile::RefreshMarkers()
{
    if (!Markers)
    {
        return;
    }

    Markers->ClearChildren();
    for (const auto& Slot : ModSlots)
    {
        const bool bFilled = !Slot.Value.IsEmpty();
        const auto& Style = GetEquipmentTheme();
        Markers->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, Style.MarkerGap, 0)
        [
            SNew(SBox).WidthOverride(Style.GlyphMarkerWidth).HeightOverride(Style.MarkerSize)
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            .ToolTipText(FText::FromString(bFilled ? Slot.Key + TEXT(": ") + Slot.Value : Slot.Key))
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFitY).StretchDirection(EStretchDirection::DownOnly)
                [
                    SNew(STextBlock).Text(FText::FromString(bFilled ? TEXT("■") : TEXT("□")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", FMath::RoundToInt(Style.MarkerSize * 1.2f)))
                    .ColorAndOpacity(bFilled ? Style.FilledMod : Style.EmptyMod)
                ]
            ]
        ];
    }
    if (!IsRowPresentation())
    {
        PopulateAttributes(*Markers, {});
    }
}

void UCIEquipmentTile::PopulateAttributes(SHorizontalBox& Box, const TArray<FString>& MaximumAttributes,
    const FString& ExcludedAttribute)
{
    const auto Populate = [&](const TArray<FString>& Attributes, FStringView Type)
    {
        for (const FString& Id : Attributes)
        {
            if (Id != ExcludedAttribute)
            {
                AddAttributeMarker(Box, Id, Type, MaximumAttributes.Contains(Id));
            }
        }
    };
    Populate(OffensiveAttributes, TEXTVIEW("offensive"));
    Populate(DefensiveAttributes, TEXTVIEW("defensive"));
    Populate(UtilityAttributes, TEXTVIEW("utility"));
}

void UCIEquipmentTile::AddAttributeMarker(SHorizontalBox& Box, const FString& Id, FStringView Type,
    bool bMaximum, float Size)
{
    const auto& Style = GetEquipmentTheme();
    const auto Tint = CIInventoryAttributeIcon::Tint(GetInventoryTheme(), Type);
    if (!Tint)
    {
        return;
    }
    const float MaximumInset = bMaximum ? Style.MaximumMarkerInset : 0.f;
    Box.AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, Style.MarkerGap, 0)
    [
        SNew(SBox)
        .WidthOverride(Size > 0.f ? Size : CIInventoryAttributeIcon::Diameter)
        .HeightOverride(Size > 0.f ? Size : Style.MarkerSize)
        .ToolTipText(FText::FromString(Id))
        [
            SNew(SOverlay)
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0, 0, 0, MaximumInset))
            [CIInventoryAttributeIcon::Build(*Tint)]
            + SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("▲")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", Style.MaximumMarkerFontSize))
                .ColorAndOpacity(*Tint)
                .Visibility(bMaximum ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
            ]
        ]
    ];
}

TSharedRef<SWidget> UCIEquipmentTile::BuildInventoryContent()
{
    SAssignNew(Markers, SHorizontalBox);
    RefreshMarkers();
    const auto EquipmentVisibility = [this] {
        return PresentationKind.IsEmpty() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
    };
    return SNew(SBox)
        .MinDesiredWidth_Lambda([this] { return (PresentationKind.IsEmpty() || IsRowPresentation()) ? FOptionalSize()
            : FOptionalSize(GetEquipmentTheme().EquipmentSize.X); })
        .MinDesiredHeight_Lambda([this] { return (PresentationKind.IsEmpty() || IsRowPresentation()) ? FOptionalSize()
            : FOptionalSize(GetEquipmentTheme().EquipmentSize.Y); })
    [
        SNew(SOverlay)
        + SOverlay::Slot().HAlign(HAlign_Left)
        [
            SNew(SBox).WidthOverride_Lambda([this] { return FOptionalSize(GetEquipmentTheme().BrandWidth); }).Visibility_Lambda(EquipmentVisibility)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                [
                    SNew(SImage)
                    .Image_Lambda([this] { return ImageBrush(BrandBrush, GetEquipmentTheme().BrandIcons.FindRef(BrandId)); })
                    .ColorAndOpacity_Lambda([this] { return FSlateColor(GetEquipmentTheme().BrandTint); })
                    .ToolTipText_Lambda([this] { return FText::FromString(BrandId); })
                ]
            ]
        ]
        + SOverlay::Slot()[Super::BuildInventoryContent()]
        + SOverlay::Slot()
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
            .Padding_Lambda([this] { return FMargin(GetInventoryTheme().Spacing); })
            .Visibility_Lambda(EquipmentVisibility)
            [
                SNew(SOverlay)
                + SOverlay::Slot().VAlign(VAlign_Bottom)
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
                    .Padding_Lambda([this] {
                        const float Left = GetMarkerLeftInset() - GetInventoryTheme().Spacing;
                        return FMargin(Left, 0.f, GetEquipmentTheme().BadgeSize, 0.f);
                    })
                    [
                        SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                        .HAlign(HAlign_Left)[Markers.ToSharedRef()]
                    ]
                ]
                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)
                [
                    IsRowPresentation() ? SNullWidget::NullWidget : BuildTalent()
                ]
                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
                [
                    BuildExpertise()
                ]
            ]
        ]
    ];
}

TSharedRef<SWidget> UCIEquipmentTile::BuildTalent()
{
    return SNew(SBox)
        .WidthOverride_Lambda([this] { return FOptionalSize(GetEquipmentTheme().BadgeSize); })
        .HeightOverride_Lambda([this] { return FOptionalSize(GetEquipmentTheme().BadgeSize); })
        .ToolTipText_Lambda([this] { return FText::FromString(TalentId); })
        .Visibility_Lambda([this] { return TalentId.IsEmpty() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
        [
            SNew(SOverlay)
            + SOverlay::Slot()[SNew(SImage).Image_Lambda([this] {
                return ImageBrush(TalentBrush, GetEquipmentTheme().TalentIcons.FindRef(TalentId)); })]
            + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("★")))
                .Visibility_Lambda([this] { return !TalentId.IsEmpty() && !GetEquipmentTheme().TalentIcons.FindRef(TalentId)
                    ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            ]
        ];
}

TSharedRef<SWidget> UCIEquipmentTile::BuildExpertise()
{
    return SNew(STextBlock).Text_Lambda([this] { return ExpertiseLevel ? FText::AsNumber(*ExpertiseLevel) : FText::GetEmpty(); })
        .Font_Lambda([this] { return GetInventoryTheme().CountFont; })
        .ToolTipText(NSLOCTEXT("CatalystInventory", "Expertise", "Expertise"));
}

void UCIEquipmentTile::ReleaseSlateResources(bool bReleaseChildren)
{
    Markers.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}
