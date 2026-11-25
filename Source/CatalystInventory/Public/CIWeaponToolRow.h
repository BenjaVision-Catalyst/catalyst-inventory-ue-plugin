#pragma once

#include "CIEquipmentTile.h"
#include "Presentation/CFMagritteBinding.h"
#include "CIWeaponToolRow.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIWeaponToolRowTheme : public UCIEquipmentTileTheme
{
    GENERATED_BODY()

public:
    UCIWeaponToolRowTheme();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float ThumbnailWidth = 224.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float RowHeight = 126.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float HeaderHeight = 54.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FMargin ThumbnailInset = FMargin(20.f, 8.f, 20.f, 26.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float ContentInset = 12.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row", meta = (ClampMin = "0.0"))
    float ModInsetFromTierBand = 9.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float ConsumableSize = 32.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float StatWidth = 112.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float TalentAreaWidth = 68.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    float DividerThickness = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FLinearColor DividerTint = FLinearColor(1.f, 1.f, 1.f, .12f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FSlateFontInfo StatLabelFont;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FSlateFontInfo LevelFont;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment Row")
    FSlateFontInfo BadgeFont;

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
    virtual const UCFTileTheme& GetTileTheme() const override;

protected:
    virtual bool IsRowPresentation() const override { return true; }
    virtual FMargin GetIconPadding() const override;
    virtual float GetMarkerLeftInset() const override;
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
    TSharedRef<SWidget> MakeIcon(UTexture* Texture, const FText& Fallback, const FText& Tooltip, float Size);
    TSharedRef<SWidget> BuildRequiredLevel();

    TSharedPtr<class SHorizontalBox> Title, StatWidgets, Indicators;
    TArray<FStat> Stats, ComparisonStats;
    TArray<FString> MaximumAttributes, Talents;
    FString Consumable;
    TOptional<int64> RequiredLevel;
    bool bProficient = false;
};
