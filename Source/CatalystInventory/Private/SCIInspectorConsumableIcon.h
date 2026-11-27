#pragma once

#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

class SCIInspectorConsumableIcon final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCIInspectorConsumableIcon) : _Fuel(false) {}
        SLATE_ARGUMENT(FLinearColor, Tint)
        SLATE_ARGUMENT(bool, Fuel)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Tint = Args._Tint;
        bFuel = Args._Fuel;
    }

    FVector2D ComputeDesiredSize(float) const override { return {14.0, 16.0}; }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        const auto Paint = [&](const TArray<FVector2D>& Points)
        {
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points,
                ESlateDrawEffect::NoPixelSnapping, Tint, true, 1.25f);
        };
        if (bFuel)
        {
            Paint({{3, 5}, {5, 5}, {5, 2}, {9, 2}, {9, 5}, {11, 5}, {11, 14}, {3, 14}, {3, 5}});
            Paint({{5, 8}, {9, 12}});
        }
        else
        {
            Paint({{4, 14}, {4, 6}, {7, 2}, {10, 6}, {10, 14}, {4, 14}});
            Paint({{4, 7}, {10, 7}});
            Paint({{4, 12}, {10, 12}});
        }
        return Layer;
    }

private:
    FLinearColor Tint = FLinearColor::White;
    bool bFuel = false;
};
