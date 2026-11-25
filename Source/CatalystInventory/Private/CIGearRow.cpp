#include "CIGearRow.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
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
    HeaderHeight = 60.f;
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
    BrandName.Reset();
    GearSlot.Reset();
    Attribute.Reset();
    bJunk = false;
    bFavorite = false;
    if (bGear)
    {
        for (const auto& Field : Item.Describe().orderedChildren())
        {
            if (Field->id == "brandName") { BrandName = ReadIdentifier(Item.Read(*Field)); }
            else if (Field->id == "gearSlotIdentifier") { GearSlot = ReadIdentifier(Item.Read(*Field)); }
            else if (Field->id == "isJunk") { bJunk = ReadFlag(Item.Read(*Field)); }
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
    Title->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, Style.MarkerGap, 0.f)
    [
        SNew(SBox).WidthOverride(Style.PrimaryMarkerSize).HAlign(HAlign_Center)
        [
            GearSlot.IsEmpty() ? SNullWidget::NullWidget
                : MakeIcon(Style.GearSlotIcons.FindRef(GearSlot), FText::FromString(TEXT("◇")),
                    FText::FromString(GearSlot), Style.BadgeSize)
        ]
    ];
    Title->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(Label).Font(Style.LabelFont).ColorAndOpacity(Style.TextTint)
            .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Label)]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Text(FText::FromString(BrandName)).Font(Style.CountFont).ColorAndOpacity(Style.TextTint)
            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
            .Visibility(BrandName.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)]
    ];

    Primary->ClearChildren();
    if (Attribute)
    {
        const auto& MarkerStyle = GetEquipmentTheme();
        const TCHAR* Glyph = TEXT("◆");
        FLinearColor Tint = Style.TextTint;
        if (Attribute->Type == TEXT("offensive"))
        {
            Glyph = TEXT("●");
            Tint = MarkerStyle.Offensive;
        }
        else if (Attribute->Type == TEXT("defensive"))
        {
            Tint = MarkerStyle.Defensive;
        }
        else if (Attribute->Type == TEXT("utility"))
        {
            Glyph = TEXT("▲");
            Tint = MarkerStyle.Utility;
        }
        AddMarker(*Primary, Attribute->Id, Glyph, Tint, true, Attribute->bMaximum, Style.PrimaryMarkerSize);
        Primary->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(Attribute->Label).Font(Style.CountFont).ColorAndOpacity(Style.TextTint)
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Attribute->Label)]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(Attribute->Value).Font(Style.CountFont).ColorAndOpacity(Style.PrimaryValueTint)
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Attribute->Value)]
        ];
    }
    PopulateRowIndicators(*Indicators, Attribute ? Attribute->Id : FString());
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
            SNew(STextBlock).Text(LOCTEXT("FavoriteGlyph", "★")).Font(Style.LabelFont).ColorAndOpacity(Style.TextTint)
            .ToolTipText(LOCTEXT("Favorite", "Favorite"))
            .Visibility_Lambda([this] { return bFavorite ? EVisibility::Visible : EVisibility::Collapsed; })
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
    Super::ReleaseSlateResources(bReleaseChildren);
}

#undef LOCTEXT_NAMESPACE
