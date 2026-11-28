#include "CIWeaponToolRow.h"
#include "CIInventoryRowData.h"
#include "SCIInspectorConsumableIcon.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CatalystInventory"

namespace
{
bool IsToolRowKind(const FString& Kind)
{
    return Kind == TEXT("tool") || Kind == TEXT("weapon") || Kind == TEXT("gadget");
}
}

UCIWeaponToolRowTheme::UCIWeaponToolRowTheme()
{
    ThumbnailInset.Bottom = 18.f;
}

const UCFTileTheme& UCIWeaponToolRow::GetTileTheme() const
{
    return Theme ? Super::GetTileTheme() : *GetDefault<UCIWeaponToolRowTheme>();
}

const UCIWeaponToolRowTheme& UCIWeaponToolRow::GetWeaponToolTheme() const
{
    const auto* RowTheme = Cast<UCIWeaponToolRowTheme>(Theme);
    return RowTheme ? *RowTheme : *GetDefault<UCIWeaponToolRowTheme>();
}

void UCIWeaponToolRow::BindItem(const FCFMagritteBinding& Item)
{
    Super::BindItem(Item);
    const bool bSupported = IsToolRowKind(PresentationKind);
    SetVisibility(bSupported ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Stats.Reset();
    Description = FText::GetEmpty();
    Consumable = CIInventoryRowData::Read(Item, "consumableTypeIdentifier").ToString();
    ConsumableIcon = CIInventoryRowData::Read(Item, "inspectorConsumableIcon").ToString();
    if (PresentationKind == TEXT("gadget"))
    {
        if (const auto Inspector = Item.Follow("inspector"))
        {
            Description = CIInventoryRowData::Read(*Inspector, "itemSummary");
        }
    }
    else if (bSupported)
    {
        Stats = ReadStats(Item);
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
            else if (Field->id == "value") { Number = CIInventoryRowData::Number(Value); }
        }
        if (!Id.IsEmpty() && Number.IsSet())
        {
            Result.Add({Id, FText::FromString(Name.IsEmpty() ? Id : Name), Number, FText::AsNumber(*Number)});
        }
    }
    if (Result.IsEmpty())
    {
        if (const auto StatsCard = CIInventoryRowData::Card(Item, "stats"))
        {
            for (const auto& Field : StatsCard->Describe().orderedChildren())
            {
                const FText Label = CIInventoryRowData::DisplayValue(Field->property("label"));
                auto Data = StatsCard->Read(*Field);
                FString Display = CIInventoryRowData::DisplayValue(Data).ToString();
                Display.RemoveFromEnd(TEXT(" RPM"));
                Display.RemoveFromEnd(TEXT(" rounds"));
                if (!Label.IsEmpty() && !Display.IsEmpty())
                {
                    const FString Id = Label.ToString();
                    if (Id == TEXT("Optimal Range") || Id == TEXT("Reload time"))
                    {
                        continue;
                    }
                    if (std::holds_alternative<std::string>(Data.data))
                    {
                        Data = magritte::Value(std::string(TCHAR_TO_UTF8(*Display)));
                    }
                    Result.Add({Id, Label, CIInventoryRowData::Number(Data), FText::FromString(Display)});
                }
            }
        }
    }
    TOptional<double> Damage;
    bool bHasDps = false;
    for (FStat& Stat : Result)
    {
        const FString Name = Stat.Label.ToString();
        if (Name.Equals(TEXT("Damage"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("DMG"), ESearchCase::IgnoreCase)
            || Stat.Id == TEXT("damagePerShot"))
        {
            Stat.Label = LOCTEXT("DamageAbbreviation", "DMG");
            Stat.Id = TEXT("damagePerShot");
            Damage = Stat.Value;
        }
        else if (Name.Equals(TEXT("DPS"), ESearchCase::IgnoreCase)
            || Name.Equals(TEXT("Damage per second"), ESearchCase::IgnoreCase) || Stat.Id == TEXT("damagePerSecond"))
        {
            Stat.Label = LOCTEXT("DpsAbbreviation", "DPS");
            Stat.Id = TEXT("damagePerSecond");
            bHasDps = true;
        }
        else if (Name.Equals(TEXT("Magazine"), ESearchCase::IgnoreCase) || Stat.Id == TEXT("magazineCapacity"))
        {
            Stat.Label = LOCTEXT("MagazineAbbreviation", "MAG");
            Stat.Id = TEXT("magazineCapacity");
        }
    }
    if (Damage)
    {
        for (int32 Index = 0; Index < Result.Num(); ++Index)
        {
            auto& Stat = Result[Index];
            if (Stat.Value && (Stat.Id.Equals(TEXT("Fire rate"), ESearchCase::IgnoreCase)
                || Stat.Id == TEXT("roundsPerMinute")))
            {
                if (bHasDps)
                {
                    Result.RemoveAt(Index);
                }
                else
                {
                    const double Dps = CIInventoryRowData::DamagePerSecond(*Damage, *Stat.Value);
                    Stat = {TEXT("damagePerSecond"), LOCTEXT("DpsAbbreviation", "DPS"), Dps, FText::AsNumber(Dps)};
                }
                break;
            }
        }
    }
    return Result;
}

void UCIWeaponToolRow::SetComparisonItem(const TOptional<FCFMagritteBinding>& Item)
{
    ComparisonStats = Item && PresentationKind != TEXT("gadget") ? ReadStats(*Item) : TArray<FStat>();
    RefreshStats();
    InvalidateLayoutAndVolatility();
}

void UCIWeaponToolRow::RefreshDetails()
{
    if (!Title)
    {
        return;
    }

    const auto& Style = GetWeaponToolTheme();
    Title->ClearChildren();
    TSharedRef<SWidget> ConsumableWidget = SNullWidget::NullWidget;
    if (PresentationKind == TEXT("gadget"))
    {
        const int32* Glyph = Style.SlotIconGlyphs.Find(FallbackIconIdentifier);
        if (Glyph)
        {
            auto Font = Style.SlotIconFont;
            Font.Size = 16.f;
            ConsumableWidget = SNew(STextBlock).Text(FText::AsCultureInvariant(FString::Chr(*Glyph)))
                .Font(Font).ColorAndOpacity(Style.TextTint);
        }
    }
    else if (ConsumableIcon == TEXT("cartridge") || ConsumableIcon == TEXT("fuel"))
    {
        ConsumableWidget = SNew(SCIInspectorConsumableIcon)
            .Tint(FLinearColor(1.f, .75f, .02f)).Fuel(ConsumableIcon == TEXT("fuel"));
    }
    else if (!Consumable.IsEmpty())
    {
        ConsumableWidget = MakeIcon(Style.ConsumableIcons.FindRef(Consumable),
            FText::FromString(TEXT("▮▮▮")), FText::FromString(Consumable), Style.ConsumableSize);
    }
    Title->AddSlot().AutoWidth().VAlign(VAlign_Center)
    [
        SNew(SBox).WidthOverride(Style.ConsumableSize).HAlign(HAlign_Left)[ConsumableWidget]
    ];
    Title->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
    [
        SNew(STextBlock).Text(Label).Font(Style.LabelFont).ColorAndOpacity(Style.TextTint)
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Label)
    ];
    PopulateRowIndicators(*Indicators);
    RefreshStats();
    InvalidateLayoutAndVolatility();
}

