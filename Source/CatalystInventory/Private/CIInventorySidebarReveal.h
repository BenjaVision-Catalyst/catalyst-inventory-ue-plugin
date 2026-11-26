#pragma once

#include "Math/UnrealMathUtility.h"

struct FCIInventorySidebarReveal
{
    void Update(bool bOpen, double Now)
    {
        if (bOpen != bTarget)
        {
            bTarget = bOpen;
            From = Progress;
            Started = Now;
        }
        const double Time = FMath::Clamp((Now - Started) / .20, 0.0, 1.0);
        Progress = FMath::InterpEaseInOut(From, bOpen ? 1.0 : 0.0, Time, 4.0);
    }

    double GetProgress() const { return Progress; }

private:
    double Progress = 0.0;
    double From = 0.0;
    double Started = 0.0;
    bool bTarget = false;
};

struct FCIInventorySidebarGeometry
{
    FCIInventorySidebarGeometry(double GridWidth, double ViewportWidth, double InGridGap, double Progress)
        : Width(FMath::Min(GridWidth * .49, ViewportWidth * .20) * (2.0 / 3.0)),
          GridGap(InGridGap), Reveal(Progress)
    {
    }

    double GetTravel() const { return (Width + 2.0 * GridGap) * Reveal; }

    template <typename PaintStrip>
    void PaintVisibleStrips(PaintStrip&& Paint) const
    {
        const double ClipWidth = GetTravel();
        const double Feather = FMath::Min(ClipWidth, GridGap * (1.0 - Reveal));
        const double Solid = ClipWidth - Feather;
        const auto PaintClipped = [&](double Start, double End, double Opacity)
        {
            End = FMath::Min(End, Width);
            if (Start < End)
            {
                Paint(Start / Width, End / Width, Opacity);
            }
        };
        PaintClipped(0.0, Solid, Reveal);
        constexpr int32 Steps = 4;
        const double Step = Feather / Steps;
        for (int32 Index = 0; Index < Steps && Step > 0.0; ++Index)
        {
            PaintClipped(Solid + Step * Index, Solid + Step * (Index + 1),
                Reveal * (1.0 - (Index + .5) / Steps));
        }
    }

    const double Width;

private:
    const double GridGap;
    const double Reveal;
};
