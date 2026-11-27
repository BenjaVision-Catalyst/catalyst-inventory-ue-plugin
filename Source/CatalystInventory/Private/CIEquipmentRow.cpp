#include "CIEquipmentRow.h"
#include "CIInventoryRowData.h"

#include "Engine/Texture.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CatalystInventory"

FString UCIEquipmentRow::ReadIdentifier(const magritte::Value& Value)
{
    const auto* String = std::get_if<std::string>(&Value.data);
    return String ? FString(UTF8_TO_TCHAR(String->c_str())) : FString();
}

UCIEquipmentRowTheme::UCIEquipmentRowTheme()
{
    Style.Background = FLinearColor(.075f, .114f, .153f, .24f);
    Style.FocusedBackground = FLinearColor(.086f, .067f, .212f);
    IconSize = 92.f;
    IconWidth = 144.f;
    LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 18);
    CountFont = FCoreStyle::GetDefaultFontStyle("Bold", 18);
    StatLabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
    LevelFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
    BadgeFont = FCoreStyle::GetDefaultFontStyle("Bold", 13);
    BadgeSize = 29.f;
    MarkerSize = 27.f;
    GlyphMarkerWidth = 13.f;
    AttributeGlyphMarkerWidth = 22.f;
    MarkerGap = 5.f;
}

const UCFTileTheme& UCIEquipmentRow::GetTileTheme() const
{
    return Theme ? Super::GetTileTheme() : *GetDefault<UCIEquipmentRowTheme>();
}

const UCIEquipmentRowTheme& UCIEquipmentRow::GetRowTheme() const
{
    const auto* RowTheme = Cast<UCIEquipmentRowTheme>(&GetTileTheme());
    return RowTheme ? *RowTheme : *GetDefault<UCIEquipmentRowTheme>();
}

TSharedRef<SWidget> UCIEquipmentRow::MakeIcon(UTexture* Texture, const FText& Fallback, const FText& Tooltip, float Size)
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

FMargin UCIEquipmentRow::GetIconPadding() const
{
    return GetRowTheme().ThumbnailInset;
}

TSharedRef<SWidget> UCIEquipmentRow::BuildExpertise()
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

TSharedRef<SWidget> UCIEquipmentRow::BuildRequiredLevel()
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

void UCIEquipmentRow::BindItem(const FCFMagritteBinding& Item)
{
    Super::BindItem(Item);
    InspectorAttributes.Reset();
    MaximumAttributes.Reset();
    Talents.Reset();
    RequiredLevel.Reset();
    bProficient = false;
    for (const auto& Field : Item.Describe().orderedChildren())
    {
        const auto Value = Item.Read(*Field);
        if (Field->id == "requiredPlayerLevel")
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
    const auto& Palette = GetEquipmentTheme();
    const auto AddInspectorAttributes = [&](std::string_view CardKind)
    {
        const auto Card = CIInventoryRowData::Card(Item, CardKind);
        if (!Card)
        {
            return;
        }
        for (const auto& Field : Card->Describe().orderedChildren())
        {
            const auto Visibility = Field->property("visible");
            const auto* Visible = std::get_if<bool>(&Visibility.data);
            const FString Id = ReadIdentifier(Field->property("label"));
            const FString Type = ReadIdentifier(Field->property("metadata:ciAttributeType"));
            if ((Visible && !*Visible) || Id.IsEmpty() || CIInventoryRowData::DisplayValue(Card->Read(*Field)).IsEmpty())
            {
                continue;
            }

            if (Type == TEXT("offensive"))
            {
                InspectorAttributes.Add({Id, TEXT("●"), Palette.Offensive});
            }
            else if (Type == TEXT("defensive"))
            {
                InspectorAttributes.Add({Id, TEXT("◆"), Palette.Defensive});
            }
            else if (Type == TEXT("utility"))
            {
                InspectorAttributes.Add({Id, TEXT("▲"), Palette.Utility});
            }
            const auto Meter = Field->property("metadata:ciMeterMillionths");
            if (const auto* Millionths = std::get_if<std::int64_t>(&Meter.data); Millionths && *Millionths == 1000000)
            {
                MaximumAttributes.AddUnique(Id);
            }
        }
    };
    if (PresentationKind != TEXT("gear"))
    {
        AddInspectorAttributes("primary");
    }
    AddInspectorAttributes("modifiers");
}

void UCIEquipmentRow::PopulateRowIndicators(SHorizontalBox& Box, const FString& ExcludedAttribute)
{
    const auto& Style = GetRowTheme();
    Box.ClearChildren();
    if (InspectorAttributes.IsEmpty())
    {
        PopulateAttributes(Box, MaximumAttributes, ExcludedAttribute);
    }
    for (const auto& Attribute : InspectorAttributes)
    {
        if (Attribute.Id != ExcludedAttribute)
        {
            AddMarker(Box, Attribute.Id, Attribute.Glyph, Attribute.Tint, true, MaximumAttributes.Contains(Attribute.Id));
        }
    }
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
    Box.AddSlot().AutoWidth()
    [
        SNew(SBox).MinDesiredWidth(Style.TalentAreaWidth).HAlign(HAlign_Left)
        [TalentWidgets]
    ];
}

TSharedRef<SWidget> UCIEquipmentRow::BuildRow(const TSharedRef<SWidget>& Thumbnail,
    const TSharedRef<SWidget>& Title, const TSharedRef<SWidget>& Details, const TSharedRef<SWidget>& Indicators)
{
    const auto& Style = GetRowTheme();
    const auto* DividerBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
    return SNew(SBox).HeightOverride(Style.RowHeight)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SBox).WidthOverride(Style.ThumbnailWidth)
            [Thumbnail]
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
                SNew(SBox).HeightOverride_Lambda([this] { return FOptionalSize(GetHeaderHeight()); })
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(Style.ContentInset, 0.f)
                    [Title]
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
                [Details]
                + SHorizontalBox::Slot().FillWidth(1.5f).VAlign(VAlign_Center)
                [
                    SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                    .HAlign(HAlign_Right)[Indicators]
                ]
            ]
        ]
    ];
}

#undef LOCTEXT_NAMESPACE