void UCIWeaponToolRow::RefreshStats()
{
    if (!StatWidgets)
    {
        return;
    }

    const auto& Style = GetWeaponToolTheme();
    StatWidgets->ClearChildren();
    for (const FStat& Stat : Stats)
    {
        FLinearColor Tint = Style.TextTint;
        FText Display = Stat.Display;
        if (Stat.Value && (Stat.Id == TEXT("damagePerShot") || Stat.Id == TEXT("damagePerSecond")))
        {
            Display = CIInventoryRowData::FormatCompactNumber(*Stat.Value);
        }
        else if (Stat.Value && Stat.Id == TEXT("magazineCapacity"))
        {
            FNumberFormattingOptions Formatting;
            Formatting.SetMaximumFractionalDigits(0);
            Display = FText::AsNumber(*Stat.Value, &Formatting);
        }
        FText Direction;
        const auto* Other = ComparisonStats.FindByPredicate([&Stat](const FStat& Candidate) { return Stat.Id == Candidate.Id; });
        if (Other && Stat.Value && Other->Value && *Stat.Value != *Other->Value)
        {
            const bool bHigher = *Stat.Value > *Other->Value;
            Tint = bHigher ? Style.HigherValue : Style.LowerValue;
            Direction = bHigher ? LOCTEXT("Higher", "▲") : LOCTEXT("Lower", "▼");
        }
        StatWidgets->AddSlot().AutoWidth()
        [
            SNew(SBox).WidthOverride(Style.StatWidth)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFitX).StretchDirection(EStretchDirection::DownOnly)
                .HAlign(HAlign_Center)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, -2.f)
                    [SNew(STextBlock).Text(Stat.Label.ToUpper()).Font(Style.StatLabelFont).ColorAndOpacity(Style.TextTint)]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(SBox).WidthOverride(12.f)]
                        + SHorizontalBox::Slot().AutoWidth()
                        [SNew(STextBlock).Text(Display).Font(Style.CountFont).ColorAndOpacity(FLinearColor::White)]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(SBox).WidthOverride(12.f).HAlign(HAlign_Right)
                            [SNew(STextBlock).Text(Direction).Font(Style.StatLabelFont).ColorAndOpacity(Tint)]
                        ]
                    ]
                ]
            ]
        ];
    }
}

float UCIWeaponToolRow::GetMarkerLeftInset() const
{
    if (HasTierBand())
    {
        return GetWeaponToolTheme().ModInsetFromTierBand;
    }
    return Super::GetMarkerLeftInset();
}

float UCIWeaponToolRow::GetHeaderHeight() const
{
    return PresentationKind == TEXT("gadget") ? 32.f : Super::GetHeaderHeight();
}

TSharedRef<SWidget> UCIWeaponToolRow::BuildInventoryContent()
{
    SAssignNew(Title, SHorizontalBox);
    SAssignNew(StatWidgets, SHorizontalBox);
    SAssignNew(Indicators, SHorizontalBox);
    RefreshDetails();

    const auto StatsPanel = SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
        .HAlign(HAlign_Left)[StatWidgets.ToSharedRef()];
    const auto Details = SNew(SOverlay)
        + SOverlay::Slot()[StatsPanel]
        + SOverlay::Slot()
        [
            SNew(STextBlock)
            .Text_Lambda([this] { return Description; })
            .Font(GetWeaponToolTheme().StatLabelFont)
            .ColorAndOpacity(GetWeaponToolTheme().TextTint)
            .AutoWrapText(true)
            .Visibility_Lambda([this] {
                return PresentationKind == TEXT("gadget") ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            })
        ];
    return BuildRow(Super::BuildInventoryContent(), Title.ToSharedRef(), Details, Indicators.ToSharedRef());
}

void UCIWeaponToolRow::ReleaseSlateResources(bool bReleaseChildren)
{
    Title.Reset();
    StatWidgets.Reset();
    Indicators.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

#undef LOCTEXT_NAMESPACE
