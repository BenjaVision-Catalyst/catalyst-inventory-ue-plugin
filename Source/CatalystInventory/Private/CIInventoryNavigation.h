#pragma once

#include "CoreMinimal.h"

class UCIInventoryTile;

struct FCIInventoryNavigationCell
{
    int32 Span = 1;
    TWeakObjectPtr<UCIInventoryTile> Tile;
    float FocusProgress = 0.f;
};

struct FCIInventoryNavigationRow
{
    int32 Columns = 0;
    TArray<FCIInventoryNavigationCell> Cells;
};

inline FIntPoint StepInventoryGrid(TConstArrayView<FCIInventoryNavigationRow> Rows, FIntPoint Cell, FIntPoint Direction)
{
    if (Rows.IsEmpty())
    {
        return FIntPoint::ZeroValue;
    }

    Cell.Y = FMath::Clamp(Cell.Y, 0, Rows.Num() - 1);
    const auto& Row = Rows[Cell.Y];
    Cell.X = FMath::Clamp(Cell.X, 0, Row.Cells.Num() - 1);
    if (Direction.X != 0)
    {
        Cell.X = FMath::Clamp(Cell.X + Direction.X, 0, Row.Cells.Num() - 1);
        return Cell;
    }

    const int32 NextRow = FMath::Clamp(Cell.Y + Direction.Y, 0, Rows.Num() - 1);
    if (NextRow == Cell.Y)
    {
        return Cell;
    }
    double Center = Row.Cells[Cell.X].Span * .5;
    for (int32 Index = 0; Index < Cell.X; ++Index)
    {
        Center += Row.Cells[Index].Span;
    }
    const auto& Target = Rows[NextRow];
    const int32 Column = FMath::FloorToInt(Center * Target.Columns / Row.Columns);
    int32 Right = 0;
    for (int32 Index = 0; Index < Target.Cells.Num(); ++Index)
    {
        Right += Target.Cells[Index].Span;
        if (Column < Right)
        {
            return {Index, NextRow};
        }
    }
    // A partial row may have no tile in the target column.
    return {Target.Cells.Num() - 1, NextRow};
}

struct FCIInventoryNavigationInput
{
    void SetPad(FIntPoint Direction, bool bDown)
    {
        auto& Side = Direction.X < 0 || Direction.Y < 0 ? PadNegative : PadPositive;
        (Direction.X != 0 ? Side.X : Side.Y) = int32(bDown);
        if (PadNegative == FIntPoint::ZeroValue && PadPositive == FIntPoint::ZeroValue && Strength() < .35)
        {
            HeldDirection = FIntPoint::ZeroValue;
        }
    }

    void SetStickAxis(bool bHorizontal, double Value)
    {
        (bHorizontal ? Stick.X : Stick.Y) = Value;
    }

    FIntPoint Step(double Now)
    {
        const double Magnitude = Strength();
        bStickReady |= Magnitude < .35;
        const FIntPoint Pad = PadPositive - PadNegative;
        FIntPoint Direction(Pad.X, Pad.X == 0 ? Pad.Y : 0);
        if (Direction == FIntPoint::ZeroValue && bStickReady && Magnitude >= .60)
        {
            Direction = FMath::Abs(Stick.X) >= FMath::Abs(Stick.Y)
                ? FIntPoint(Stick.X > 0 ? 1 : -1, 0) : FIntPoint(0, Stick.Y > 0 ? -1 : 1);
        }
        if (Direction == FIntPoint::ZeroValue)
        {
            if (Magnitude < .35)
            {
                HeldDirection = FIntPoint::ZeroValue;
            }
            return FIntPoint::ZeroValue;
        }

        const bool bRepeating = Direction == HeldDirection;
        if (bRepeating && Now < NextMove)
        {
            return FIntPoint::ZeroValue;
        }
        NextMove = Now + (bRepeating ? .14 : .34);
        HeldDirection = Direction;
        return Direction;
    }

    bool UpdateTrigger()
    {
        const bool bPressed = bTriggerButton || TriggerAxis >= (bTriggerHeld ? .30 : .60);
        const bool bToggle = bPressed && !bTriggerHeld;
        bTriggerHeld = bPressed;
        return bToggle;
    }

    bool bTriggerButton = false;
    float TriggerAxis = 0.f;

private:
    double Strength() const { return FMath::Max(FMath::Abs(Stick.X), FMath::Abs(Stick.Y)); }

    FVector2D Stick = FVector2D::ZeroVector;
    FIntPoint HeldDirection = FIntPoint::ZeroValue;
    double NextMove = 0.0;
    FIntPoint PadNegative = FIntPoint::ZeroValue;
    FIntPoint PadPositive = FIntPoint::ZeroValue;
    bool bStickReady = false;
    bool bTriggerHeld = false;
};
