#pragma once

#include "CIInventoryTile.h"
#include "CIResourceTile.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIResourceTileTheme : public UCIInventoryTileTheme
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource Tile")
    bool bShowLabel = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Resource Tile")
    TObjectPtr<UTexture> FallbackIcon;
};

/** A resource stack with optional name, presentation art and quantity. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIResourceTile : public UCIInventoryTile
{
    GENERATED_BODY()

public:
    virtual void BindItem(const FCFMagritteBinding& Item) override;
    virtual const UCFTileTheme& GetTileTheme() const override;

protected:
    virtual UTexture* GetTileIcon() const override;
    virtual FText TileLabel() const override;
};
