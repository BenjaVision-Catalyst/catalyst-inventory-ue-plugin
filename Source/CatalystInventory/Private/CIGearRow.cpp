#include "CIGearRow.h"
#include "CIInventoryRowData.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
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
    Manufacturer = CIInventoryRowData::Manufacturer(Item);
    Armour = CIInventoryRowData::Read(Item, "inspectorArmour");
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
                        Result.Id = UTF8_TO_TCHAR(Field->id.c_str());
                        Result.Type = ReadIdentifier(Field->property("metadata:ciAttributeType"));
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
    FText Subtitle = Manufacturer;
    if (!Armour.IsEmpty())
    {
        const FText Contribution = LOCTEXT("ArmourContribution", "{0} Armour");
        Subtitle = Manufacturer.IsEmpty() ? FText::Format(Contribution, Armour)
            : FText::Format(LOCTEXT("ManufacturerAndArmour", "{0} · {1}"), Manufacturer,
                FText::Format(Contribution, Armour));
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

    Primary->ClearChildren();
    if (Attribute)
    {
        const auto& MarkerStyle = GetEquipmentTheme();

        FLinearColor Tint = Style.TextTint;
        if (Attribute->Type == TEXT("offensive"))
        {
            Tint = MarkerStyle.Offensive;
        }
        else if (Attribute->Type == TEXT("defensive"))
        {
            Tint = MarkerStyle.Defensive;
        }
        else if (Attribute->Type == TEXT("utility"))
        {
            Tint = MarkerStyle.Utility;
        }
        if (Tint != Style.TextTint)
        {
            AddMarker(*Primary, Attribute->Id, TEXT("●"), (Tint * .6f).CopyWithNewOpacity(.65f),
                true, Attribute->bMaximum, 15.f);
        }
        Primary->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(Attribute->Label).Font(Style.CountFont).ColorAndOpacity(Style.TextTint)
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).ToolTipText(Attribute->Label)]
            + SVerticalBox::Slot().AutoHeight()
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
