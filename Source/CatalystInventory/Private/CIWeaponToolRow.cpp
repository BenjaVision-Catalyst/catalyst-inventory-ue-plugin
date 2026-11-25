#include "CIWeaponToolRow.h"

#include "Engine/Texture.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CatalystInventory"

namespace
{
FString ReadIdentifier(const magritte::Value& Value)
{
    const auto* String = std::get_if<std::string>(&Value.data);
    return String ? FString(UTF8_TO_TCHAR(String->c_str())) : FString();
}

bool IsToolOrWeapon(const FString& Kind)
{
    return Kind == TEXT("tool") || Kind == TEXT("weapon");
}
}

UCIWeaponToolRowTheme::UCIWeaponToolRowTheme()
{
    LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 18);
    CountFont = FCoreStyle::GetDefaultFontStyle("Bold", 18);
    StatLabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 16);
    LevelFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
    BadgeFont = FCoreStyle::GetDefaultFontStyle("Bold", 13);
    BadgeSize = 29.f;
    MarkerSize = 27.f;
    GlyphMarkerWidth = 13.f;
    AttributeGlyphMarkerWidth = 22.f;
    MarkerGap = 5.f;
}

const UCFTileTheme& UCIWeaponToolRow::GetTileTheme() const
{
    return Theme ? Super::GetTileTheme() : *GetDefault<UCIWeaponToolRowTheme>();
}

const UCIWeaponToolRowTheme& UCIWeaponToolRow::GetRowTheme() const
{
    const auto* RowTheme = Cast<UCIWeaponToolRowTheme>(Theme);
    return RowTheme ? *RowTheme : *GetDefault<UCIWeaponToolRowTheme>();
}

void UCIWeaponToolRow::BindItem(const FCFMagritteBinding& Item)
{
    Super::BindItem(Item);
    const bool bSupported = IsToolOrWeapon(PresentationKind);
    SetVisibility(bSupported ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Stats.Reset();
    MaximumAttributes.Reset();
    Talents.Reset();
    RequiredLevel.Reset();
    Consumable.Reset();
    bProficient = false;
    if (bSupported)
    {
        Stats = ReadStats(Item);
        for (const auto& Field : Item.Describe().orderedChildren())
        {
            const auto Value = Item.Read(*Field);
            if (Field->id == "consumableTypeIdentifier")
            {
                Consumable = ReadIdentifier(Value);
            }
            else if (Field->id == "requiredPlayerLevel")
            {
                if (const auto* Level = std::get_if<std::int64_t>(&Value.data); Level && *Level >= 0)
                {
                    RequiredLevel = *Level;
                }
            }
            else if (Field->id == "isProficient")
            {
                const auto* Proficient = std::get_if<bool>(&Value.data);
                bProficient = Proficient && *Proficient;
            }
            else if ((Field->id == "maximumAttributes" || Field->id == "talentIdentifiers") && Value.isCollection())
            {
                auto& Identifiers = Field->id == "maximumAttributes" ? MaximumAttributes : Talents;
                for (const auto& Entry : Value.elements())
                {
                    FString Id = ReadIdentifier(Entry);
                    if (!Id.IsEmpty())
                    {
                        Identifiers.AddUnique(MoveTemp(Id));
                    }
                    if (Identifiers.Num() == 16)
                    {
                        break;
                    }
                }
            }
        }
    }
    RefreshDetails();
}

TArray<UCIWeaponToolRow::FStat> UCIWeaponToolRow::ReadStats(const FCFMagritteBinding& Item)
{
    TArray<FStat> Result;
    for (const auto& Stat : Item.Related("comparisonStats", 8))
    {
        FString Id;
        FString Name;
        TOptional<double> Number;
        for (const auto& Field : Stat.Describe().orderedChildren())
        {
            const auto Value = Stat.Read(*Field);
            if (Field->id == "statIdentifier") { Id = ReadIdentifier(Value); }
            else if (Field->id == "displayName") { Name = ReadIdentifier(Value); }
            else if (Field->id == "value")
            {
                if (const auto* Real = std::get_if<double>(&Value.data); Real && FMath::IsFinite(*Real))
                {
                    Number = *Real;
                }
                else if (const auto* Integer = std::get_if<std::int64_t>(&Value.data))
                {
                    Number = static_cast<double>(*Integer);
                }
            }
        }
        if (!Id.IsEmpty() && Number.IsSet())
        {
            Result.Add({Id, FText::FromString(Name.IsEmpty() ? Id : Name), *Number});
        }
    }
    return Result;
}

void UCIWeaponToolRow::SetComparisonItem(const TOptional<FCFMagritteBinding>& Item)
{
    ComparisonStats = Item ? ReadStats(*Item) : TArray<FStat>();
    RefreshStats();
    InvalidateLayoutAndVolatility();
}

TSharedRef<SWidget> UCIWeaponToolRow::MakeIcon(UTexture* Texture, const FText& Fallback, const FText& Tooltip, float Size)
{
    const auto Brush = MakeShared<FSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->DrawAs = ESlateBrushDrawType::Image;
    if (Texture)
    {
        Brush->SetImageSize(FVector2D(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight()));
    }
    TSharedRef<SWidget> Content = Texture
        ? StaticCastSharedRef<SWidget>(SNew(SImage).Image_Lambda([Brush] { return &*Brush; }))
        : StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(Fallback).Font(GetRowTheme().LabelFont)
            .ColorAndOpacity(GetRowTheme().TextTint));
    return SNew(SBox)
        .WidthOverride(Size).HeightOverride(Size)
        .ToolTipText(Tooltip)
        [
            SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
            .StretchDirection(Texture ? EStretchDirection::Both : EStretchDirection::DownOnly)
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [Content]
        ];
}

