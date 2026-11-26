#include "CIInventoryShell.h"

#include "CIInventoryTile.h"

#include "Algo/BinarySearch.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/NamedSlot.h"
#include "Components/TextBlock.h"
#include "Containers/StaticArray.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Presentation/CFMenu.h"
#include "Presentation/CFSpanGrid.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"

namespace
{
constexpr float ProjectionRasterScale = 2.f;
constexpr int32 MainRightGuardPixels = 4;
constexpr int32 MainBottomGuardPixels = 2;
constexpr int32 ProjectionStrips = 32;
constexpr float FooterHeight = 51.f;
constexpr float ActionPromptGap = 36.f;
const FName GearSection(TEXT("gear"));
const FName ApparelSection(TEXT("apparel"));
const FName ResourcesSection(TEXT("resources"));

bool HasSavedSets(FName Section)
{
    return Section == GearSection || Section == ApparelSection;
}

FText SavedSetsLabel(FName Section)
{
    return Section == ApparelSection ? NSLOCTEXT("Inventory", "OutfitsHeading", "Outfits")
        : NSLOCTEXT("Inventory", "GearSetsHeading", "Gear Sets");
}

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

    FVector2D Unproject(const FVector2D& Point) const
    {
        const FVector2D Offset = Point - Center;
        const double X = Offset.X * FocalLength / (FocalLength * CosYaw - Offset.X * SinYaw);
        return FVector2D(X, Offset.Y * (FocalLength + X * SinYaw) / FocalLength) + Extent * .5;
    }
};

struct FInventoryShellLayout
{
    FInventoryPlane Left, Right;
    FVector2D MenuExtent = FVector2D::ZeroVector;
    double MenuInset = 0.0;
    double MenuFit = 0.0;
    FVector2D MainOrigin = FVector2D::ZeroVector;
    FVector2D MainExtent = FVector2D::ZeroVector;
    FVector2D FooterOrigin = FVector2D::ZeroVector;
    double Scale = 0.0;
};

FInventoryShellLayout LayoutFor(const FVector2D& Size, const FVector2D& MenuSize, float MenuPanelGap, float PanelHeight,
    float FooterGap)
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
    Layout.MenuInset = 8.0 * Scale;
    if (MenuSize.GetMin() > 0.0)
    {
        Layout.MenuFit = FMath::Min((TabWidth - Layout.MenuInset * 2.0) / MenuSize.X, TabHeight / MenuSize.Y);
        Layout.MenuExtent = MenuSize * Layout.MenuFit;
    }
    const double GridTop = Layout.MenuInset + Layout.MenuExtent.Y + FMath::Max(0.f, MenuPanelGap) * Scale;
    const double GridWidth = TabWidth - 16.0 * Scale;
    const double GridInset = FMath::Min(44.0 * Scale, TabWidth * .105);
    const double Top = Size.Y * .055;
    const double Bottom = Size.Y * .80 + 56.0 * Scale;
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
    const double MainRightInset = 8.0 * Scale;
    Layout.MainOrigin = {Layout.Left.Extent.X - MainRightInset - GridWidth, GridTop};
    const double AvailableHeight = Layout.Left.Extent.Y - Layout.MainOrigin.Y - (FooterGap + FooterHeight) * Scale;
    Layout.MainExtent = {GridWidth, FMath::Min(AvailableHeight, FMath::Max(1.f, PanelHeight) * Scale)};
    Layout.FooterOrigin = Layout.MainOrigin + FVector2D(0.0, Layout.MainExtent.Y + FooterGap * Scale);
    return Layout;
}
}

