#pragma once

#include "CIInventoryTile.h"
#include "CIApparelTile.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIApparelTileTheme : public UCIInventoryTileTheme
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apparel Tile")
    TMap<FString, TObjectPtr<UTexture>> CategoryIcons;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Apparel Tile")
    TObjectPtr<UTexture> FallbackCategoryIcon;
};

/** One apparel category with an optional equipped item and a count of owned items. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIApparelTile : public UCIInventoryTile
{
    GENERATED_BODY()

public:
    virtual void BindItem(const FCFMagritteBinding& Item) override;
    virtual const FString& GetArtDefinitionIdentifier() const override { return EquippedDefinitionIdentifier; }
    virtual const UCFTileTheme& GetTileTheme() const override;

    UPROPERTY(BlueprintReadOnly, Category = "Apparel Tile")
    FString CategoryIdentifier;

    UPROPERTY(BlueprintReadOnly, Category = "Apparel Tile")
    FString EquippedDefinitionIdentifier;

    UPROPERTY(BlueprintReadOnly, Category = "Apparel Tile")
    int64 InventoryTypeCount = 0;

protected:
    virtual UTexture* GetTileIcon() const override;
    virtual FText TileLabel() const override;
    virtual const FSlateFontInfo& TileLabelFont() const override { return GetInventoryTheme().CountFont; }
};