void UCIWeaponToolRow::RefreshDetails()
{
    if (!Title)
    {
        return;
    }

    const auto& Style = GetRowTheme();
    Title->ClearChildren();
    Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, Style.ContentInset, 0)
    [
        MakeIcon(Style.ConsumableIcons.FindRef(Consumable),
            Consumable.IsEmpty() ? FText::GetEmpty() : FText::FromString(TEXT("▮▮▮")),
            FText::FromString(Consumable), Style.ConsumableSize)
    ];
    Title->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
    [
        SNew(STextBlock).Text(Label).Font(Style.LabelFont).ColorAndOpacity(Style.TextTint)
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Label)
    ];

    Indicators->ClearChildren();
    PopulateAttributes(*Indicators, MaximumAttributes);
    const auto TalentWidgets = SNew(SHorizontalBox);
    if (Talents.IsEmpty())
    {
        TalentWidgets->AddSlot().AutoWidth()[BuildTalent()];
    }
    for (const FString& Id : Talents)
    {
        TalentWidgets->AddSlot().AutoWidth().Padding(0, 0, Style.MarkerGap, 0)
        [
            MakeIcon(GetEquipmentTheme().TalentIcons.FindRef(Id), FText::FromString(TEXT("★")),
                FText::FromString(Id), Style.BadgeSize)
        ];
    }
    Indicators->AddSlot().AutoWidth()
    [
        SNew(SBox).MinDesiredWidth(Style.TalentAreaWidth).HAlign(HAlign_Left)
        [TalentWidgets]
    ];
    RefreshStats();
    InvalidateLayoutAndVolatility();
}

void UCIWeaponToolRow::RefreshStats()
{
    if (!StatWidgets)
    {
        return;
    }

    const auto& Style = GetRowTheme();
    StatWidgets->ClearChildren();
    for (const FStat& Stat : Stats)
    {
        FLinearColor Tint = Style.TextTint;
        FText Display = FText::AsNumber(Stat.Value);
        const auto* Other = ComparisonStats.FindByPredicate([&Stat](const FStat& Candidate) { return Stat.Id == Candidate.Id; });
        if (Other && Stat.Value != Other->Value)
        {
            const bool bHigher = Stat.Value > Other->Value;
            Tint = bHigher ? Style.HigherValue : Style.LowerValue;
            Display = FText::Format(bHigher ? LOCTEXT("Higher", "{0} ▲") : LOCTEXT("Lower", "{0} ▼"), Display);
        }
        StatWidgets->AddSlot().AutoWidth()
        [
            SNew(SBox).WidthOverride(Style.StatWidth)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFitX).StretchDirection(EStretchDirection::DownOnly)
                .HAlign(HAlign_Left)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock).Text(Stat.Label).Font(Style.StatLabelFont).ColorAndOpacity(Style.TextTint)]
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock).Text(Display).Font(Style.CountFont).ColorAndOpacity(Tint)]
                ]
            ]
        ];
    }
}

