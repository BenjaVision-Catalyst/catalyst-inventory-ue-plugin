#pragma once

#include "CIEquipmentRow.h"
#include "CIWeaponToolRow.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIWeaponToolRowTheme : public UCIEquipmentRowTheme
{
    GENERATED_BODY()

public:
    UCIWeaponToolRowTheme();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row", meta = (ClampMin = "0.0"))
    float ModInsetFromTierBand = 9.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float ConsumableSize = 32.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float StatWidth = 112.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FLinearColor HigherValue = FLinearColor(.4f, .85f, .25f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FLinearColor LowerValue = FLinearColor(1.f, .21f, .13f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    TMap<FString, TObjectPtr<UTexture>> ConsumableIcons;

};

/** List presentation for tools and weapons. The list supplies the current comparison item. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIWeaponToolRow : public UCIEquipmentRow
{
    GENERATED_BODY()

public:
    virtual void BindItem(const FCFMagritteBinding& Item) override;
    void SetComparisonItem(const TOptional<FCFMagritteBinding>& Item);
    virtual const UCFTileTheme& GetTileTheme() const override;

protected:
    virtual float GetMarkerLeftInset() const override;
    virtual float GetHeaderHeight() const override;
    virtual TSharedRef<SWidget> BuildInventoryContent() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    struct FStat
    {
        FString Id;
        FText Label;
        TOptional<double> Value;
        FText Display;
    };

    static TArray<FStat> ReadStats(const FCFMagritteBinding& Item);
    const UCIWeaponToolRowTheme& GetWeaponToolTheme() const;
    void RefreshDetails();
    void RefreshStats();

    TSharedPtr<class SHorizontalBox> Title, StatWidgets, Indicators;
    TArray<FStat> Stats, ComparisonStats;
    FString Consumable;
    FString ConsumableIcon;
    FText Description;
};
