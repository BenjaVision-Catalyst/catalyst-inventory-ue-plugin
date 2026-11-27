#pragma once

#include "CIInventoryTile.h"

#include "CIInspectorIdentityTile.generated.h"

/** A pinned item summary using the same frame and tier band as other Inventory tiles. */
UCLASS()
class CATALYSTINVENTORY_API UCIInspectorIdentityTile : public UCIInventoryTile
{
    GENERATED_BODY()

public:
    void InitializeFrom(const UCIInventoryTile& Source);

protected:
    virtual TSharedRef<SWidget> BuildInventoryContent() override;
};
