#pragma once

#include "Model/CFObject.h"
#include "Presentation/CFMagritteBinding.h"

class UCIInventoryView;

/** A game profile's Inventory as a retained, read-only Magritte object. */
class CATALYSTINVENTORY_API FCIInventoryModel final : public FCFIdentifiableObject
{
public:
    explicit FCIInventoryModel(FCFMagritteBinding InBinding, TSubclassOf<UCIInventoryView> InViewClass = {});

    FCFViewDescription MagritteDescription() const override;

private:
    friend class FCIInventoryBuilder;
    FCFMagritteBinding Binding;
    TSubclassOf<UCIInventoryView> ViewClass;
};
