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
    FLinearColor Utility = FLinearColor(1.f, .84f, .015f);

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

protected:
    virtual TSharedRef<SWidget> BuildInventoryContent() override;
    virtual FText TileLabel() const override { return PresentationKind.IsEmpty() ? Label : FText::GetEmpty(); }
    virtual bool ShowsTypeCount() const override { return !PresentationKind.IsEmpty() && !IsRowPresentation(); }
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

    const UCIEquipmentTileTheme& GetEquipmentTheme() const;
    TArray<FString> OffensiveAttributes, DefensiveAttributes, UtilityAttributes;

    virtual bool IsRowPresentation() const { return false; }
    virtual float GetMarkerLeftInset() const { return GetInventoryTheme().Spacing; }
    void PopulateAttributes(SHorizontalBox& Box, const TArray<FString>& MaximumAttributes,
        const FString& ExcludedAttribute = FString());
    void AddAttributeMarker(SHorizontalBox& Box, const FString& Id, FStringView Type,
        bool bMaximum = false, float Size = 0.f);
    TSharedRef<SWidget> BuildTalent();
    virtual TSharedRef<SWidget> BuildExpertise();
    TOptional<int64> ExpertiseLevel;

private:
    void RefreshMarkers();

    TSharedPtr<class SHorizontalBox> Markers;
    TArray<TPair<FString, FString>> ModSlots;

    FString BrandId, TalentId;
    FSlateBrush BrandBrush, TalentBrush;
};
