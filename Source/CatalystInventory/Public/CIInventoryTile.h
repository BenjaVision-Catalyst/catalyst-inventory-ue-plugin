#pragma once

#include "Presentation/CFTile.h"

#include "CIInventoryTile.generated.h"

class FCFMagritteBinding;
class UTexture2D;

/** A themed tile bound to a described Inventory item. Icons are resolved locally. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIInventoryTile : public UCFTile
{
    GENERATED_BODY()

public:
    UCIInventoryTile();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile", meta = (ClampMin = "1", ClampMax = "6"))
    int32 TierId = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    TObjectPtr<UTexture2D> Icon;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    FText Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    bool bShowCount = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile", meta = (EditCondition = "bShowCount"))
    int64 Count = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    TMap<int32, FLinearColor> TierColors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile", meta = (ClampMin = "0.0"))
    float TierBandWidth = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    FLinearColor TextTint = FLinearColor(0.9f, 0.96f, 0.92f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    FLinearColor IconTint = FLinearColor::White;

    void BindItem(const FCFMagritteBinding& Item);

protected:
    virtual TSharedRef<SWidget> BuildTileContent() override;

private:
    FSlateBrush IconBrush;
};
