#include "CIInventoryShell.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Presentation/CFMenu.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

namespace
{
struct FInventoryPlane
{
    FVector2D Extent = FVector2D::ZeroVector;
    FVector2D Center = FVector2D::ZeroVector;
    double SinYaw = 0.0;
    double CosYaw = 1.0;
    double FocalLength = 1.0;

    FVector2D Project(const FVector2D& Point) const
    {
        const FVector2D Relative = Point - Extent * .5;
        const double Perspective = FocalLength / (FocalLength + Relative.X * SinYaw);
        return Center + FVector2D(Relative.X * CosYaw, Relative.Y) * Perspective;
    }
};

struct FInventoryShellLayout
{
    FInventoryPlane Left, Right;
    FVector2D MenuBounds = FVector2D::ZeroVector;
    double Scale = 0.0;
};

FInventoryShellLayout LayoutFor(const FVector2D& Size)
{
    FInventoryShellLayout Layout;
    const double CharacterX = Size.X * .61803398875;
    const double CharacterLeft = CharacterX - Size.X * .055;
    const double CharacterRight = CharacterX + Size.X * .055;
    const double RightRoom = Size.X - CharacterRight;
    constexpr double PaneScale = .85;
    const double Scale = FMath::Max(0.0,
        FMath::Min(Size.Y / 900.0, (CharacterLeft - RightRoom * .11 - 8.0) / 805.0) * PaneScale);
    Layout.Scale = Scale;
    const double TabWidth = FMath::Min(Size.X * .47, 512.0 * Scale);
    const double TabHeight = 51.0 * Scale;
    const double GridTop = TabHeight + 10.0 * Scale;
    const double GridHeight = 1.5 * TabHeight;
    const double GridWidth = TabWidth - 16.0 * Scale;
    const double GridInset = FMath::Min(44.0 * Scale, TabWidth * .105);
    const double GridBottom = GridTop + 3.0 * 21.0 * Scale + 5.0 * GridHeight + 4.0 * 8.0 * Scale;
    const double Top = Size.Y * .055;
    const double Bottom = FMath::Max(Top + GridBottom + 59.0 * Scale, Size.Y * .80 + 56.0 * Scale);
    const auto MakePlane = [&](double Width, double Yaw)
    {
        const double Radians = FMath::DegreesToRadians(Yaw);
        FInventoryPlane Plane{
            FVector2D(Width, Bottom - Top), FVector2D(0.0, (Top + Bottom) * .5),
            FMath::Sin(Radians), FMath::Cos(Radians), Size.Y * .75
        };
        const double MinY = FMath::Min(Plane.Project({0.0, 0.0}).Y, Plane.Project({Width, 0.0}).Y);
        const double MaxY = FMath::Max(Plane.Project({0.0, Plane.Extent.Y}).Y, Plane.Project(Plane.Extent).Y);
        Plane.Center.Y += FMath::Max(8.0 - MinY, 0.0) - FMath::Max(MaxY + 8.0 - Size.Y, 0.0);
        return Plane;
    };
    Layout.Left = MakePlane(FMath::Max(TabWidth, GridInset + GridWidth + 8.0 * Scale), 9.0);
    Layout.Right = MakePlane(FMath::Min(420.0 * Scale, RightRoom * .78 * PaneScale), -18.0);
    const double InspectorLeft = Layout.Right.Project({0.0, GridTop}).X;
    const double InspectorRight = Layout.Right.Project({Layout.Right.Extent.X - 8.0 * Scale, GridTop}).X;
    Layout.Right.Center.X = (CharacterRight + Size.X - InspectorLeft - InspectorRight) * .5;
    const double CharacterGap = Layout.Right.Center.X + InspectorLeft - CharacterRight;
    const double InnerEdge = CharacterLeft - CharacterGap
        + Layout.Left.Project({Layout.Left.Extent.X, GridTop}).X
        - Layout.Left.Project({GridInset + GridWidth, GridTop}).X;
    const double LeftMargin = FMath::Max(8.0, Size.X * .025);
    const double ProjectedWidth = FMath::Max(0.0, InnerEdge - LeftMargin);

    // Invert the projected span with the same yaw and focal length. Widen toward
    // the viewport edge while keeping the central opening in the same place.
    const double DepthFactor = ProjectedWidth * Layout.Left.SinYaw / (2.0 * Layout.Left.FocalLength);
    const double PlaneWidth = 2.0 * ProjectedWidth
        / (Layout.Left.CosYaw + FMath::Sqrt(FMath::Square(Layout.Left.CosYaw) + 4.0 * FMath::Square(DepthFactor)));
    Layout.Left = MakePlane(PlaneWidth, 9.0);
    Layout.Left.Center.X = InnerEdge - Layout.Left.Project({PlaneWidth, 0.0}).X;
    Layout.MenuBounds = FVector2D(TabWidth, TabHeight);
    return Layout;
}
}

