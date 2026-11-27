#pragma once

#include "CIEquipmentRow.h"
#include "CIGearRow.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIGearRowTheme : public UCIEquipmentRowTheme
{
    GENERATED_BODY()

public:
    UCIGearRowTheme();

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Row")
    TMap<FString, TObjectPtr<UTexture>> GearSlotIcons;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Row")
    TObjectPtr<UTexture> JunkIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Row")
    float JunkIconSize = 44.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Row")
    float PrimaryMarkerSize = 44.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gear Row")
    FLinearColor PrimaryValueTint = FLinearColor(1.f, .57f, .21f);
};

/** Gear list entry with a brand, primary attribute and player-specific item state. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIGearRow : public UCIEquipmentRow
{
    GENERATED_BODY()

public:
    virtual void BindItem(const FCFMagritteBinding& Item) override;
    virtual const UCFTileTheme& GetTileTheme() const override;

protected:
    virtual bool ShowsItemIcon() const override { return !bJunk; }
    virtual TSharedRef<SWidget> BuildInventoryContent() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
    const UCIGearRowTheme& GetGearTheme() const;
    void RefreshDetails();
    TSharedRef<SWidget> BuildThumbnail();

    struct FPrimaryAttribute
    {
        FString Id;
        FText Label, Value;
        FString Type;
        bool bMaximum = false;
    };

    TSharedPtr<SHorizontalBox> Title, Primary, Indicators;
    TSharedPtr<class STextBlock> FavoriteIndicator;
    TOptional<FPrimaryAttribute> Attribute;
    FText Manufacturer;
    FText Armour;
    bool bJunk = false;
    bool bFavorite = false;
};
