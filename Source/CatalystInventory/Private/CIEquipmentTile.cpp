#include "CIEquipmentTile.h"

#include "Engine/Texture.h"
#include "Presentation/CFMagritteBinding.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
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
    const auto* EquipmentTheme = Cast<UCIEquipmentTileTheme>(Theme);
    return EquipmentTheme ? *EquipmentTheme : *GetDefault<UCIEquipmentTileTheme>();
}

void UCIEquipmentTile::BindItem(const FCFMagritteBinding& Item)
{
    Super::BindItem(Item);
    PresentationKind.Reset();
    InventoryTypeCount = 0;
    bHasTypeCount = false;
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
        else if (Field->id == "inventoryTypeCount" || Field->id == "expertiseLevel")
        {
            if (const auto* Number = std::get_if<std::int64_t>(&Value.data); Number && *Number >= 0)
            {
                if (Field->id == "inventoryTypeCount")
                {
                    InventoryTypeCount = *Number;
                    bHasTypeCount = true;
                }
                else
                {
                    ExpertiseLevel = *Number;
                }
            }
        }
    }
    if (PresentationKind == TEXT("weapon") || PresentationKind == TEXT("tool") ||
        PresentationKind == TEXT("gear") || PresentationKind == TEXT("gadget"))
    {
        bShowCount = false;
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
    const auto AddMarker = [this](const FString& Id, const TCHAR* Glyph, FLinearColor Tint, bool bAttribute)
    {
        const auto Brush = MakeShared<FSlateBrush>();
        Markers->AddSlot().AutoWidth().Padding(0, 0, GetEquipmentTheme().MarkerGap, 0)
        [
            SNew(SBox)
            .WidthOverride_Lambda([this] { return FOptionalSize(GetEquipmentTheme().MarkerSize); })
            .HeightOverride_Lambda([this] { return FOptionalSize(GetEquipmentTheme().MarkerSize); })
            .ToolTipText(FText::FromString(Id))
            [
                SNew(SOverlay)
                + SOverlay::Slot()
                [
                    SNew(SImage).Image_Lambda([this, Brush, Id, bAttribute] {
                        return ImageBrush(*Brush, bAttribute ? GetEquipmentTheme().AttributeIcons.FindRef(Id).Get() : nullptr);
                    }).ColorAndOpacity(Tint)
                ]
                + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(FText::FromString(Glyph))
                    .Visibility_Lambda([this, Id, bAttribute] {
                        return bAttribute && GetEquipmentTheme().AttributeIcons.FindRef(Id)
                            ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                    }).ColorAndOpacity(Tint)
                ]
            ]
        ];
    };
    for (const auto& Slot : ModSlots)
    {
        const bool bFilled = !Slot.Value.IsEmpty();
        AddMarker(bFilled ? Slot.Key + TEXT(": ") + Slot.Value : Slot.Key, bFilled ? TEXT("■") : TEXT("□"),
            bFilled ? GetEquipmentTheme().FilledMod : GetEquipmentTheme().EmptyMod, false);
    }
    for (const FString& Id : OffensiveAttributes) { AddMarker(Id, TEXT("●"), GetEquipmentTheme().Offensive, true); }
    for (const FString& Id : DefensiveAttributes) { AddMarker(Id, TEXT("◆"), GetEquipmentTheme().Defensive, true); }
    for (const FString& Id : UtilityAttributes) { AddMarker(Id, TEXT("▲"), GetEquipmentTheme().Utility, true); }
}

TSharedRef<SWidget> UCIEquipmentTile::BuildTileContent()
{
    SAssignNew(Markers, SHorizontalBox);
    RefreshMarkers();
    const auto EquipmentVisibility = [this] {
        return PresentationKind.IsEmpty() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
    };
    return SNew(SBox)
        .MinDesiredWidth_Lambda([this] { return PresentationKind.IsEmpty() ? FOptionalSize()
            : FOptionalSize(GetEquipmentTheme().EquipmentSize.X); })
        .MinDesiredHeight_Lambda([this] { return PresentationKind.IsEmpty() ? FOptionalSize()
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
        + SOverlay::Slot()[Super::BuildTileContent()]
        + SOverlay::Slot()
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
            .Padding_Lambda([this] { return FMargin(GetInventoryTheme().Spacing); })
            .Visibility_Lambda(EquipmentVisibility)
            [
                SNew(SOverlay)
                + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
                [
                    SNew(STextBlock).Text_Lambda([this] { return bHasTypeCount ? FText::AsNumber(InventoryTypeCount) : FText::GetEmpty(); })
                    .Font_Lambda([this] { return GetInventoryTheme().CountFont; })
                    .ToolTipText(NSLOCTEXT("CatalystInventory", "TypeCount", "Items of this type in Inventory"))
                ]
                + SOverlay::Slot().VAlign(VAlign_Bottom)
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush"))
                    .Padding_Lambda([this] { return FMargin(0, 0, GetEquipmentTheme().BadgeSize, 0); })
                    [
                        SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                        .HAlign(HAlign_Left)[Markers.ToSharedRef()]
                    ]
                ]
                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride_Lambda([this] { return FOptionalSize(GetEquipmentTheme().BadgeSize); })
                    .HeightOverride_Lambda([this] { return FOptionalSize(GetEquipmentTheme().BadgeSize); })
                    .ToolTipText_Lambda([this] { return FText::FromString(TalentId); })
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
                    ]
                ]
                + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
                [
                    SNew(STextBlock).Text_Lambda([this] { return ExpertiseLevel ? FText::AsNumber(*ExpertiseLevel) : FText::GetEmpty(); })
                    .Font_Lambda([this] { return GetInventoryTheme().CountFont; })
                    .ToolTipText(NSLOCTEXT("CatalystInventory", "Expertise", "Expertise"))
                ]
            ]
        ]
    ];
}

void UCIEquipmentTile::ReleaseSlateResources(bool bReleaseChildren)
{
    Markers.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}
