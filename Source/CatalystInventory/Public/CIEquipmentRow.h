#pragma once

#include "CIEquipmentTile.h"
#include "Presentation/CFMagritteBinding.h"
#include "CIEquipmentRow.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIEquipmentRowTheme : public UCIEquipmentTileTheme
{
    GENERATED_BODY()

public:
    UCIEquipmentRowTheme();

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
    TObjectPtr<UTexture> ProficiencyIcon;
};

UCLASS(Abstract)
class CATALYSTINVENTORY_API UCIEquipmentRow : public UCIEquipmentTile
{
    GENERATED_BODY()

public:
    virtual void BindItem(const FCFMagritteBinding& Item) override;
    virtual const UCFTileTheme& GetTileTheme() const override;

protected:
    virtual bool IsRowPresentation() const override { return true; }
    virtual FText TileLabel() const override { return FText::GetEmpty(); }
    virtual FMargin GetIconPadding() const override;
    virtual TSharedRef<SWidget> BuildExpertise() override;
    const UCIEquipmentRowTheme& GetRowTheme() const;
    TSharedRef<SWidget> BuildRow(const TSharedRef<SWidget>& Thumbnail,
        const TSharedRef<SWidget>& Title, const TSharedRef<SWidget>& Details,
        const TSharedRef<SWidget>& Indicators);
    void PopulateRowIndicators(SHorizontalBox& Box, const FString& ExcludedAttribute = FString());
    TSharedRef<SWidget> MakeIcon(UTexture* Texture, const FText& Fallback, const FText& Tooltip, float Size);
    static FString ReadIdentifier(const magritte::Value& Value);

private:
    TSharedRef<SWidget> BuildRequiredLevel();
    TArray<FString> MaximumAttributes, Talents;
    TOptional<int64> RequiredLevel;
    bool bProficient = false;
};
