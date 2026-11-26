#pragma once

#include "Presentation/CFTile.h"
#include "Presentation/CFMagritteBinding.h"

#include "CIInventoryTile.generated.h"

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

USTRUCT(BlueprintType)
struct CATALYSTINVENTORY_API FCIInventoryScrollbarStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0.5"))
    float Width = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
    FLinearColor TrackTint = FLinearColor(1.f, 1.f, 1.f, .15f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
    FLinearColor ThumbTint = FLinearColor(1.f, 1.f, 1.f, .45f);
};

/** Editable frame, tier and content metrics shared across Inventory tiles. */
UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIInventoryTileTheme : public UCFTileTheme
{
    GENERATED_BODY()

public:
    UCIInventoryTileTheme();

    // The projected shell composites scene blur separately from the offscreen tile surface.
    UCIInventoryTileTheme* CreateProjectedSurfaceTheme(UObject* Outer) const;

    /** Space below the menu on the shared Inventory plane. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Layout", meta = (ClampMin = "0.0"))
    float MenuPanelGap = 16.f;

    /** Height of the shared section panel in unscaled Inventory layout units. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Layout", meta = (ClampMin = "1.0"))
    float PanelHeight = 481.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Layout")
    FSlateFontInfo InspectorTitleFont;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Layout", meta = (ClampMin = "0.0"))
    float InspectorPadding = 14.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Layout")
    FCIInventoryScrollbarStyle Scrollbar;

    // Grid dimensions use the same unscaled units as the Inventory scaffold.
    /** Horizontal distance between tiles in viewport pixels. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid", meta = (ClampMin = "0.0"))
    float TileGap = 8.f;

    /** Vertical distance between rows in unscaled Inventory layout units. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid", meta = (ClampMin = "0.0"))
    float RowGap = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid", meta = (ClampMin = "1.0"))
    float EquipmentTileHeight = 76.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid", meta = (ClampMin = "1.0"))
    float ResourceTileHeight = 94.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid")
    FSlateFontInfo GroupLabelFont;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid")
    FLinearColor GroupLabelTint = FLinearColor(0.9f, 0.96f, 0.92f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid", meta = (ClampMin = "1.0"))
    float GroupLabelHeight = 21.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid")
    FMargin GroupLabelPadding = FMargin(0.f, 0.f, 0.f, 4.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory Grid", meta = (ClampMin = "0.0"))
    float GroupGap = 4.f;

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

    /** Size of the square centered icon box in tile layout units. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile", meta = (ClampMin = "1.0"))
    float IconSize = 32.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    FSlateFontInfo LabelFont;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tile")
    FSlateFontInfo CountFont;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Empty Slots")
    TMap<FName, TObjectPtr<UTexture>> SlotIcons;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Empty Slots")
    FSlateFontInfo SlotIconFont;

    /** Glyph code points in the slot icon font, indexed by slot or category ID. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Empty Slots")
    TMap<FName, int32> SlotIconGlyphs;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Empty Slots")
    FLinearColor EmptySlotIconTint = FLinearColor(.7f, .76f, .8f);

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

    UPROPERTY(BlueprintReadOnly, Category = "Inventory Tile")
    int64 InventoryTypeCount = 0;

    // The authored span stays available after the grid projects its Slate slot widths.
    int32 GridColumnSpan = 1;

    virtual const UCFTileTheme& GetTileTheme() const override;

    virtual void BindItem(const FCFMagritteBinding& Item);
    UTexture* GetInspectorIcon() const { return ResolveTileIcon(); }
    const TOptional<FCFMagritteBinding>& GetInspectorItem() const { return InspectorItem; }
    const FText& GetItemLabel() const { return ItemLabel.IsEmpty() ? Label : ItemLabel; }
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
    virtual FMargin GetIconPadding() const { return FMargin(0.f, 0.f, GetVisibleTierBandWidth(), 0.f); }
    virtual bool ShowsTypeCount() const { return true; }
    virtual FText TileLabel() const { return Label; }
    virtual const FSlateFontInfo& TileLabelFont() const { return GetInventoryTheme().LabelFont; }

private:
    UTexture* ResolveTileIcon() const;
    TOptional<FCFMagritteBinding> InspectorItem;
    FText ItemLabel;
    FName EmptySlotIdentifier;
    FString DefinitionIdentifier;
    int64 InventoryStorageCapacity = 0;
    FSlateBrush IconBrush;
};
