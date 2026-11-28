#pragma once

#include "Algo/AllOf.h"
#include "CIInventoryShell.h"
#include "CIInventorySidebarReveal.h"

struct FCIInventorySavedSetRows
{
    static constexpr int32 VisibleCount = 6;
    static constexpr double FadeOutSeconds = .12;
    static constexpr double SlideSeconds = .20;
    static constexpr double FadeInSeconds = .12;

    struct FFrame
    {
        double Position = 0.0;
        double Opacity = 1.0;
        double Focus = 0.0;
    };

    struct FRow
    {
        FCIInventorySavedSet Set;
        bool bCreate = false;
        int32 Index = INDEX_NONE;
        FFrame From;
        double Destination = 0.0;
        bool bEntering = false;
    };

    // True when the canvas needs new widgets or labels. Animation otherwise only changes layout and opacity.
    bool Update(TConstArrayView<FCIInventorySavedSet> Sets, int32 Selection, double Now, bool bShowCreate = true)
    {
        const int32 Count = Sets.Num() + int32(bShowCreate);
        if (Rows.IsEmpty())
        {
            FirstIndex = FirstVisible(0, Selection);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                auto& Row = Rows.AddDefaulted_GetRef();
                Row.Index = Index;
                Row.bCreate = Index == Sets.Num();
                if (!Row.bCreate)
                {
                    Row.Set = Sets[Index];
                }
                Row.Destination = Index - FirstIndex;
                Row.From = {Row.Destination, 1.0, Index == Selection ? 1.0 : 0.0};
            }
            LiveCount = Count;
            SelectedIndex = Selection;
            return true;
        }

        const bool bSameList = LiveCount == Count && Algo::AllOf(Rows, [&](const FRow& Row) {
            return Row.Index == INDEX_NONE || Row.bCreate || Row.Set.Identifier == Sets[Row.Index].Identifier;
        });
        int32 NewFirst = FirstVisible(FirstIndex, Selection);
        // When Create moves past the bottom, make room for it and the new row together.
        if (bShowCreate && Sets.Num() == LiveCount && SelectedIndex == LiveCount - 1 && Selection == SelectedIndex)
        {
            NewFirst = FirstVisible(NewFirst, Sets.Num());
        }
        if (bSameList)
        {
            bool bLabelsChanged = false;
            for (auto& Row : Rows)
            {
                if (Row.Index != INDEX_NONE && !Row.bCreate && Row.Set.Name != Sets[Row.Index].Name)
                {
                    Row.Set.Name = Sets[Row.Index].Name;
                    bLabelsChanged = true;
                }
                // Scroll navigation shifts the window without interrupting an insert or removal.
                Row.From.Position += FirstIndex - NewFirst;
                Row.Destination += FirstIndex - NewFirst;
                if (Selection != SelectedIndex && Row.Index != INDEX_NONE)
                {
                    Row.From.Focus = Row.Index == Selection ? 1.0 : 0.0;
                }
            }
            SelectedIndex = Selection;
            FirstIndex = NewFirst;
            return bLabelsChanged;
        }

        TArray<FRow> Next;
        Next.Reserve(Count);
        bool bRemoved = false;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            auto& Row = Next.AddDefaulted_GetRef();
            Row.Index = Index;
            Row.bCreate = Index == Sets.Num();
            if (!Row.bCreate)
            {
                Row.Set = Sets[Index];
            }
            Row.Destination = Index - NewFirst;
            const auto* Previous = Rows.FindByPredicate([&](const FRow& Entry) { return SameRow(Row, Entry); });
            Row.From = Previous ? Sample(*Previous, Now) : FFrame{Row.Destination, 0.0, 0.0};
            Row.bEntering = !Previous || Row.From.Opacity < 1.0;
        }
        for (const auto& Previous : Rows)
        {
            if (Next.ContainsByPredicate([&](const FRow& Entry) { return SameRow(Previous, Entry); }))
            {
                continue;
            }
            const auto Frame = Sample(Previous, Now);
            if (Frame.Opacity > 0.0 && Frame.Position > -1.0 && Frame.Position < VisibleCount)
            {
                auto& Row = Next.Add_GetRef(Previous);
                Row.Index = INDEX_NONE;
                Row.From = Frame;
                Row.Destination = Frame.Position;
                bRemoved = true;
            }
        }
        Rows = MoveTemp(Next);
        LiveCount = Count;
        FirstIndex = NewFirst;
        SelectedIndex = Selection;
        Started = Now;
        SlideDelay = bRemoved ? FadeOutSeconds : 0.0;
        return true;
    }

    FFrame Sample(const FRow& Row, double Now) const
    {
        const double Elapsed = Now - Started;
        if (Row.Index == INDEX_NONE)
        {
            return {Row.From.Position, Row.From.Opacity * (1.0 - Ease(Elapsed / FadeOutSeconds)), Row.From.Focus};
        }
        const double Move = Ease((Elapsed - SlideDelay) / SlideSeconds);
        const double Opacity = Row.bEntering
            ? FMath::Lerp(Row.From.Opacity, 1.0, Ease((Elapsed - SlideDelay - SlideSeconds * .65) / FadeInSeconds))
            : 1.0;
        return {FMath::Lerp(Row.From.Position, Row.Destination, Move), Opacity,
            FMath::Lerp(Row.From.Focus, Row.Index == SelectedIndex ? 1.0 : 0.0, Move)};
    }

    const TArray<FRow>& GetRows() const { return Rows; }
    int32 GetRowCount() const { return LiveCount; }
    int32 GetFirstIndex() const { return FirstIndex; }

private:
    static int32 FirstVisible(int32 First, int32 Selection)
    {
        // Keep the top row anchored on removal so the rows below move up in the viewport.
        return FMath::Clamp(First, FMath::Max(0, Selection + 1 - VisibleCount), Selection);
    }

    static bool SameRow(const FRow& A, const FRow& B)
    {
        return A.bCreate == B.bCreate && (A.bCreate || A.Set.Identifier == B.Set.Identifier);
    }

    static double Ease(double Fraction)
    {
        return FMath::SmoothStep(0.0, 1.0, Fraction);
    }

    TArray<FRow> Rows;
    int32 LiveCount = 0;
    int32 FirstIndex = 0;
    int32 SelectedIndex = 0;
    double Started = 0.0;
    double SlideDelay = 0.0;
};

struct FCIInventorySavedSetsPresentation
{
    FCIInventorySidebarReveal Reveal;
    FCIInventorySavedSetRows Rows;
};
