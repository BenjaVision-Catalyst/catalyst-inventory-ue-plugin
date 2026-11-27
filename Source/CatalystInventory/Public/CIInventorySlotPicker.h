#pragma once

#include "CoreMinimal.h"
#include "Presentation/CFMagritteBinding.h"

class UTexture;

/** The host supplies owned, compatible items for the selected slot and authorizes the equip. */
struct FCIInventorySlotChoice
{
    FString Identifier;
    FText Label;
    bool bEquipped = false;
    TOptional<FCFMagritteBinding> Item{};
    TWeakObjectPtr<UTexture> Icon{};
    FName FallbackIconIdentifier;
};

struct FCIInventorySlotPicker
{
    FText Heading;
    TArray<FCIInventorySlotChoice> Choices;
    TFunction<bool(const FString&)> Equip;
};

using FCIInventorySlotPickerSource = TFunction<TOptional<FCIInventorySlotPicker>(const FCFMagritteBinding&)>;
