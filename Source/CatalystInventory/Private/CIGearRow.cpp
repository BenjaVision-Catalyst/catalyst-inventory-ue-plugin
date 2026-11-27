#include "CIGearRow.h"
#include "CIInventoryRowData.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CatalystInventoryGear"

namespace
{
bool ReadFlag(const magritte::Value& Value)
{
    const auto* Flag = std::get_if<bool>(&Value.data);
    return Flag && *Flag;
}
}

UCIGearRowTheme::UCIGearRowTheme()
{
    HeaderHeight = 50.f;
    BrandWidth = 106.f;
    TalentAreaWidth = 0.f;
}

const UCFTileTheme& UCIGearRow::GetTileTheme() const
{
    return Theme ? Super::GetTileTheme() : *GetDefault<UCIGearRowTheme>();
}

const UCIGearRowTheme& UCIGearRow::GetGearTheme() const
{
    const auto* GearTheme = Cast<UCIGearRowTheme>(Theme);
    return GearTheme ? *GearTheme : *GetDefault<UCIGearRowTheme>();
}

void UCIGearRow::BindItem(const FCFMagritteBinding& Item)
{
    Super::BindItem(Item);
    const bool bGear = PresentationKind == TEXT("gear");
    SetVisibility(bGear ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    Manufacturer = CIInventoryRowData::Manufacturer(Item);
    GearSlot = FName(*CIInventoryRowData::Read(Item, "gearSlotIdentifier").ToString());
    Attribute.Reset();
    bJunk = false;
    bFavorite = false;
    if (bGear)
    {
        for (const auto& Field : Item.Describe().orderedChildren())
        {
            if (Field->id == "isJunk") { bJunk = ReadFlag(Item.Read(*Field)); }
            else if (Field->id == "isFavorite") { bFavorite = ReadFlag(Item.Read(*Field)); }
        }
        if (const auto PrimaryBinding = Item.Follow("primaryAttribute"))
        {
            FPrimaryAttribute Result;
            FString Format;
            TOptional<double> Amount;
            for (const auto& Field : PrimaryBinding->Describe().orderedChildren())
            {
                const auto Value = PrimaryBinding->Read(*Field);
                if (Field->id == "attributeIdentifier") { Result.Id = ReadIdentifier(Value); }
                else if (Field->id == "displayName") { Result.Label = FText::FromString(ReadIdentifier(Value)); }
                else if (Field->id == "attributeType") { Result.Type = ReadIdentifier(Value); }
                else if (Field->id == "valueFormat") { Format = ReadIdentifier(Value); }
                else if (Field->id == "isMaximum") { Result.bMaximum = ReadFlag(Value); }
                else if (Field->id == "value")
                {
                    if (const auto* Real = std::get_if<double>(&Value.data); Real && FMath::IsFinite(*Real))
                    {
                        Amount = *Real;
                    }
                    else if (const auto* Integer = std::get_if<std::int64_t>(&Value.data))
                    {
                        Amount = static_cast<double>(*Integer);
                    }
                }
            }
            if (Amount && !Result.Id.IsEmpty() && !Result.Label.IsEmpty())
            {
                FNumberFormattingOptions Options;
                Options.SetAlwaysSign(Format == TEXT("signedNumber") || Format == TEXT("signedPercent"));
                Result.Value = Format == TEXT("signedPercent") ? FText::AsPercent(*Amount, &Options)
                    : FText::AsNumber(*Amount, &Options);
                Attribute.Emplace(MoveTemp(Result));
            }
        }
        if (!Attribute)
        {
            if (const auto PrimaryCard = CIInventoryRowData::Card(Item, "primary"))
            {
                for (const auto& Field : PrimaryCard->Describe().orderedChildren())
                {
                    FPrimaryAttribute Result;
                    Result.Label = CIInventoryRowData::DisplayValue(Field->property("label"));
                    Result.Value = CIInventoryRowData::DisplayValue(PrimaryCard->Read(*Field));
                    if (!Result.Label.IsEmpty() && !Result.Value.IsEmpty())
                    {
                        Result.Id = Result.Label.ToString();
                        Result.Type = ReadIdentifier(Field->property("metadata:ciAttributeType"));
                        const auto Meter = Field->property("metadata:ciMeterMillionths");
                        if (const auto* Millionths = std::get_if<std::int64_t>(&Meter.data))
                        {
                            Result.bMaximum = *Millionths == 1000000;
                        }
                        Attribute.Emplace(MoveTemp(Result));
                        break;
                    }
                }
            }
        }
    }
    RefreshDetails();
}

void UCIGearRow::RefreshDetails()
{
    if (!Title)
    {
        return;
    }

    const auto& Style = GetGearTheme();
    Title->ClearChildren();
    auto ManufacturerFont = Style.CountFont;
    ManufacturerFont.Size = 14.f;
    const FName Slot = GearSlot.IsNone() ? FallbackIconIdentifier : GearSlot;
    auto* SlotArt = Style.GearSlotIcons.FindRef(Slot.ToString()).Get();
    if (!SlotArt && Slot == TEXT("Chest"))
    {
        SlotArt = Icon;
    }
    const int32* SlotGlyph = Style.SlotIconGlyphs.Find(Slot);
    auto SlotFont = Style.SlotIconFont;
    SlotFont.Size = 16.f;
    TSharedRef<SWidget> SlotWidget = SNullWidget::NullWidget;
    if (!Slot.IsNone())
    {
        if (SlotArt || !SlotGlyph)
        {
            SlotWidget = MakeIcon(SlotArt, FText::FromString(TEXT("◇")), FText::FromName(Slot), Style.BadgeSize);
        }
        else
        {
            SlotWidget = SNew(STextBlock).Text(FText::AsCultureInvariant(FString::Chr(*SlotGlyph)))
                .Font(SlotFont).ColorAndOpacity(Style.TextTint);
        }
    }
    Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Style.MarkerGap, 0.f)
    [
        SNew(SBox).WidthOverride(Style.PrimaryMarkerSize).HAlign(HAlign_Center)
        [
            SlotWidget
        ]
    ];
    Title->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(Label).Font(Style.LabelFont).ColorAndOpacity(Style.TextTint)
            .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Label)]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, -2.f, 0.f, 0.f)
        [SNew(STextBlock).Text(Manufacturer).Font(ManufacturerFont).ColorAndOpacity(Style.TextTint)
            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
            .Visibility(Manufacturer.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)]
    ];

    Primary->ClearChildren();
    if (Attribute)
    {
        AddAttributeMarker(*Primary, Attribute->Id, Attribute->Type, Attribute->bMaximum, Style.PrimaryMarkerSize);
        const FString AttributeName = Attribute->Label.ToString();
        const bool bArmour = Attribute->Id.Equals(TEXT("armor"), ESearchCase::IgnoreCase)
            || Attribute->Id.Equals(TEXT("armour"), ESearchCase::IgnoreCase)
            || AttributeName.Equals(TEXT("armor"), ESearchCase::IgnoreCase)
            || AttributeName.Equals(TEXT("armour"), ESearchCase::IgnoreCase);
        const FText AttributeLabel = CIInventoryRowData::NormalizeLabel(Attribute->Label);
        const FText StatLabel = bArmour ? LOCTEXT("ArmourAbbreviation", "ARM") : AttributeLabel;
        Primary->AddSlot().FillWidth(1.f).HAlign(HAlign_Left).VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, -2.f)
            [SNew(STextBlock).Text(StatLabel).Font(Style.StatLabelFont).ColorAndOpacity(Style.TextTint)
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(AttributeLabel)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [SNew(STextBlock).Text(Attribute->Value).Font(Style.CountFont).ColorAndOpacity(Style.TextTint)
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Attribute->Value)]
        ];
    }
    PopulateRowIndicators(*Indicators, Attribute ? Attribute->Id : FString());
    if (FavoriteIndicator)
    {
        FavoriteIndicator->SetVisibility(bFavorite ? EVisibility::Visible : EVisibility::Collapsed);
    }
    InvalidateLayoutAndVolatility();
}