FMargin UCIWeaponToolRow::GetIconPadding() const
{
    return GetRowTheme().ThumbnailInset;
}

float UCIWeaponToolRow::GetMarkerLeftInset() const
{
    if (HasTierBand())
    {
        return GetRowTheme().ModInsetFromTierBand;
    }
    return Super::GetMarkerLeftInset();
}

TSharedRef<SWidget> UCIWeaponToolRow::BuildExpertise()
{
    const auto& Style = GetRowTheme();
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SBox)
            .Visibility_Lambda([this] { return bProficient ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [MakeIcon(Style.ProficiencyIcon, LOCTEXT("ProficiencyGlyph", "✓"), LOCTEXT("Proficiency", "Proficient"), Style.BadgeSize)]
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SBox).WidthOverride(Style.BadgeSize).HeightOverride(Style.BadgeSize)
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            .Visibility_Lambda([this] { return ExpertiseLevel ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            [
                SNew(STextBlock).Text_Lambda([this] { return ExpertiseLevel ? FText::AsNumber(*ExpertiseLevel) : FText::GetEmpty(); })
                .Font(Style.BadgeFont).ColorAndOpacity(Style.TextTint)
                .ToolTipText(LOCTEXT("Expertise", "Expertise"))
            ]
        ];
}

TSharedRef<SWidget> UCIWeaponToolRow::BuildRequiredLevel()
{
    const auto& Style = GetRowTheme();
    return SNew(SVerticalBox)
        .Visibility_Lambda([this] { return RequiredLevel ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
        .ToolTipText(LOCTEXT("PlayerLevel", "Required player level"))
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
        [
            SNew(STextBlock).Font(Style.LevelFont).ColorAndOpacity(Style.TextTint)
            .Text(LOCTEXT("LevelLabel", "LEVEL"))
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
        [
            SNew(STextBlock).Font(Style.LevelFont).ColorAndOpacity(Style.TextTint)
            .Text_Lambda([this] { return RequiredLevel ? FText::AsNumber(*RequiredLevel) : FText::GetEmpty(); })
        ];
}

TSharedRef<SWidget> UCIWeaponToolRow::BuildInventoryContent()
{
    const auto& Style = GetRowTheme();
    const auto* DividerBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
    SAssignNew(Title, SHorizontalBox);
    SAssignNew(StatWidgets, SHorizontalBox);
    SAssignNew(Indicators, SHorizontalBox);
    RefreshDetails();

    return SNew(SBox).HeightOverride(Style.RowHeight)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SBox).WidthOverride(Style.ThumbnailWidth)
            [Super::BuildInventoryContent()]
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SBox).WidthOverride(Style.DividerThickness)
            [SNew(SBorder).Padding(0).BorderImage(DividerBrush).BorderBackgroundColor(Style.DividerTint)]
        ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox).HeightOverride(Style.HeaderHeight)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(Style.ContentInset, 0.f)
                    [Title.ToSharedRef()]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Style.ContentInset, 0.f)
                    [BuildRequiredLevel()]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox).HeightOverride(Style.DividerThickness)
                [SNew(SBorder).Padding(0).BorderImage(DividerBrush).BorderBackgroundColor(Style.DividerTint)]
            ]
            + SVerticalBox::Slot().FillHeight(1.f).Padding(Style.ContentInset, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(3.f).VAlign(VAlign_Center)
                [
                    SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                    .HAlign(HAlign_Left)[StatWidgets.ToSharedRef()]
                ]
                + SHorizontalBox::Slot().FillWidth(1.5f).VAlign(VAlign_Center)
                [
                    SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                    .HAlign(HAlign_Right)[Indicators.ToSharedRef()]
                ]
            ]
        ]
    ];
}

void UCIWeaponToolRow::ReleaseSlateResources(bool bReleaseChildren)
{
    Title.Reset();
    StatWidgets.Reset();
    Indicators.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

#undef LOCTEXT_NAMESPACE
