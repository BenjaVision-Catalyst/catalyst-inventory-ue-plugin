#include "CIApparelTile.h"

#include "Engine/Texture.h"
#include "Presentation/CFMagritteBinding.h"

void UCIApparelTile::BindItem(const FCFMagritteBinding& Item)
{
    const FString PreviousEquipped = EquippedDefinitionIdentifier;
    Super::BindItem(Item);
    CategoryIdentifier.Reset();
    EquippedDefinitionIdentifier.Reset();
    InventoryTypeCount = 0;
    bShowCount = false;
    for (const auto& Field : Item.Describe().orderedChildren())
    {
        if (Field->id == "categoryIdentifier" || Field->id == "equippedDefinitionIdentifier")
        {
            const auto Value = Item.Read(*Field);
            if (const auto* Id = std::get_if<std::string>(&Value.data))
            {
                FString& Target = Field->id == "categoryIdentifier" ? CategoryIdentifier : EquippedDefinitionIdentifier;
                Target = UTF8_TO_TCHAR(Id->c_str());
            }
        }
        else if (Field->id == "inventoryTypeCount")
        {
            const auto Value = Item.Read(*Field);
            if (const auto* Quantity = std::get_if<std::int64_t>(&Value.data); Quantity && *Quantity >= 0)
            {
                InventoryTypeCount = *Quantity;
            }
        }
    }
    if (PreviousEquipped != EquippedDefinitionIdentifier)
    {
        Icon = nullptr;
    }
    SetVisibility(CategoryIdentifier.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
    InvalidateLayoutAndVolatility();
}

const UCFTileTheme& UCIApparelTile::GetTileTheme() const
{
    return Theme ? Super::GetTileTheme() : *GetDefault<UCIApparelTileTheme>();
}

FText UCIApparelTile::TileLabel() const
{
    return CategoryIdentifier.IsEmpty() ? FText::GetEmpty() : FText::AsNumber(InventoryTypeCount);
}

UTexture* UCIApparelTile::GetTileIcon() const
{
    if (!EquippedDefinitionIdentifier.IsEmpty() && Icon)
    {
        return Icon;
    }

    const auto* Style = Cast<UCIApparelTileTheme>(&GetTileTheme());
    if (Style)
    {
        if (UTexture* CategoryIcon = Style->CategoryIcons.FindRef(CategoryIdentifier))
        {
            return CategoryIcon;
        }
        return Style->FallbackCategoryIcon;
    }
    return nullptr;
}