TSharedRef<SWidget> UCIGearRow::BuildThumbnail()
{
    const auto& Style = GetGearTheme();
    return SNew(SOverlay)
        + SOverlay::Slot()[Super::BuildInventoryContent()]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(SBox).Visibility_Lambda([this] { return bJunk ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
            [MakeIcon(Style.JunkIcon, LOCTEXT("JunkGlyph", "×"), LOCTEXT("Junk", "Marked as Junk"), Style.JunkIconSize)]
        ]
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(Style.Spacing)
        [
            SAssignNew(FavoriteIndicator, STextBlock)
            .Tag(TEXT("GearFavoriteIndicator"))
            .Text(LOCTEXT("FavoriteGlyph", "★")).Font(Style.LabelFont).ColorAndOpacity(Style.TextTint)
            .ToolTipText(LOCTEXT("Favorite", "Favorite"))
            .Visibility(bFavorite ? EVisibility::Visible : EVisibility::Collapsed)
        ];
}

TSharedRef<SWidget> UCIGearRow::BuildInventoryContent()
{
    SAssignNew(Title, SHorizontalBox);
    SAssignNew(Primary, SHorizontalBox);
    SAssignNew(Indicators, SHorizontalBox);
    RefreshDetails();
    return BuildRow(BuildThumbnail(), Title.ToSharedRef(), Primary.ToSharedRef(), Indicators.ToSharedRef());
}

void UCIGearRow::ReleaseSlateResources(bool bReleaseChildren)
{
    Title.Reset();
    Primary.Reset();
    Indicators.Reset();
    FavoriteIndicator.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

#undef LOCTEXT_NAMESPACE
