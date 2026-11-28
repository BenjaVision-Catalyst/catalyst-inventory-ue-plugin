#include "CIResourceTile.h"
#include "CIInventoryRowData.h"

#include "Engine/Texture.h"

void UCIResourceTile::BindItem(const FCFMagritteBinding& Item)
{
    const FString PreviousDefinition = GetArtDefinitionIdentifier();
    Super::BindItem(Item);
    const FString Category = CIInventoryRowData::Read(Item, "resourceCategory").ToString();
    FallbackIconIdentifier = Category.IsEmpty() ? FName(TEXT("Resources")) : FName(*Category);
    bShowTier = false;
    bShowCount = bShowCount && Count > 0;
    if (PreviousDefinition != GetArtDefinitionIdentifier())
    {
        Icon = nullptr;
    }
    SetVisibility(bShowCount ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    InvalidateLayoutAndVolatility();
}

const UCFTileTheme& UCIResourceTile::GetTileTheme() const
{
    return Theme ? Super::GetTileTheme() : *GetDefault<UCIResourceTileTheme>();
}

UTexture* UCIResourceTile::GetTileIcon() const
{
    if (Icon)
    {
        return Icon;
    }
    const auto* Style = Cast<UCIResourceTileTheme>(&GetTileTheme());
    return Style ? Style->FallbackIcon.Get() : nullptr;
}

FText UCIResourceTile::TileLabel() const
{
    const auto* Style = Cast<UCIResourceTileTheme>(&GetTileTheme());
    return Style && !Style->bShowLabel ? FText::GetEmpty() : Super::TileLabel();
}
