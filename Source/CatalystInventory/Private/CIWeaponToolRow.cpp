#include "CIWeaponToolRow.h"
#include "CIInventoryRowData.h"
#include "SCIInspectorConsumableIcon.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CatalystInventory"

namespace
{
bool IsToolRowKind(const FString& Kind)
{
    return Kind == TEXT("tool") || Kind == TEXT("weapon") || Kind == TEXT("gadget");
}
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
    Consumable = CIInventoryRowData::Read(Item, "consumableTypeIdentifier").ToString();
    ConsumableIcon = CIInventoryRowData::Read(Item, "inspectorConsumableIcon").ToString();
    Manufacturer = CIInventoryRowData::Manufacturer(Item);
    UtilityTier = FText::GetEmpty();
    if (bSupported)
    {
        Stats = ReadStats(Item);
        if (PresentationKind == TEXT("gadget"))
        {
            if (const auto TierCard = CIInventoryRowData::Card(Item, "tier"))
            {
                for (const auto& Field : TierCard->Describe().orderedChildren())
                {
                    if (ReadIdentifier(Field->property("label")) == TEXT("Current tier"))
                    {
                        UtilityTier = CIInventoryRowData::DisplayValue(TierCard->Read(*Field));
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
                FString Display = CIInventoryRowData::DisplayValue(StatsCard->Read(*Field)).ToString();
                Display.RemoveFromEnd(TEXT(" RPM"));
                Display.RemoveFromEnd(TEXT(" rounds"));
                if (!Label.IsEmpty() && !Display.IsEmpty())
                {
                    Result.Add({Label.ToString(), Label, {}, FText::FromString(Display)});
                }
            }
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

void UCIWeaponToolRow::RefreshDetails()
{
    if (!Title)
    {
        return;
    }

    const auto& Style = GetWeaponToolTheme();
    Title->ClearChildren();
    FText Subtitle = Manufacturer;
    if (!UtilityTier.IsEmpty())
    {
        const FText Tier = FText::Format(LOCTEXT("UtilityTierValue", "Utility Tier {0}"), UtilityTier);
        Subtitle = Manufacturer.IsEmpty() ? Tier
            : FText::Format(LOCTEXT("ManufacturerAndTier", "{0} · {1}"), Manufacturer, Tier);
    }
    if (ConsumableIcon == TEXT("cartridge") || ConsumableIcon == TEXT("fuel"))
    {
        Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, Style.ContentInset, 0)
        [SNew(SCIInspectorConsumableIcon).Tint(Style.TextTint).Fuel(ConsumableIcon == TEXT("fuel"))];
    }
    else if (!Consumable.IsEmpty())
    {
        Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, Style.ContentInset, 0)
        [MakeIcon(Style.ConsumableIcons.FindRef(Consumable), FText::FromString(TEXT("▮▮▮")),
            FText::FromString(Consumable), Style.ConsumableSize)];
    }
    Title->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(Label).Font(Style.LabelFont).ColorAndOpacity(Style.TextTint)
            .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Label)]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(Subtitle).Font(Style.CountFont).ColorAndOpacity(Style.TextTint)
            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
            .Visibility(Subtitle.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)]
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
        const auto* Other = ComparisonStats.FindByPredicate([&Stat](const FStat& Candidate) { return Stat.Id == Candidate.Id; });
        if (Other && Stat.Value && Other->Value && *Stat.Value != *Other->Value)
        {
            const bool bHigher = *Stat.Value > *Other->Value;
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

float UCIWeaponToolRow::GetMarkerLeftInset() const
{
    if (HasTierBand())
    {
        return GetWeaponToolTheme().ModInsetFromTierBand;
    }
    return Super::GetMarkerLeftInset();
}

TSharedRef<SWidget> UCIWeaponToolRow::BuildInventoryContent()
{
    SAssignNew(Title, SHorizontalBox);
    SAssignNew(StatWidgets, SHorizontalBox);
    SAssignNew(Indicators, SHorizontalBox);
    RefreshDetails();

    const auto StatsPanel = SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
        .HAlign(HAlign_Left)[StatWidgets.ToSharedRef()];
    return BuildRow(Super::BuildInventoryContent(), Title.ToSharedRef(), StatsPanel, Indicators.ToSharedRef());
}

void UCIWeaponToolRow::ReleaseSlateResources(bool bReleaseChildren)
{
    Title.Reset();
    StatWidgets.Reset();
    Indicators.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

#undef LOCTEXT_NAMESPACE
