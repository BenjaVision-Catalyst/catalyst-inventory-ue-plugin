#pragma once

#include "CIEquipmentTile.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

namespace CIInventoryAttributeIcon
{
inline constexpr float Diameter = 15.f;

inline TOptional<FLinearColor> Tint(const UCIInventoryTileTheme& Theme, FStringView Type)
{
    const auto* EquipmentTheme = Cast<UCIEquipmentTileTheme>(&Theme);
    const auto& Palette = EquipmentTheme ? *EquipmentTheme : *GetDefault<UCIEquipmentTileTheme>();
    const FLinearColor* Color = nullptr;
    if (Type == TEXTVIEW("offensive")) { Color = &Palette.Offensive; }
    else if (Type == TEXTVIEW("defensive")) { Color = &Palette.Defensive; }
    else if (Type == TEXTVIEW("utility")) { Color = &Palette.Utility; }

    const float Brightness = Type == TEXTVIEW("utility") ? .63f : .6f;
    return Color ? TOptional<FLinearColor>((*Color * Brightness).CopyWithNewOpacity(.65f)) : TOptional<FLinearColor>();
}

inline TSharedRef<SWidget> Build(FLinearColor Tint)
{
    static const FSlateRoundedBoxBrush Circle{FLinearColor::White, Diameter * .5f};
    return SNew(SBox).WidthOverride(Diameter).HeightOverride(Diameter)
    [
        SNew(SImage).Image(&Circle).ColorAndOpacity(Tint)
    ];
}
}
