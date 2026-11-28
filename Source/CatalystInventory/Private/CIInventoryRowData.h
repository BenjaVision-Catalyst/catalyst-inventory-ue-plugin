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

inline TOptional<double> Number(const magritte::Value& Value)
{
    if (const auto* Integer = std::get_if<std::int64_t>(&Value.data))
    {
        return double(*Integer);
    }
    if (const auto* Real = std::get_if<double>(&Value.data); Real && FMath::IsFinite(*Real))
    {
        return *Real;
    }
    if (const auto* Text = std::get_if<std::string>(&Value.data))
    {
        double Parsed;
        if (LexTryParseString(Parsed, UTF8_TO_TCHAR(Text->c_str())) && FMath::IsFinite(Parsed))
        {
            return Parsed;
        }
    }
    return {};
}

inline FText FormatCompactNumber(double Value)
{
    FNumberFormattingOptions Formatting;
    Formatting.SetMaximumFractionalDigits(0);
    return Value > 1000.0
        ? FText::Format(NSLOCTEXT("CatalystInventory", "ThousandsAbbreviation", "{0}K"),
            FText::AsNumber(Value / 1000.0, &Formatting))
        : FText::AsNumber(Value, &Formatting);
}

inline FText FormatArmour(const magritte::Value& Data)
{
    const auto Value = Number(Data);
    if (!Value || *Value <= 1000.0)
    {
        return DisplayValue(Data);
    }
    const auto Formatted = FormatCompactNumber(*Value);
    const auto* Text = std::get_if<std::string>(&Data.data);
    return Text && Text->starts_with('+')
        ? FText::Format(NSLOCTEXT("CatalystInventory", "PositiveArmour", "+{0}"), Formatted)
        : Formatted;
}

inline double DamagePerSecond(double Damage, double RoundsPerMinute)
{
    return Damage * RoundsPerMinute / 60.0;
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

inline magritte::Value ReadValue(const FCFMagritteBinding& Item, std::string_view Id)
{
    for (const auto& Field : Item.Describe().orderedChildren())
    {
        if (Field->id == Id)
        {
            return Item.Read(*Field);
        }
    }
    return {};
}

inline FText Read(const FCFMagritteBinding& Item, std::string_view Id)
{
    return DisplayValue(ReadValue(Item, Id));
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
