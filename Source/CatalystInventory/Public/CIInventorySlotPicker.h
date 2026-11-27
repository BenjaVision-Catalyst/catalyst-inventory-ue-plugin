#pragma once

#include "CoreMinimal.h"

class FCFMagritteBinding;

/** The host supplies owned, compatible items for the selected slot and authorizes the equip. */
struct FCIInventorySlotChoice
{
    FString Identifier;
    FText Label;
    bool bEquipped = false;
};

struct FCIInventorySlotPicker
{
    FText Heading;
    TArray<FCIInventorySlotChoice> Choices;
    TFunction<bool(const FString&)> Equip;
};

using FCIInventorySlotPickerSource = TFunction<TOptional<FCIInventorySlotPicker>(const FCFMagritteBinding&)>;