class SCIInventoryShell final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCIInventoryShell) : _Outline(FLinearColor::White), _OutlineThickness(4.f) {}
        SLATE_ATTRIBUTE(FLinearColor, Outline)
        SLATE_ATTRIBUTE(float, OutlineThickness)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Outline = Args._Outline;
        OutlineThickness = Args._OutlineThickness;
    }

    ~SCIInventoryShell()
    {
        if (Renderer)
        {
            BeginCleanup(Renderer);
        }
    }

    void SetMenu(UCFMenu* InMenu)
    {
        Menu = InMenu;
        MenuWidget = InMenu ? TSharedPtr<SWidget>(InMenu->TakeWidget()) : nullptr;
        MenuTexture.Reset();
        if (MenuWidget && FApp::CanEverRender() && FSlateApplication::IsInitialized())
        {
            if (!Renderer)
            {
                Renderer = new FWidgetRenderer(false);
            }
            RedrawMenu();
        }
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        return MenuWidget ? MenuWidget->OnKeyDown(Geometry, Event) : FReply::Unhandled();
    }

    virtual void Tick(const FGeometry&, double, float) override
    {
        if (Renderer && Menu.IsValid() && Menu->GetSelectedIndex() != RenderedSelection)
        {
            RedrawMenu();
        }
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        const FVector2D Size = Geometry.GetLocalSize();
        if (Size.X < 32.0 || Size.Y < 32.0)
        {
            return Layer;
        }

        const auto Layout = LayoutFor(Size);
        const auto DrawOutline = [&](const FInventoryPlane& Plane)
        {
            if (Plane.Extent.GetMin() <= 0.0)
            {
                return;
            }
            const TArray<FVector2D> Corners{
                Plane.Project({0.0, 0.0}), Plane.Project({Plane.Extent.X, 0.0}),
                Plane.Project(Plane.Extent), Plane.Project({0.0, Plane.Extent.Y}), Plane.Project({0.0, 0.0})
            };
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Corners,
                ESlateDrawEffect::None, Outline.Get(), true, OutlineThickness.Get());
        };
        DrawOutline(Layout.Left);
        DrawOutline(Layout.Right);

        if (MenuTexture.IsValid() && Layout.MenuBounds.GetMin() > 0.0)
        {
            DrawMenu(Geometry, Elements, Layer + 1, Layout);
            return Layer + 1;
        }
        return Layer;
    }

private:
    void RedrawMenu()
    {
        MenuWidget->SlatePrepass(1.f);
        const auto Desired = MenuWidget->GetDesiredSize();
        const FVector2D Size(FMath::CeilToInt(Desired.X), FMath::CeilToInt(Desired.Y));
        if (Size.GetMin() <= 0.0)
        {
            MenuTexture.Reset();
            return;
        }
        if (!MenuTexture.IsValid() || MenuTexture->SizeX != Size.X || MenuTexture->SizeY != Size.Y)
        {
            MenuTexture.Reset(FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, false));
        }
        Renderer->DrawWidget(MenuTexture.Get(), MenuWidget.ToSharedRef(), Size, 0.f);
        RenderedSelection = Menu.IsValid() ? Menu->GetSelectedIndex() : INDEX_NONE;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void DrawMenu(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
        const FInventoryShellLayout& Layout) const
    {
        const FVector2D SourceSize(MenuTexture->SizeX, MenuTexture->SizeY);
        const double Inset = 8.0 * Layout.Scale;
        const FVector2D Bounds = Layout.MenuBounds - FVector2D(Inset * 2.0, 0.0);
        const double Fit = FMath::Min(Bounds.X / SourceSize.X, Bounds.Y / SourceSize.Y);
        const FVector2D DrawSize = SourceSize * Fit;
        constexpr int32 Strips = 32;
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        Vertices.Reserve((Strips + 1) * 2);
        Indices.Reserve(Strips * 6);
        for (int32 Strip = 0; Strip <= Strips; ++Strip)
        {
            const float U = float(Strip) / Strips;
            for (const float V : {0.f, 1.f})
            {
                const auto Point = Layout.Left.Project(FVector2D(Inset + U * DrawSize.X, Inset + V * DrawSize.Y));
                Vertices.Add(FSlateVertex::Make(Geometry.GetAccumulatedRenderTransform(),
                    FVector2f(Point), FVector2f(U, V), FColor::White));
            }
            if (Strip > 0)
            {
                const auto I = static_cast<SlateIndex>(Strip * 2);
                Indices.Append({SlateIndex(I - 2), SlateIndex(I - 1), I, I, SlateIndex(I - 1), SlateIndex(I + 1)});
            }
        }
        FSlateBrush Brush;
        Brush.SetResourceObject(MenuTexture.Get());
        const auto Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush);
        FSlateDrawElement::MakeCustomVerts(Elements, Layer, Handle, Vertices, Indices, nullptr, 0, 0,
            ESlateDrawEffect::PreMultipliedAlpha | ESlateDrawEffect::NoGamma);
    }

    TAttribute<FLinearColor> Outline;
    TAttribute<float> OutlineThickness;
    TWeakObjectPtr<UCFMenu> Menu;
    TSharedPtr<SWidget> MenuWidget;
    TStrongObjectPtr<UTextureRenderTarget2D> MenuTexture;
    FWidgetRenderer* Renderer = nullptr;
    int32 RenderedSelection = INDEX_NONE;
};

TSharedRef<SWidget> UCIInventoryShell::RebuildWidget()
{
    SAssignNew(Shell, SCIInventoryShell)
        .Outline_Lambda([this] { return PanelOutline; })
        .OutlineThickness_Lambda([this] { return PanelOutlineThickness; });
    UpdateMenu();
    return Shell.ToSharedRef();
}

void UCIInventoryShell::ReleaseSlateResources(bool bReleaseChildren)
{
    Shell.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

void UCIInventoryShell::OnSlotAdded(UPanelSlot*)
{
    UpdateMenu();
}

void UCIInventoryShell::OnSlotRemoved(UPanelSlot*)
{
    UpdateMenu();
}

void UCIInventoryShell::UpdateMenu()
{
    if (Shell)
    {
        Shell->SetMenu(Cast<UCFMenu>(GetContent()));
    }
}