class SCIInventoryActionFooter final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCIInventoryActionFooter) {}
        SLATE_ATTRIBUTE(FName, Section)
        SLATE_ATTRIBUTE(bool, SavedSetsOpen)
        SLATE_ARGUMENT(UCFMenu*, Menu)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Section = Args._Section;
        SavedSetsOpen = Args._SavedSetsOpen;
        Menu = Args._Menu;
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D(0.f, FooterHeight); }

    bool IsOverSavedSets(const FVector2D& Position) const { return SavedSetsBounds.ContainsPoint(Position); }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        SavedSetsBounds = FSlateRect(0, 0, 0, 0);
        const auto* CurrentMenu = Menu.Get();
        if (!CurrentMenu)
        {
            return Layer;
        }

        const auto& Style = CurrentMenu->GetMenuTheme();
        const FString Input = CurrentMenu->PreviousPrompt.ToString();
        const bool bKeyboard = Input == TEXT("Q");
        const bool bPlayStation = Input == TEXT("L1");
        const TCHAR* ConfirmGlyph = bKeyboard ? TEXT("Enter") : (bPlayStation ? TEXT("×") : TEXT("A"));
        const FName CurrentSection = Section.Get();
        const bool bResources = CurrentSection == ResourcesSection;
        const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const auto Font = FCoreStyle::GetDefaultFontStyle("Regular", 15);
        const auto CapFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
        double Right = Geometry.GetLocalSize().X;
        double Y = 2.0;

        const auto DrawPrompt = [&](const FText& Label, const TCHAR* Glyph, bool bRound, float Opacity = 1.f)
        {
            const FText Key = FText::FromString(Glyph);
            const FText Text = bKeyboard
                ? FText::Format(NSLOCTEXT("Inventory", "GearActionKeyLabel", "{0}  {1}"), Key, Label)
                : Label;
            const FVector2D TextSize = Measure->Measure(Text, Font);
            Right -= TextSize.X;
            const auto DrawText = [&](const FText& Value, const FSlateFontInfo& TextFont,
                const FVector2D& Position, const FVector2D& Size, FLinearColor Ink)
            {
                Ink.A *= Opacity;
                FSlateDrawElement::MakeText(Elements, Layer + 1,
                    Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)),
                    Value, TextFont, ESlateDrawEffect::NoPixelSnapping, Ink);
            };
            DrawText(Text, Font, {Right, Y}, TextSize, Style.Ink);
            if (!bKeyboard)
            {
                const FVector2D CapSize = bRound ? FVector2D(21, 21) : FVector2D(23, 18);
                Right -= CapSize.X + 5.0;
                const FVector2D Origin(Right, Y + (TextSize.Y - CapSize.Y) * .5);
                const auto& Brush = bRound ? RoundCap : ShoulderCap;
                FLinearColor Fill = Style.ShoulderFill;
                Fill.A *= Opacity;
                FSlateDrawElement::MakeBox(Elements, Layer,
                    Geometry.ToPaintGeometry(CapSize, FSlateLayoutTransform(Origin)),
                    &Brush, ESlateDrawEffect::NoPixelSnapping, Fill);
                const auto& KeyFont = bRound && Glyph[1] == 0 ? Font : CapFont;
                const FVector2D KeySize = Measure->Measure(Key, KeyFont);
                DrawText(Key, KeyFont, Origin + (CapSize - KeySize) * .5, KeySize, Style.ShoulderInk);
            }
            Right -= ActionPromptGap;
        };

        DrawPrompt(NSLOCTEXT("Inventory", "CloseActionLabel", "Close"),
            bKeyboard ? TEXT("Backspace") : (bPlayStation ? TEXT("○") : TEXT("B")), true);
        const FText SectionAction = bResources
            ? NSLOCTEXT("Inventory", "OpenResourceCategories", "Categories") : SavedSetsLabel(CurrentSection);
        const double ActionRight = Right;
        DrawPrompt(SectionAction, bKeyboard ? (bResources ? TEXT("C") : TEXT("G"))
            : (bPlayStation ? TEXT("L2") : TEXT("LT")), false);
        if (HasSavedSets(CurrentSection))
        {
            SavedSetsBounds = FSlateRect(Right + ActionPromptGap, 0.0, ActionRight, 25.0);
        }
        if (bResources)
        {
            DrawPrompt(NSLOCTEXT("Inventory", "ResourceRadials", "Radials"),
                bKeyboard ? TEXT("F2") : (bPlayStation ? TEXT("□") : TEXT("X")), true);
            Right = Geometry.GetLocalSize().X;
            Y += 28.0;
            DrawPrompt(NSLOCTEXT("Inventory", "SelectResourceStack", "Select"),
                ConfirmGlyph, true, .35f);
            DrawPrompt(NSLOCTEXT("Inventory", "ResourceDrop", "Drop"), TEXT("R3"), true, .35f);
        }
        else if (HasSavedSets(CurrentSection) && SavedSetsOpen.Get())
        {
            Right = Geometry.GetLocalSize().X;
            Y += 28.0;
            DrawPrompt(NSLOCTEXT("Inventory", "CreateGearSetActionLabel", "Create"), ConfirmGlyph, true);
        }
        return Layer + 1;
    }

