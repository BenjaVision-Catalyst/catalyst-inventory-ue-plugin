#pragma once

#include "CIEquipmentRow.h"
#include "CIApparelRow.generated.h"

UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIApparelRowTheme : public UCIEquipmentRowTheme
{
    GENERATED_BODY()

public:
    UCIApparelRowTheme();
};

/** An individual apparel item with its preview, name and optional brand. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIApparelRow : public UCIInventoryTile
{
    GENERATED_BODY()

public:
    virtual void BindItem(const FCFMagritteBinding& Item) override;
    virtual const UCFTileTheme& GetTileTheme() const override;

protected:
    virtual TSharedRef<SWidget> BuildInventoryContent() override;

private:
    const UCIEquipmentRowTheme& GetRowTheme() const;

    FText Manufacturer;
};
