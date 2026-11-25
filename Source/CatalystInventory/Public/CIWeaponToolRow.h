#pragma once

#include "CIEquipmentTile.h"
#include "Presentation/CFMagritteBinding.h"
#include "CIWeaponToolRow.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIWeaponToolRowTheme : public UCIEquipmentTileTheme
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float ThumbnailWidth = 164.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float RowHeight = 90.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float ColumnSpacing = 12.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FLinearColor HigherValue = FLinearColor(.4f, .85f, .25f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FLinearColor LowerValue = FLinearColor(1.f, .21f, .13f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    TMap<FString, TObjectPtr<UTexture>> ConsumableIcons;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    TObjectPtr<UTexture> ProficiencyIcon;
};

/** List presentation for tools and weapons. The list supplies the current comparison item. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIWeaponToolRow : public UCIEquipmentTile
{
    GENERATED_BODY()

public:
    virtual void BindItem(const FCFMagritteBinding& Item) override;
    void SetComparisonItem(const TOptional<FCFMagritteBinding>& Item);

protected:
    virtual bool IsRowPresentation() const override { return true; }
    virtual TSharedRef<SWidget> BuildExpertise() override;
    virtual TSharedRef<SWidget> BuildTileContent() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    struct FStat
    {
        FString Id;
        FText Label;
        double Value;
    };

    static TArray<FStat> ReadStats(const FCFMagritteBinding& Item);
    const UCIWeaponToolRowTheme& GetRowTheme() const;
    void RefreshDetails();
    void RefreshStats();
    TSharedRef<SWidget> MakeIcon(UTexture* Texture, const FText& Fallback, const FText& Tooltip);

    TSharedPtr<class SHorizontalBox> Title, StatWidgets, Indicators;
    TArray<FStat> Stats, ComparisonStats;
    TArray<FString> MaximumAttributes, Talents;
    FString Consumable;
    TOptional<int64> RequiredLevel;
    bool bProficient = false;
};
