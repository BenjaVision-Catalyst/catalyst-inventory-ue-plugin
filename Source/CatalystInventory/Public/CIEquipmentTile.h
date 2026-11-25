#pragma once

#include "CIInventoryTile.h"

#include "CIEquipmentTile.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIEquipmentTileTheme : public UCIInventoryTileTheme
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    FVector2D EquipmentSize = FVector2D(216, 100);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    float MarkerSize = 23.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    float MarkerGap = 4.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    float GlyphMarkerWidth = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile", meta = (ClampMin = "0.0"))
    float AttributeGlyphMarkerWidth = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    float MaximumMarkerInset = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile", meta = (ClampMin = "1"))
    int32 MaximumMarkerFontSize = 10;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    float BrandWidth = 76.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    float BadgeSize = 24.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    FLinearColor FilledMod = FLinearColor(1.f, .34f, .06f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    FLinearColor EmptyMod = FLinearColor(.54f, .54f, .54f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    FLinearColor Offensive = FLinearColor(.95f, .11f, .1f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    FLinearColor Defensive = FLinearColor(.13f, .5f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    FLinearColor Utility = FLinearColor(1.f, .72f, .04f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    FLinearColor BrandTint = FLinearColor(1.f, 1.f, 1.f, .18f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    TMap<FString, TObjectPtr<UTexture>> BrandIcons;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    TMap<FString, TObjectPtr<UTexture>> TalentIcons;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Tile")
    TMap<FString, TObjectPtr<UTexture>> AttributeIcons;
};

/** An individual tool, weapon, gear item or gadget and its Inventory context. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIEquipmentTile : public UCIInventoryTile
{
    GENERATED_BODY()

public:
    virtual void BindItem(const FCFMagritteBinding& Item) override;

    UPROPERTY(BlueprintReadOnly, Category = "Equipment Tile")
    FString PresentationKind;

    UPROPERTY(BlueprintReadOnly, Category = "Equipment Tile")
    int64 InventoryTypeCount = 0;

protected:
    virtual TSharedRef<SWidget> BuildInventoryContent() override;
    virtual FText TileLabel() const override { return PresentationKind.IsEmpty() ? Label : FText::GetEmpty(); }
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    const UCIEquipmentTileTheme& GetEquipmentTheme() const;
    virtual bool IsRowPresentation() const { return false; }
    virtual float GetMarkerLeftInset() const { return GetInventoryTheme().Spacing; }
    void PopulateAttributes(SHorizontalBox& Box, const TArray<FString>& MaximumAttributes);
    TSharedRef<SWidget> BuildTalent();
    virtual TSharedRef<SWidget> BuildExpertise();
    TOptional<int64> ExpertiseLevel;

private:
    void AddMarker(SHorizontalBox& Box, const FString& Id, const TCHAR* Glyph,
        FLinearColor Tint, bool bAttribute, bool bMaximum = false);
    void RefreshMarkers();

    TSharedPtr<class SHorizontalBox> Markers;
    TArray<TPair<FString, FString>> ModSlots;
    TArray<FString> OffensiveAttributes, DefensiveAttributes, UtilityAttributes;
    FString BrandId, TalentId;
    bool bHasTypeCount = false;
    FSlateBrush BrandBrush, TalentBrush;
};
