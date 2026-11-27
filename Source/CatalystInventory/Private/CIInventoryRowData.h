#pragma once

#include "CIInventoryTile.h"

namespace CIInventoryRowData
{
inline FText DisplayValue(const magritte::Value& Value)
{
    if (const auto* Text = std::get_if<std::string>(&Value.data))
    {
        return FText::FromString(UTF8_TO_TCHAR(Text->c_str()));
    }
    if (const auto* Number = std::get_if<std::int64_t>(&Value.data))
    {
        return FText::AsNumber(*Number);
    }
    if (const auto* Number = std::get_if<double>(&Value.data))
    {
        return FText::AsNumber(*Number);
    }
    return FText::GetEmpty();
}

inline FText NormalizeLabel(const FText& Label)
{
    const FString Name = Label.ToString();
    // Older profiles still send these labels for the same stat.
    if (Name == TEXT("Utility Tier") || Name == TEXT("Current tier"))
    {
        return NSLOCTEXT("CatalystInventory", "UtilityLabel", "Utility");
    }
    return Label;
}

inline FText Read(const FCFMagritteBinding& Item, std::string_view Id)
{
    for (const auto& Field : Item.Describe().orderedChildren())
    {
        if (Field->id == Id)
        {
            return DisplayValue(Item.Read(*Field));
        }
    }
    return FText::GetEmpty();
}

inline FText Manufacturer(const FCFMagritteBinding& Item)
{
    const auto Name = Read(Item, "inspectorManufacturer");
    return Name.IsEmpty() ? Read(Item, "brandName") : Name;
}

inline TOptional<FCFMagritteBinding> Card(const FCFMagritteBinding& Item, std::string_view Kind)
{
    if (const auto Inspector = Item.Follow("inspector"))
    {
        for (const auto& Field : Inspector->Describe().orderedChildren())
        {
            const auto Metadata = Field->property("metadata:ciCard");
            const auto* Name = std::get_if<std::string>(&Metadata.data);
            if (Name && *Name == Kind)
            {
                return Inspector->Follow(Field->id);
            }
        }
    }
    return {};
}
}