private:
    TAttribute<FName> Section;
    TAttribute<bool> SavedSetsOpen;
    TWeakObjectPtr<UCFMenu> Menu;
    mutable FSlateRect SavedSetsBounds{0, 0, 0, 0};
    const FSlateRoundedBoxBrush RoundCap{FLinearColor::White, 10.5f};
    const FSlateRoundedBoxBrush ShoulderCap{FLinearColor::White, 4.f};
};

class SCIInventoryShell final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCIInventoryShell) {}
        SLATE_ATTRIBUTE(float, MenuPanelGap)
        SLATE_ATTRIBUTE(float, PanelHeight)
        SLATE_ATTRIBUTE(float, FooterGap)
        SLATE_ATTRIBUTE(FName, ActionSection)
        SLATE_ATTRIBUTE(UCIInventoryTileTheme*, InventoryTheme)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        MenuPanelGap = Args._MenuPanelGap;
        PanelHeight = Args._PanelHeight;
        FooterGap = Args._FooterGap;
        ActionSection = Args._ActionSection;
        InventoryTheme = Args._InventoryTheme;
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
        FooterTexture.Reset();
        FooterWidget.Reset();
        SavedSetsTexture.Reset();
        if (InMenu)
        {
            SAssignNew(FooterWidget, SCIInventoryActionFooter)
                .Menu(InMenu)
                .Section(ActionSection)
                .SavedSetsOpen_Lambda([this] { return bSavedSetsOpen; });
        }
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

    void SetMainPanel(UWidgetSwitcher* InPanel)
    {
        MainPanel = InPanel;
        MainTexture.Reset();
        MainContent.Reset();
        MainRenderRoot.Reset();
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        const FKey Key = Event.GetKey();
        if (HasSavedSets(ActionSection.Get()) && (Key == EKeys::G || Key == EKeys::Gamepad_LeftTrigger))
        {
            if (!Event.IsRepeat())
            {
                bSavedSetsOpen = !bSavedSetsOpen;
            }
            return FReply::Handled();
        }
        if (bSavedSetsOpen && (Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right))
        {
            bSavedSetsOpen = false;
            return FReply::Handled();
        }
        return MenuWidget ? MenuWidget->OnKeyDown(Geometry, Event) : FReply::Unhandled();
    }

    FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() == EKeys::LeftMouseButton && FooterWidget && FooterTexture.IsValid()
            && HasSavedSets(ActionSection.Get()))
        {
            const auto Layout = GetLayout(Geometry.GetLocalSize());
            const FVector2D PlanePoint = Layout.Left.Unproject(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
            if (FooterWidget->IsOverSavedSets((PlanePoint - Layout.FooterOrigin) / Layout.Scale))
            {
                bSavedSetsOpen = !bSavedSetsOpen;
                return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
            }
        }
        return FReply::Unhandled();
    }

    virtual void Tick(const FGeometry& Geometry, double, float DeltaTime) override
    {
        if (Renderer && Menu.IsValid() && Menu->GetSelectedIndex() != RenderedSelection)
        {
            RedrawMenu();
        }

        UWidget* ActivePanel = MainPanel.IsValid() ? MainPanel->GetActiveWidget() : nullptr;
        const auto* NamedSlot = Cast<UNamedSlot>(ActivePanel);
        UWidget* Content = NamedSlot ? NamedSlot->GetContent() : ActivePanel;
        if (MainContent.Get() != Content)
        {
            MainContent = Content;
            MainTexture.Reset();
            MainRenderRoot.Reset();
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        const auto Size = Geometry.GetLocalSize();
        const FName Section = ActionSection.Get();
        if (!HasSavedSets(Section))
        {
            bSavedSetsOpen = false;
            SavedSetsReveal = 0.f;
        }
        SavedSetsReveal = FMath::FInterpConstantTo(SavedSetsReveal, bSavedSetsOpen ? 1.f : 0.f, DeltaTime, 6.f);
        if (!Renderer || !Content || Size.GetMin() < 32.0)
        {
            FooterTexture.Reset();
            if (MainTexture.IsValid())
            {
                MainTexture.Reset();
                Invalidate(EInvalidateWidgetReason::Paint);
            }
            return;
        }

        const auto Layout = GetLayout(Size);
        const auto Extent = Layout.MainExtent;
        if (Extent.GetMin() >= 1.0)
        {
            const float PaintScale = Layout.Scale * ProjectionRasterScale;
            if (!MainRenderRoot)
            {
                SAssignNew(MainRenderRoot, SBox)[ActivePanel->TakeWidget()];
                LastGridViewport = FVector2D::ZeroVector;
            }
            // Keep the last tile's stroke and its filtered edge away from the render-target boundary.
            MainRenderRoot->SetPadding(FMargin(0.f, 0.f, MainRightGuardPixels / PaintScale,
                MainBottomGuardPixels / PaintScale));
            const FVector2D TargetSize(
                FMath::CeilToInt(Extent.X * ProjectionRasterScale) + MainRightGuardPixels,
                FMath::CeilToInt(Extent.Y * ProjectionRasterScale) + MainBottomGuardPixels);
            if (auto* Grid = Cast<UCFSpanGrid>(Content))
            {
                Grid->FitLastRowBottom(Extent.Y / Layout.Scale);
            }
            const double PixelScale = (Geometry.LocalToAbsolute({1.0, 0.0})
                - Geometry.LocalToAbsolute(FVector2D::ZeroVector)).Length();
            if ((Size != LastGridViewport || PixelScale != LastGridPixelScale) && PixelScale > 0.0)
            {
                if (auto* Grid = Cast<UCFSpanGrid>(Content))
                {
                    const double RasterWidth = TargetSize.X / ProjectionRasterScale;
                    const double GridWidth = (TargetSize.X - MainRightGuardPixels) / ProjectionRasterScale;
                    const double StripWidth = RasterWidth / ProjectionStrips;
                    TStaticArray<double, ProjectionStrips + 1> ProjectedEdges;
                    for (int32 Index = 0; Index <= ProjectionStrips; ++Index)
                    {
                        ProjectedEdges[Index] = Layout.Left.Project(
                            Layout.MainOrigin + FVector2D(Index * StripWidth, 0.0)).X;
                    }
                    const double ContentEndStrip = GridWidth / StripWidth;
                    const int32 EndIndex = FMath::FloorToInt(ContentEndStrip);
                    const double EndX = FMath::Lerp(ProjectedEdges[EndIndex], ProjectedEdges[EndIndex + 1],
                        ContentEndStrip - EndIndex);
                    const double StartX = ProjectedEdges[0];
                    Grid->ApplyHorizontalProjection((EndX - StartX) * PixelScale, [&](double OutputX) {
                        const double ScreenX = StartX + OutputX / PixelScale;
                        // Invert the same piecewise-linear mapping used by the surface mesh.
                        const int32 Strip = FMath::Clamp(Algo::UpperBound(ProjectedEdges, ScreenX) - 1,
                            0, ProjectionStrips - 1);
                        const double Alpha = (ScreenX - ProjectedEdges[Strip])
                            / (ProjectedEdges[Strip + 1] - ProjectedEdges[Strip]);
                        return (Strip + Alpha) * StripWidth / Layout.Scale;
                    });
                }
                LastGridViewport = Size;
                LastGridPixelScale = PixelScale;
            }
            DrawToTexture(MainTexture, MainRenderRoot.ToSharedRef(), TargetSize, DeltaTime, PaintScale);
            if (FooterWidget && (Section == GearSection || Section == ApparelSection || Section == ResourcesSection))
            {
                DrawToTexture(FooterTexture, FooterWidget.ToSharedRef(),
                    FVector2D(TargetSize.X - MainRightGuardPixels,
                        FMath::CeilToInt(FooterHeight * PaintScale)), DeltaTime, PaintScale);
            }
            else
            {
                FooterTexture.Reset();
            }
            if (SavedSetsReveal > 0.f)
            {
                const double Height = Extent.Y / Layout.Scale;
                auto* Theme = InventoryTheme.Get();
                if (!SavedSetsGrid.IsValid() || SavedSetsSection != Section
                    || SavedSetsTheme.Get() != Theme || SavedSetsHeight != Height)
                {
                    BuildSavedSetsGrid(Section, *Theme, Height);
                }
                DrawToTexture(SavedSetsTexture, SavedSetsGrid->TakeWidget(), FVector2D(
                    FMath::CeilToInt(Extent.X * SavedSetsWidthRatio * ProjectionRasterScale),
                    TargetSize.Y - MainBottomGuardPixels), DeltaTime, PaintScale);
            }
            Invalidate(EInvalidateWidgetReason::Paint);
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

        const auto Layout = GetLayout(Size);
        if (SavedSetsTexture.IsValid() && SavedSetsReveal > 0.f)
        {
            const FVector2D Extent(SavedSetsTexture->SizeX / ProjectionRasterScale,
                SavedSetsTexture->SizeY / ProjectionRasterScale);
            const FVector2D Origin = Layout.MainOrigin - FVector2D((Extent.X + 16.0 * Layout.Scale) * SavedSetsReveal, 0);
            DrawSurface(Geometry, Elements, Layer + 1, Layout.Left, SavedSetsTexture.Get(), Origin, Extent, SavedSetsReveal);
        }
        if (MainTexture.IsValid() && Layout.MainExtent.GetMin() > 0.0)
        {
            const FVector2D RasterExtent(MainTexture->SizeX / ProjectionRasterScale,
                MainTexture->SizeY / ProjectionRasterScale);
            DrawSurface(Geometry, Elements, Layer + 1, Layout.Left, MainTexture.Get(), Layout.MainOrigin, RasterExtent);
        }

        if (MenuTexture.IsValid() && Layout.MenuExtent.GetMin() > 0.0)
        {
            DrawSurface(Geometry, Elements, Layer + 2, Layout.Left, MenuTexture.Get(),
                FVector2D(Layout.MainOrigin.X - FirstMenuItemX * Layout.MenuFit, Layout.MenuInset), Layout.MenuExtent);
        }
        if (FooterTexture.IsValid() && Layout.MainExtent.GetMin() > 0.0)
        {
            const FVector2D FooterExtent(FooterTexture->SizeX / ProjectionRasterScale,
                FooterTexture->SizeY / ProjectionRasterScale);
            DrawSurface(Geometry, Elements, Layer + 2, Layout.Left, FooterTexture.Get(), Layout.FooterOrigin, FooterExtent);
        }
        return Layer + 2;
    }

private:
    void BuildSavedSetsGrid(FName Section, UCIInventoryTileTheme& Theme, double PanelExtent)
    {
        constexpr int32 VisibleRows = 6;
        const double Heading = Theme.GroupLabelHeight + Theme.GroupLabelPadding.GetTotalSpaceAlong<Orient_Vertical>();
        const float RowHeight = FMath::Max(1.0, (PanelExtent - Heading - (VisibleRows - 1) * Theme.RowGap) / VisibleRows);
        SavedSetsGrid.Reset(NewObject<UCFSpanGrid>());
        SavedSetsGrid->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        SavedSetsGrid->Configure(1, RowHeight, 0.f, Theme.RowGap, Theme.GroupGap,
            Theme.GroupLabelHeight, Theme.GroupLabelPadding, Theme.GroupLabelFont, Theme.GroupLabelTint);
        SavedSetsGrid->AddHeading(SavedSetsLabel(Section));

        auto* Tile = NewObject<UCFTile>(SavedSetsGrid.Get());
        Tile->Theme = &Theme;
        auto* Label = NewObject<UTextBlock>(Tile);
        Label->SetText(NSLOCTEXT("Inventory", "NewGearSet", "Create"));
        Label->SetFont(Theme.LabelFont);
        Label->SetColorAndOpacity(FSlateColor(Theme.TextTint));
        auto* Inset = NewObject<UBorder>(Tile);
        Inset->SetBrushColor(FLinearColor::Transparent);
        Inset->SetPadding(FMargin(Theme.Spacing, 0.f));
        Inset->SetVerticalAlignment(VAlign_Center);
        Inset->SetContent(Label);
        Tile->SetContent(Inset);
        SavedSetsGrid->AddItem(*Tile, 1);
        Tile->TakeWidget();
        Tile->SetVisualState(1.f, 1.f);

        SavedSetsSection = Section;
        SavedSetsTheme = &Theme;
        SavedSetsHeight = PanelExtent;
    }

    FInventoryShellLayout GetLayout(const FVector2D& Size) const
    {
        const FVector2D MenuSize = MenuTexture.IsValid()
            ? FVector2D(MenuTexture->SizeX, MenuTexture->SizeY) : FVector2D::ZeroVector;
        return LayoutFor(Size, MenuSize, MenuPanelGap.Get(), PanelHeight.Get(), FooterGap.Get());
    }

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
        DrawToTexture(MenuTexture, MenuWidget.ToSharedRef(), Size, 0.f);

        // Use the actual Slate arrangement so prompt text and theme spacing can vary.
        FirstMenuItemX = 0.0;
        if (const UWidget* FirstItem = Menu.IsValid() ? Menu->GetChildAt(0) : nullptr)
        {
            const auto ItemWidget = FirstItem->GetCachedWidget();
            if (ItemWidget && ItemWidget->GetVisibility().IsVisible())
            {
                const auto& MenuGeometry = MenuWidget->GetPaintSpaceGeometry();
                FirstMenuItemX = MenuGeometry.AbsoluteToLocal(
                    ItemWidget->GetPaintSpaceGeometry().GetAbsolutePosition()).X;
            }
        }
        RenderedSelection = Menu.IsValid() ? Menu->GetSelectedIndex() : INDEX_NONE;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    void DrawToTexture(TStrongObjectPtr<UTextureRenderTarget2D>& Texture, const TSharedRef<SWidget>& Widget,
        const FVector2D& Size, float DeltaTime, float Scale = 1.f)
    {
        if (!Texture.IsValid() || Texture->SizeX != Size.X || Texture->SizeY != Size.Y)
        {
            Texture.Reset(FWidgetRenderer::CreateTargetFor(Size, TF_Bilinear, false));
        }
        Renderer->DrawWidget(Texture.Get(), Widget, Scale, Size, DeltaTime);
    }

    void DrawSurface(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
        const FInventoryPlane& Plane, UTextureRenderTarget2D* Texture, const FVector2D& Origin,
        const FVector2D& Extent, float VisibleFraction = 1.f) const
    {
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        Vertices.Reserve((ProjectionStrips + 1) * 2);
        Indices.Reserve(ProjectionStrips * 6);
        for (int32 Strip = 0; Strip <= ProjectionStrips; ++Strip)
        {
            const float U = (float(Strip) / ProjectionStrips) * VisibleFraction;
            for (const float V : {0.f, 1.f})
            {
                const auto Point = Plane.Project(Origin + FVector2D(U * Extent.X, V * Extent.Y));
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
        Brush.SetResourceObject(Texture);
        const auto Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush);
        FSlateDrawElement::MakeCustomVerts(Elements, Layer, Handle, Vertices, Indices, nullptr, 0, 0,
            ESlateDrawEffect::PreMultipliedAlpha | ESlateDrawEffect::NoGamma);
    }

    TAttribute<float> MenuPanelGap;
    TAttribute<float> PanelHeight;
    TAttribute<float> FooterGap;
    TAttribute<FName> ActionSection;
    TAttribute<UCIInventoryTileTheme*> InventoryTheme;
    TWeakObjectPtr<UCFMenu> Menu;
    TWeakObjectPtr<UWidgetSwitcher> MainPanel;
    TWeakObjectPtr<UWidget> MainContent;
    TSharedPtr<SWidget> MenuWidget;
    TSharedPtr<SBox> MainRenderRoot;
    TSharedPtr<SCIInventoryActionFooter> FooterWidget;
    TStrongObjectPtr<UCFSpanGrid> SavedSetsGrid;
    TWeakObjectPtr<UCIInventoryTileTheme> SavedSetsTheme;
    FName SavedSetsSection;
    double SavedSetsHeight = 0.0;
    TStrongObjectPtr<UTextureRenderTarget2D> MenuTexture;
    TStrongObjectPtr<UTextureRenderTarget2D> MainTexture;
    TStrongObjectPtr<UTextureRenderTarget2D> FooterTexture;
    TStrongObjectPtr<UTextureRenderTarget2D> SavedSetsTexture;
    static constexpr double SavedSetsWidthRatio = .49 * (2.0 / 3.0);
    float SavedSetsReveal = 0.f;
    bool bSavedSetsOpen = false;
    FWidgetRenderer* Renderer = nullptr;
    FVector2D LastGridViewport = FVector2D::ZeroVector;
    double LastGridPixelScale = 0.0;
    double FirstMenuItemX = 0.0;
    int32 RenderedSelection = INDEX_NONE;
};

void UCIInventoryShell::SetLayoutTheme(UCIInventoryTileTheme* InTheme)
{
    LayoutTheme = InTheme;
}

TSharedRef<SWidget> UCIInventoryShell::RebuildWidget()
{
    SAssignNew(Shell, SCIInventoryShell)
        .MenuPanelGap_Lambda([this] {
            return (LayoutTheme ? LayoutTheme.Get() : GetDefault<UCIInventoryTileTheme>())->MenuPanelGap;
        })
        .PanelHeight_Lambda([this] {
            return (LayoutTheme ? LayoutTheme.Get() : GetDefault<UCIInventoryTileTheme>())->PanelHeight;
        })
        .FooterGap_Lambda([this] {
            const auto& Theme = LayoutTheme ? *LayoutTheme : *GetDefault<UCIInventoryTileTheme>();
            return FMath::Max(0.f, Theme.GroupLabelHeight + Theme.GroupLabelPadding.GetTotalSpaceAlong<Orient_Vertical>());
        })
        .ActionSection_Lambda([this] { return ActionSection; })
        .InventoryTheme_Lambda([this] {
            return LayoutTheme ? LayoutTheme.Get() : GetMutableDefault<UCIInventoryTileTheme>();
        });
    UpdateContent();
    return Shell.ToSharedRef();
}

void UCIInventoryShell::ReleaseSlateResources(bool bReleaseChildren)
{
    UnbindMenu();
    Shell.Reset();
    Super::ReleaseSlateResources(bReleaseChildren);
}

void UCIInventoryShell::OnSlotAdded(UPanelSlot*)
{
    UpdateContent();
}

void UCIInventoryShell::OnSlotRemoved(UPanelSlot*)
{
    UpdateContent();
}

UCFMenu* UCIInventoryShell::GetMenu() const
{
    return Cast<UCFMenu>(GetChildAt(0));
}

UWidgetSwitcher* UCIInventoryShell::GetMainPanel() const
{
    return Cast<UWidgetSwitcher>(GetChildAt(1));
}

void UCIInventoryShell::UpdateContent()
{
    if (!Shell)
    {
        return;
    }

    UnbindMenu();
    auto* Menu = GetMenu();
    Shell->SetMenu(Menu);
    Shell->SetMainPanel(GetMainPanel());
    if (Menu)
    {
        BoundMenu = Menu;
        Menu->OnSelectionChanged.AddUniqueDynamic(this, &UCIInventoryShell::OnMenuSelectionChanged);
        OnMenuSelectionChanged(Menu->GetSelectedIndex(), nullptr);
    }
}

void UCIInventoryShell::UnbindMenu()
{
    if (auto* Menu = BoundMenu.Get())
    {
        Menu->OnSelectionChanged.RemoveDynamic(this, &UCIInventoryShell::OnMenuSelectionChanged);
    }
    BoundMenu.Reset();
}

void UCIInventoryShell::OnMenuSelectionChanged(int32 Index, UCFMenuItem*)
{
    auto* MainPanel = GetMainPanel();
    if (MainPanel && Index >= 0 && Index < MainPanel->GetNumWidgets())
    {
        MainPanel->SetActiveWidgetIndex(Index);
    }
}
