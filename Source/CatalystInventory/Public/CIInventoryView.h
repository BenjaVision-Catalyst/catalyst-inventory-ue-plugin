#pragma once

#include "Blueprint/UserWidget.h"

#include "CIInventoryView.generated.h"

/** Base for independently authored Catalyst Inventory widget compositions. */
UCLASS(Abstract, Blueprintable, BlueprintType)
class CATALYSTINVENTORY_API UCIInventoryView : public UUserWidget
{
    GENERATED_BODY()
};
