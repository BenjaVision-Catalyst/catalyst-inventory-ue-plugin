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
    BaseArmour = CIInventoryRowData::FormatArmour(CIInventoryRowData::ReadValue(Item, "inspectorArmour"));
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
            Result.Id = CIInventoryRowData::Read(*PrimaryBinding, "attributeIdentifier").ToString();
            Result.Type = CIInventoryRowData::Read(*PrimaryBinding, "attributeType").ToString();
            if (!Result.Id.IsEmpty() && !Result.Type.IsEmpty())
            {
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
                    Result.Id = ReadIdentifier(Field->property("label"));
                    Result.Type = ReadIdentifier(Field->property("metadata:ciAttributeType"));
                    if (!Result.Id.IsEmpty() && !Result.Type.IsEmpty()
                        && !CIInventoryRowData::DisplayValue(PrimaryCard->Read(*Field)).IsEmpty())
                    {
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
    const FText ArmourValue = BaseArmour.IsEmpty() ? LOCTEXT("MissingArmour", "—") : BaseArmour;
    Primary->AddSlot().AutoWidth().VAlign(VAlign_Center)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, -2.f)
        [SNew(STextBlock).Text(LOCTEXT("ArmourAbbreviation", "ARM")).Font(Style.StatLabelFont)
            .ColorAndOpacity(Style.TextTint).ToolTipText(LOCTEXT("BaseArmour", "Base armour"))]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
        [SNew(STextBlock).Text(ArmourValue).Font(Style.CountFont).ColorAndOpacity(Style.TextTint)]
    ];
    PopulateRowIndicators(*Indicators, Attribute ? Attribute->Id : FString(),
        Attribute ? FStringView(Attribute->Type) : FStringView());
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
