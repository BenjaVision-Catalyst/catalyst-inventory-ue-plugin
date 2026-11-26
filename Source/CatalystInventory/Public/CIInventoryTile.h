#pragma once

#include "Presentation/CFTile.h"

#include "CIInventoryTile.generated.h"

class FCFMagritteBinding;
class UTexture;

/** Shared tier-band geometry for compact tiles and rows. */
UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIInventoryTierStyle : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tier", meta = (ClampMin = "0.0"))
    float BandWidth = 4.f;
};

/** Editable frame, tier and content metrics shared across Inventory tiles. */
UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIInventoryTileTheme : public UCFTileTheme
{
    GENERATED_BODY()

public:
    UCIInventoryTileTheme();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    TMap<int32, FLinearColor> TierColors;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    TObjectPtr<UCIInventoryTierStyle> TierStyle;

    const UCIInventoryTierStyle& GetTierStyle() const;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile", meta = (ClampMin = "0.0"))
    float Spacing = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    FVector2D MinimumSize = FVector2D(72.f, 72.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    FLinearColor TextTint = FLinearColor(0.9f, 0.96f, 0.92f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    FLinearColor IconTint = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    FSlateFontInfo LabelFont;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    FSlateFontInfo CountFont;
};

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
    bool bShowTier = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    TObjectPtr<UTexture> Icon;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    FText Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile")
    bool bShowCount = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Tile", meta = (EditCondition = "bShowCount"))
    int64 Count = 0;

    virtual const UCFTileTheme& GetTileTheme() const override;

    virtual void BindItem(const FCFMagritteBinding& Item);
    virtual const FString& GetArtDefinitionIdentifier() const { return DefinitionIdentifier; }

protected:
    virtual TSharedRef<SWidget> BuildTileContent() override final;
    virtual TSharedRef<SWidget> BuildInventoryContent();

    TSharedRef<SWidget> BuildTileIcon();
    TSharedRef<SWidget> BuildTileLabel();
    bool HasTierBand() const { return GetVisibleTierBandWidth() > 0.f; }
    float GetVisibleTierBandWidth() const;

    const UCIInventoryTileTheme& GetInventoryTheme() const;
    virtual UTexture* GetTileIcon() const { return Icon; }
    virtual bool ShowsItemIcon() const { return true; }
    virtual FMargin GetIconPadding() const { return FMargin(0.f); }
    virtual FText TileLabel() const { return Label; }
    virtual const FSlateFontInfo& TileLabelFont() const { return GetInventoryTheme().LabelFont; }

private:
    FString DefinitionIdentifier;
    FSlateBrush IconBrush;
};
