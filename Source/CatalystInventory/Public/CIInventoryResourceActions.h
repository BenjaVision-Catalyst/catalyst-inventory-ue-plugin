#pragma once

#include "CoreMinimal.h"

struct FCIInventoryStackState
{
    int32 Count = 0;
    bool bCanSplit = false;
    bool bCanJoin = false;
};

struct FCIInventoryStackTransfer
{
    FString Destination;
    int32 PreviousCount = 0;
    int32 Moved = 0;
};

struct FCIInventoryStackResult
{
    FString Destination;
    TArray<FCIInventoryStackTransfer> Transfers;
};

struct FCIInventoryResourceRadialInput
{
    FString StackId;
    FName DefinitionId;
    FVector2D IconCenter = FVector2D::ZeroVector;
    double IconSize = 0.0;
    FVector2D Sticks[2]{FVector2D::ZeroVector, FVector2D::ZeroVector};
    FVector2D ViewSize = FVector2D::ZeroVector;
    double Scale = 1.0;
    bool bPlace = false;
    bool bClear = false;
};

struct FCIInventoryResourceRadialAvailability
{
    bool bCanPlace = false;
    bool bCanClear = false;
};

struct FCIInventoryResourceActions
{
    TFunction<TOptional<FCIInventoryStackState>(const FString&)> Read;
    TFunction<bool(const FGuid&, const FString&, int32, int32, bool)> Commit;
    TFunction<TOptional<FCIInventoryStackResult>(const FGuid&)> Poll;
    TFunction<void(bool)> SetRadialsOpen;
    TFunction<FCIInventoryResourceRadialAvailability(const FCIInventoryResourceRadialInput&)> UpdateRadials;
    bool bPublishesAfterLanding = false;
};

inline FVector2D CIResourcePreviewAnchor(FVector2D Source, FVector2D SourceExtent, FVector2D& PreviewExtent,
    int32 Column, int32 Columns, double GridLeft, double GridRight, double ViewHeight, double Gap)
{
    const double RowFraction = Source.Y / ViewHeight;
    int32 VerticalBand = RowFraction < 1.0 / 3.0 ? -1 : (RowFraction > 2.0 / 3.0 ? 1 : 0);
    const double Inward = Column < Columns / 2 ? 1.0 : -1.0;
    if (Columns == 2 && VerticalBand == 0)
    {
        VerticalBand = RowFraction < .5 ? -1 : 1;
    }
    const bool bSideways = Columns > 2 && (Column == 0 || Column == Columns - 1 || VerticalBand == 0);
    const double Clearance = Gap * .5;
    const double AvailableWidth = bSideways
        ? (Inward > 0.0 ? GridRight - Source.X : Source.X - GridLeft) - SourceExtent.X * .5 - Clearance
        : GridRight - GridLeft;
    const double AvailableHeight = bSideways ? ViewHeight
        : (VerticalBand < 0 ? ViewHeight - Source.Y : Source.Y) - SourceExtent.Y * .5 - Clearance;
    PreviewExtent *= FMath::Clamp(FMath::Min(AvailableWidth / PreviewExtent.X, AvailableHeight / PreviewExtent.Y), 0.0, 1.0);

    const FVector2D HalfPreview = PreviewExtent * .5;
    const FVector2D Adjacent = (SourceExtent + PreviewExtent) * .5 + FVector2D(Clearance);
    if (bSideways)
    {
        return {Source.X + Inward * Adjacent.X,
            FMath::Clamp(ViewHeight * (.5 + .125 * VerticalBand), HalfPreview.Y, ViewHeight - HalfPreview.Y)};
    }

    return {FMath::Clamp(Source.X + Inward * SourceExtent.X * .16, GridLeft + HalfPreview.X, GridRight - HalfPreview.X),
        Source.Y - VerticalBand * Adjacent.Y};
}
