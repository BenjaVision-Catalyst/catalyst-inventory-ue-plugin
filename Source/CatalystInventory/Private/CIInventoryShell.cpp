#include "CIInventoryShell.h"

#include "CIEquipmentTile.h"
#include "CIInspectorIdentityTile.h"
#include "CIInventoryNavigation.h"
#include "CIInventorySavedSetRows.h"
#include "CIInventoryTile.h"

#include "Algo/BinarySearch.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/NamedSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Containers/StaticArray.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Presentation/CFMenu.h"
#include "Presentation/CFSpanGrid.h"
#include "Presentation/SCFControllerButton.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/CoreStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
constexpr float ProjectionRasterScale = 2.f;
constexpr int32 MainRightGuardPixels = 4;
constexpr int32 MainBottomGuardPixels = 2;
constexpr int32 ProjectionStrips = 32;
constexpr float FooterHeight = 56.f;
constexpr float ActionPromptGap = 36.f;
const FName GearSection(TEXT("gear"));
const FName ApparelSection(TEXT("apparel"));
const FName ResourcesSection(TEXT("resources"));

bool HasSavedSets(FName Section)
{
    return Section == GearSection || Section == ApparelSection;
}

FIntPoint NavigationDirection(const FKey& Key)
{
    if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left) { return {-1, 0}; }
    if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right) { return {1, 0}; }
    if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up) { return {0, -1}; }
    if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down) { return {0, 1}; }
    return FIntPoint::ZeroValue;
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

    FVector2D Project(const FVector2D& Point, double Lift = 0.0) const
    {
        const FVector2D Relative = Point - Extent * .5;
        const double Perspective = FocalLength / (FocalLength + Relative.X * SinYaw - Lift * CosYaw);
        return Center + FVector2D(Relative.X * CosYaw + Lift * SinYaw, Relative.Y) * Perspective;
    }

    FVector2D Unproject(const FVector2D& Point) const
    {
        const FVector2D Offset = Point - Center;
        const double X = Offset.X * FocalLength / (FocalLength * CosYaw - Offset.X * SinYaw);
        return FVector2D(X, Offset.Y * (FocalLength + X * SinYaw) / FocalLength) + Extent * .5;
    }
};

struct FInventoryTileLift
{
    FBox2D Source;
    FVector2D Origin;
    FVector2D Extent;
    double Depth;
    FIntPoint Cell;
};

struct FInspectorStroke
{
    TArray<FVector2D> Points;
    FLinearColor Tint;
    float Thickness;
};

using FInspectorStrokeSink = TFunction<void(FInspectorStroke)>;

class SCIInspectorLines final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCIInspectorLines) : _Height(64.f), _Thickness(1.5f) {}
        SLATE_ARGUMENT(TArray<FVector2D>, Stroke)
        SLATE_ARGUMENT(TArray<TArray<FVector2D>>, Tracks)
        SLATE_ARGUMENT(FLinearColor, Tint)
        SLATE_ARGUMENT(float, Height)
        SLATE_ARGUMENT(float, Thickness)
        SLATE_ARGUMENT(FInspectorStrokeSink, Sink)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Stroke = Args._Stroke;
        Tracks = Args._Tracks;
        Tint = Args._Tint;
        Height = Args._Height;
        Thickness = Args._Thickness;
        Sink = Args._Sink;
    }

    FVector2D ComputeDesiredSize(float) const override { return {80.0, Height}; }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList&, int32 Layer, const FWidgetStyle&, bool) const override
    {
        if (Sink)
        {
            const FVector2D Inset(1.0, 1.0);
            const FVector2D Size = (FVector2D(Geometry.GetLocalSize()) - Inset * 2.0).ComponentMax(FVector2D(1.0, 1.0));
            const auto Queue = [&](const TArray<FVector2D>& UnitPoints, FLinearColor Color, float Weight)
            {
                FInspectorStroke Line{{}, Color, Weight};
                Line.Points.Reserve(UnitPoints.Num());
                for (const auto& Point : UnitPoints)
                {
                    Line.Points.Add(Geometry.LocalToAbsolute(Inset + Point * Size));
                }
                Sink(MoveTemp(Line));
            };
            for (const auto& Track : Tracks)
            {
                Queue(Track, Tint.CopyWithNewOpacity(Tint.A * .25f), 1.f);
            }
            Queue(Stroke, Tint, Thickness);
        }
        return Layer;
    }

private:
    TArray<FVector2D> Stroke;
    TArray<TArray<FVector2D>> Tracks;
    FLinearColor Tint = FLinearColor::White;
    float Height = 64.f;
    float Thickness = 1.5f;
    FInspectorStrokeSink Sink;
};

class SCIProjectedBlur : public SBackgroundBlur
{
public:
    using SBackgroundBlur::ComputeEffectiveKernelSize;
};

struct FInventoryShellLayout
{
    FInventoryPlane Left, Right;
    FVector2D MenuExtent = FVector2D::ZeroVector;
    double MenuInset = 0.0;
    double MenuFit = 0.0;
    FVector2D MainOrigin = FVector2D::ZeroVector;
    FVector2D MainExtent = FVector2D::ZeroVector;
    FVector2D InspectorOrigin = FVector2D::ZeroVector;
    FVector2D InspectorExtent = FVector2D::ZeroVector;
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
    Layout.InspectorOrigin = {0.0, GridTop};
    Layout.InspectorExtent.X = Layout.Right.Extent.X - 8.0 * Scale;
    const double InspectorLeft = Layout.Right.Project(Layout.InspectorOrigin).X;
    const double InspectorRight = Layout.Right.Project({Layout.InspectorExtent.X, GridTop}).X;
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
    const double InspectorBottom = Size.Y - 24.0 * Scale;
    const auto InspectorBottomAt = [&](double X)
    {
        const double ScreenX = Layout.Right.Project({X, GridTop}).X;
        return Layout.Right.Unproject({ScreenX, InspectorBottom}).Y;
    };
    Layout.InspectorExtent.Y = FMath::Max(0.0,
        FMath::Min(InspectorBottomAt(0.0), InspectorBottomAt(Layout.InspectorExtent.X)) - GridTop);
    const FVector2D InspectorEnd = Layout.InspectorOrigin + Layout.InspectorExtent;
    const double ProjectedTop = FMath::Min(Layout.Right.Project(Layout.InspectorOrigin).Y,
        Layout.Right.Project({InspectorEnd.X, Layout.InspectorOrigin.Y}).Y);
    const double ProjectedBottom = FMath::Max(Layout.Right.Project({Layout.InspectorOrigin.X, InspectorEnd.Y}).Y,
        Layout.Right.Project(InspectorEnd).Y);
    Layout.Right.Center.Y += (Size.Y - ProjectedTop - ProjectedBottom) * .5;
    Layout.FooterOrigin = Layout.MainOrigin + FVector2D(0.0, Layout.MainExtent.Y + FooterGap * Scale);
    return Layout;
}
}

namespace
{
enum class ESavedSetHold : uint8 { Overwrite, Delete };
constexpr ESavedSetHold SavedSetHoldActions[] = {ESavedSetHold::Overwrite, ESavedSetHold::Delete};
using FSavedSetHoldAction = TDelegate<void(ESavedSetHold, FString)>;
using FRenameSavedSet = TDelegate<bool(FString, FString)>;

struct FSavedSetHold
{
    FCFControllerHold Confirmation{1000};
    TSet<FKey> Inputs;
    bool bConfirmed = false;
};
}

class SCIInventoryActionFooter final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCIInventoryActionFooter) {}
        SLATE_ATTRIBUTE(FName, Section)
        SLATE_ATTRIBUTE(bool, SavedSetsOpen)
        SLATE_ATTRIBUTE(bool, ConfirmEquips)
        SLATE_ATTRIBUTE(bool, ConfirmAvailable)
        SLATE_ATTRIBUTE(bool, OverwriteAvailable)
        SLATE_ATTRIBUTE(FCFControllerHoldIndicator, OverwriteIndicator)
        SLATE_ATTRIBUTE(bool, DeleteAvailable)
        SLATE_ATTRIBUTE(FCFControllerHoldIndicator, DeleteIndicator)
        SLATE_ATTRIBUTE(bool, OptionsAvailable)
        SLATE_ARGUMENT(UCFMenu*, Menu)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Section = Args._Section;
        SavedSetsOpen = Args._SavedSetsOpen;
        ConfirmEquips = Args._ConfirmEquips;
        ConfirmAvailable = Args._ConfirmAvailable;
        OverwriteAvailable = Args._OverwriteAvailable;
        OverwriteIndicator = Args._OverwriteIndicator;
        DeleteAvailable = Args._DeleteAvailable;
        DeleteIndicator = Args._DeleteIndicator;
        OptionsAvailable = Args._OptionsAvailable;
        Menu = Args._Menu;
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D(0.f, FooterHeight); }

    bool IsOverSavedSets(const FVector2D& Position) const { return SavedSetsBounds.ContainsPoint(Position); }
    bool IsOverConfirm(const FVector2D& Position) const { return ConfirmBounds.ContainsPoint(Position); }
    bool IsOverClose(const FVector2D& Position) const { return CloseBounds.ContainsPoint(Position); }
    bool IsOverOptions(const FVector2D& Position) const { return OptionsBounds.ContainsPoint(Position); }
    bool IsOverHold(ESavedSetHold Action, const FVector2D& Position) const
    {
        return (Action == ESavedSetHold::Overwrite ? OverwriteBounds : DeleteBounds).ContainsPoint(Position);
    }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        SavedSetsBounds = FSlateRect(0, 0, 0, 0);
        ConfirmBounds = FSlateRect(0, 0, 0, 0);
        OverwriteBounds = FSlateRect(0, 0, 0, 0);
        DeleteBounds = FSlateRect(0, 0, 0, 0);
        OptionsBounds = FSlateRect(0, 0, 0, 0);
        CloseBounds = FSlateRect(0, 0, 0, 0);
        const auto* CurrentMenu = Menu.Get();
        if (!CurrentMenu)
        {
            return Layer;
        }

        const auto& Style = CurrentMenu->GetMenuTheme();
        const FString Input = CurrentMenu->PreviousPrompt.ToString();
        const bool bKeyboard = Input == TEXT("Q");
        const auto Platform = bKeyboard ? ECFControllerPlatform::Keyboard
            : Input == TEXT("L1") ? ECFControllerPlatform::PlayStation : ECFControllerPlatform::Xbox;
        const FName CurrentSection = Section.Get();
        const bool bResources = CurrentSection == ResourcesSection;
        const FCFControllerButtonStyle ButtonStyle{
            Style.ActionLabelFont, Style.ActionGlyphFont, Style.Ink, Style.ShoulderFill, Style.ShoulderInk};
        double Right = Geometry.GetLocalSize().X;
        double Y = 2.0;

        const auto DrawPrompt = [&](const FText& Label, const FKey& KeyboardKey, const FKey& ControllerKey,
            bool bAvailable = true, bool bHold = false, const FCFControllerHoldIndicator& Indicator = {})
        {
            const FKey& Key = bKeyboard ? KeyboardKey : ControllerKey;
            const auto Size = SCFControllerButton::Measure(Label, Key, Platform, ButtonStyle, bHold);
            Right -= Size.X;
            SCFControllerButton::Draw(Geometry, Elements, Layer, {Right, Y}, Label, Key, Platform,
                ButtonStyle, bAvailable, bHold, Indicator);
            Right -= ActionPromptGap;
        };

        DrawPrompt(NSLOCTEXT("Inventory", "CloseActionLabel", "Close"), EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right);
        CloseBounds = FSlateRect(Right + ActionPromptGap, 0.0, Geometry.GetLocalSize().X, 25.0);
        const FText SectionAction = bResources
            ? NSLOCTEXT("Inventory", "OpenResourceCategories", "Categories") : SavedSetsLabel(CurrentSection);
        const double ActionRight = Right;
        DrawPrompt(SectionAction, bResources ? EKeys::C : EKeys::G, EKeys::Gamepad_LeftTrigger);
        if (HasSavedSets(CurrentSection))
        {
            SavedSetsBounds = FSlateRect(Right + ActionPromptGap, 0.0, ActionRight, 25.0);
        }
        if (bResources)
        {
            DrawPrompt(NSLOCTEXT("Inventory", "ResourceRadials", "Radials"), EKeys::F2, EKeys::Gamepad_FaceButton_Left);
            Right = Geometry.GetLocalSize().X;
            Y += 28.0;
            DrawPrompt(NSLOCTEXT("Inventory", "SelectResourceStack", "Select"), EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom, false);
            DrawPrompt(NSLOCTEXT("Inventory", "ResourceDrop", "Drop"), EKeys::Delete, EKeys::Gamepad_RightThumbstick,
                false, !bKeyboard);
        }
        else if (HasSavedSets(CurrentSection) && SavedSetsOpen.Get())
        {
            Right = Geometry.GetLocalSize().X;
            Y += 28.0;
            DrawPrompt(ConfirmEquips.Get() ? NSLOCTEXT("Inventory", "EquipActionLabel", "Equip")
                : NSLOCTEXT("Inventory", "CreateGearSetActionLabel", "Create"),
                EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom, ConfirmAvailable.Get());
            ConfirmBounds = FSlateRect(Right + ActionPromptGap, 25.0, Geometry.GetLocalSize().X, FooterHeight);
            const double OptionsRight = Right;
            DrawPrompt(NSLOCTEXT("Inventory", "GearSetOptionsLabel", "Options"), EKeys::F2, EKeys::Gamepad_FaceButton_Left,
                OptionsAvailable.Get());
            OptionsBounds = FSlateRect(Right + ActionPromptGap, 25.0, OptionsRight, FooterHeight);
            const double OverwriteRight = Right;
            DrawPrompt(NSLOCTEXT("Inventory", "OverwriteGearSetLabel", "Overwrite"), EKeys::T, EKeys::Gamepad_FaceButton_Top,
                OverwriteAvailable.Get(), true, OverwriteIndicator.Get());
            OverwriteBounds = FSlateRect(Right + ActionPromptGap, 25.0, OverwriteRight, FooterHeight);
            const double DeleteRight = Right;
            const auto Indicator = DeleteIndicator.Get();
            DrawPrompt(NSLOCTEXT("Inventory", "DeleteGearSetLabel", "Delete"), EKeys::Delete, EKeys::Gamepad_RightThumbstick,
                DeleteAvailable.Get() || Indicator.bAnimating, true, Indicator);
            DeleteBounds = FSlateRect(Right + ActionPromptGap, 25.0, DeleteRight, FooterHeight);
        }
        return Layer + 2;
    }

private:
    TAttribute<FName> Section;
    TAttribute<bool> SavedSetsOpen;
    TAttribute<bool> ConfirmEquips;
    TAttribute<bool> ConfirmAvailable;
    TAttribute<bool> OverwriteAvailable;
    TAttribute<FCFControllerHoldIndicator> OverwriteIndicator;
    TAttribute<bool> DeleteAvailable;
    TAttribute<FCFControllerHoldIndicator> DeleteIndicator;
    TAttribute<bool> OptionsAvailable;
    TWeakObjectPtr<UCFMenu> Menu;
    mutable FSlateRect SavedSetsBounds{0, 0, 0, 0};
    mutable FSlateRect ConfirmBounds{0, 0, 0, 0};
    mutable FSlateRect OverwriteBounds{0, 0, 0, 0};
    mutable FSlateRect DeleteBounds{0, 0, 0, 0};
    mutable FSlateRect OptionsBounds{0, 0, 0, 0};
    mutable FSlateRect CloseBounds{0, 0, 0, 0};
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
        SLATE_ATTRIBUTE(bool, ConfirmEquips)
        SLATE_ATTRIBUTE(bool, ConfirmAvailable)
        SLATE_EVENT(FSimpleDelegate, OnConfirmSavedSet)
        SLATE_ATTRIBUTE(bool, CanOverwrite)
        SLATE_ATTRIBUTE(bool, CanDelete)
        SLATE_EVENT(FSavedSetHoldAction, OnSavedSetHold)
        SLATE_ATTRIBUTE(bool, CanRename)
        SLATE_EVENT(FRenameSavedSet, OnRenameSavedSet)
        SLATE_EVENT(FSimpleDelegate, OnClose)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        MenuPanelGap = Args._MenuPanelGap;
        PanelHeight = Args._PanelHeight;
        FooterGap = Args._FooterGap;
        ActionSection = Args._ActionSection;
        InventoryTheme = Args._InventoryTheme;
        ConfirmEquips = Args._ConfirmEquips;
        ConfirmAvailable = Args._ConfirmAvailable;
        OnConfirmSavedSet = Args._OnConfirmSavedSet;
        CanOverwrite = Args._CanOverwrite;
        CanDelete = Args._CanDelete;
        OnSavedSetHold = Args._OnSavedSetHold;
        CanRename = Args._CanRename;
        OnRenameSavedSet = Args._OnRenameSavedSet;
        OnClose = Args._OnClose;
    }

    ~SCIInventoryShell()
    {
        CloseSavedSetOptions();
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
                .SavedSetsOpen_Lambda([this] { return bSavedSetsOpen; })
                .ConfirmEquips(ConfirmEquips)
                .ConfirmAvailable(ConfirmAvailable)
                .OverwriteAvailable_Lambda([this] { return HoldTarget(ESavedSetHold::Overwrite).IsValid(); })
                .OverwriteIndicator_Lambda([this] { return HoldIndicator(ESavedSetHold::Overwrite); })
                .DeleteAvailable_Lambda([this] { return HoldTarget(ESavedSetHold::Delete).IsValid(); })
                .DeleteIndicator_Lambda([this] { return HoldIndicator(ESavedSetHold::Delete); })
                .OptionsAvailable_Lambda([this] { return CanOpenSavedSetOptions(); });
        }
        MenuTexture.Reset();
        if (MenuWidget && FApp::CanEverRender() && FSlateApplication::IsInitialized())
        {
            if (!Renderer)
            {
                Renderer = new FWidgetRenderer(false);
                ProjectedBlur = SNew(SCIProjectedBlur);
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

    void SetCloseAction(const FSimpleDelegate& Action) { OnClose = Action; }

    void SetSavedSets(const TArray<FCIInventorySavedSet>& Sets)
    {
        const FGuid PreviousTarget = SelectedSavedSetId();
        SavedSets = Sets;
        SavedSetIndex = FMath::Min(SavedSetIndex, SavedSets.Num());
        if (PreviousTarget != SelectedSavedSetId())
        {
            CloseSavedSetOptions();
            CancelSavedSetHolds();
        }
    }

    void SetSavedSetsState(bool bOpen, int32 Index)
    {
        if (bSavedSetsOpen != bOpen)
        {
            CloseSavedSetOptions();
            CancelSavedSetHolds();
        }
        bSavedSetsOpen = bOpen;
        SelectSavedSet(Index);
    }

    TMap<FName, TPair<FIntPoint, FString>> GetTabSelections() const
    {
        auto Selections = TabSelections;
        if (const auto* Tile = GetSelectedTile())
        {
            const auto& Item = Tile->GetInspectorItem();
            Selections.Add(ActionSection.Get(), {GridFocus,
                Item ? FString(UTF8_TO_TCHAR(Item->ObjectId().c_str())) : FString()});
        }
        return Selections;
    }

    void SwitchSectionNavigation(FName Section)
    {
        TabSelections = GetTabSelections();
        SetSavedSetsState(false, 0);
        SavedSetsPresentation = {};
        SetNavigationGrid(nullptr);
        RestoreTabSelection(Section);
    }

    void RestoreTabSelection(FName Section)
    {
        const auto* Selection = TabSelections.Find(Section);
        GridFocus = Selection ? Selection->Key : FIntPoint::ZeroValue;
        PendingSelectionId = Selection ? Selection->Value : FString();
    }

    FIntPoint GetGridFocus() const { return GridFocus; }

#if WITH_DEV_AUTOMATION_TESTS
    int32 GetInspectorStrokeCount() const { return InspectorStrokes.Num(); }
    TPair<bool, int32> GetInspectorScrollbarState() const
    {
        return { InspectorTexture.IsValid() && InspectorScroll->GetViewFraction() < 1.f, InspectorPaintCount };
    }

    FVector GetInspectorScrollState(const FVector2D& ViewSize) const
    {
        const auto Layout = GetLayout(ViewSize);
        return { InspectorScroll ? InspectorScroll->GetScrollOffset() : 0.f,
            Layout.Right.Project(Layout.InspectorOrigin).Y
                + (InspectorHeader ? InspectorHeader->GetPaintSpaceGeometry().GetAbsolutePosition().Y
                    / ProjectionRasterScale : 0.0),
            Layout.InspectorExtent.Y - Layout.MainExtent.Y };
    }

    double GetGridTileLift(FIntPoint Cell) const
    {
        const auto* Tile = LiftedTiles.FindByPredicate([Cell](const auto& Entry) { return Entry.Cell == Cell; });
        return Tile ? Tile->Depth : 0.0;
    }
#endif

    const UCIInventoryTile* GetSelectedTile() const
    {
        if (HasSavedSets(ActionSection.Get()) && NavigationRows.IsValidIndex(GridFocus.Y))
        {
            const auto& Cells = NavigationRows[GridFocus.Y].Cells;
            return Cells.IsValidIndex(GridFocus.X) ? Cells[GridFocus.X].Tile.Get() : nullptr;
        }
        return nullptr;
    }

    FText GetInspectorLabel() const
    {
        const auto* Tile = GetSelectedTile();
        return Tile ? Tile->GetItemLabel() : FText::GetEmpty();
    }
    const FCIInventoryNavigationInput& GetNavigationInput() const { return NavigationInput; }

    void RestoreNavigation(FIntPoint Focus, const TMap<FName, TPair<FIntPoint, FString>>& Selections,
        const TSharedPtr<const FCIInventoryNavigationInput>& Input)
    {
        TabSelections = Selections;
        RestoreTabSelection(ActionSection.Get());
        if (!TabSelections.Contains(ActionSection.Get()))
        {
            GridFocus = Focus;
        }
        if (Input)
        {
            NavigationInput = *Input;
        }
    }

    bool IsSavedSetsOpen() const { return bSavedSetsOpen; }
    int32 GetSavedSetIndex() const { return SavedSetIndex; }
    const FCIInventorySavedSetsPresentation& GetSavedSetsPresentation() const { return SavedSetsPresentation; }

    void RestoreSavedSetsPresentation(const TSharedPtr<const FCIInventorySavedSetsPresentation>& Presentation)
    {
        if (Presentation)
        {
            SavedSetsPresentation = *Presentation;
            SavedSetsSection = ActionSection.Get();
            SavedSetsCanvas.Reset();
        }
    }
    const FCIInventorySavedSet* SelectedSavedSet() const
    {
        return SavedSets.IsValidIndex(SavedSetIndex) ? &SavedSets[SavedSetIndex] : nullptr;
    }

    void CancelSavedSetHolds()
    {
        const double Now = FPlatformTime::Seconds();
        for (const auto Action : SavedSetHoldActions)
        {
            auto& State = Hold(Action);
            State.Confirmation.Update(!State.Inputs.IsEmpty(), FGuid(), Now);
        }
    }

    void CloseSavedSetOptions()
    {
        if (auto Dialog = OptionsWindow.Pin())
        {
            OptionsWindow.Reset();
            Dialog->RequestDestroyWindow();
        }
    }

    TOptional<FCFControllerHold> CompletedHold(ESavedSetHold Action) const
    {
        const auto& State = Hold(Action);
        return State.bConfirmed && HoldIndicator(Action).bAnimating
            ? TOptional<FCFControllerHold>(State.Confirmation) : TOptional<FCFControllerHold>();
    }

    void RestoreHoldConfirmation(ESavedSetHold Action, const TOptional<FCFControllerHold>& Confirmation)
    {
        if (Confirmation)
        {
            auto& State = Hold(Action);
            State.Confirmation = *Confirmation;
            State.bConfirmed = true;
        }
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (OptionsWindow.IsValid())
        {
            return FReply::Handled();
        }
        const FKey Key = Event.GetKey();
        if (InspectorScroll && !bSavedSetsOpen && (Key == EKeys::PageUp || Key == EKeys::PageDown))
        {
            ScrollInspector(Key == EKeys::PageUp ? -140.f : 140.f);
            return FReply::Handled();
        }
        if (Key == EKeys::Tab || Key == EKeys::Gamepad_Special_Right
            || (!bSavedSetsOpen && (Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)))
        {
            if (!Event.IsRepeat() && OnClose.ExecuteIfBound())
            {
                return FReply::Handled();
            }
        }
        if (bSavedSetsOpen && HasSavedSets(ActionSection.Get()))
        {
            if (Key == EKeys::T || Key == EKeys::Gamepad_FaceButton_Top)
            {
                BeginHold(ESavedSetHold::Overwrite, Key, Event.IsRepeat());
                return FReply::Handled();
            }
            if (Key == EKeys::Delete || Key == EKeys::Gamepad_RightThumbstick)
            {
                BeginHold(ESavedSetHold::Delete, Key, Event.IsRepeat());
                return FReply::Handled();
            }
            if (Key == EKeys::F2 || Key == EKeys::Gamepad_FaceButton_Left)
            {
                if (!Event.IsRepeat())
                {
                    OpenSavedSetOptions();
                }
                return FReply::Handled();
            }
        }
        if (Key == EKeys::Gamepad_LeftTrigger)
        {
            NavigationInput.bTriggerButton = true;
            return UpdateNavigationTrigger() ? FReply::Handled() : FReply::Unhandled();
        }
        if (HasSavedSets(ActionSection.Get()))
        {
            if (Key == EKeys::G)
            {
                if (!Event.IsRepeat())
                {
                    ToggleSavedSets();
                }
                return FReply::Handled();
            }
            const auto Direction = NavigationDirection(Key);
            if (Direction != FIntPoint::ZeroValue)
            {
                if (Key.IsGamepadKey())
                {
                    NavigationInput.SetPad(Direction, true);
                    MoveNavigation(NavigationInput.Step(FPlatformTime::Seconds()));
                }
                else
                {
                    MoveNavigation(Direction);
                }
                return FReply::Handled();
            }
        }
        if (bSavedSetsOpen && HasSavedSets(ActionSection.Get())
            && (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom))
        {
            if (!Event.IsRepeat() && ConfirmAvailable.Get())
            {
                OnConfirmSavedSet.ExecuteIfBound();
            }
            return FReply::Handled();
        }
        if (bSavedSetsOpen && (Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right))
        {
            SetSavedSetsState(false, SavedSetIndex);
            return FReply::Handled();
        }
        const bool bPrevious = Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder;
        const bool bNext = Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder;
        if (Menu.IsValid() && (bPrevious || bNext))
        {
            if (!Event.IsRepeat())
            {
                Menu->StepSelection(bPrevious ? -1 : 1);
            }
            return FReply::Handled();
        }
        return MenuWidget ? MenuWidget->OnKeyDown(Geometry, Event) : FReply::Unhandled();
    }

    FReply OnKeyUp(const FGeometry&, const FKeyEvent& Event) override
    {
        const FKey Key = Event.GetKey();
        if (Key == EKeys::Gamepad_LeftTrigger)
        {
            NavigationInput.bTriggerButton = false;
            return UpdateNavigationTrigger() ? FReply::Handled() : FReply::Unhandled();
        }
        if (Key.IsGamepadKey())
        {
            const auto Direction = NavigationDirection(Key);
            if (Direction != FIntPoint::ZeroValue)
            {
                NavigationInput.SetPad(Direction, false);
                return HasSavedSets(ActionSection.Get()) ? FReply::Handled() : FReply::Unhandled();
            }
        }
        return ReleaseHoldInput(Key) ? FReply::Handled() : FReply::Unhandled();
    }

    FReply OnAnalogValueChanged(const FGeometry&, const FAnalogInputEvent& Event) override
    {
        const FKey Key = Event.GetKey();
        if (Key == EKeys::Gamepad_RightY)
        {
            const float Axis = Event.GetAnalogValue();
            const float Strength = FMath::Clamp((FMath::Abs(Axis) - .2f) / .8f, 0.f, 1.f);
            InspectorScrollRate = -FMath::Sign(Axis) * Strength * 640.f;
            return HasSavedSets(ActionSection.Get()) ? FReply::Handled() : FReply::Unhandled();
        }
        if (Key == EKeys::Gamepad_LeftTriggerAxis)
        {
            NavigationInput.TriggerAxis = Event.GetAnalogValue();
            return UpdateNavigationTrigger() ? FReply::Handled() : FReply::Unhandled();
        }
        if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY)
        {
            NavigationInput.SetStickAxis(Key == EKeys::Gamepad_LeftX, Event.GetAnalogValue());
            return HasSavedSets(ActionSection.Get()) ? FReply::Handled() : FReply::Unhandled();
        }
        return FReply::Unhandled();
    }

    void OnFocusLost(const FFocusEvent&) override
    {
        InspectorScrollRate = 0.f;
        NavigationInput = {};
        for (const auto Action : SavedSetHoldActions)
        {
            Hold(Action).Inputs.Reset();
        }
        CancelSavedSetHolds();
    }

    FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (InspectorScroll && InspectorTexture.IsValid() && !OptionsWindow.IsValid())
        {
            const auto Layout = GetLayout(Geometry.GetLocalSize());
            const auto Point = Layout.Right.Unproject(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()))
                - Layout.InspectorOrigin;
            if (Point.X >= 0.0 && Point.Y >= 0.0
                && Point.X < Layout.InspectorExtent.X && Point.Y < Layout.InspectorExtent.Y)
            {
                ScrollInspector(-Event.GetWheelDelta() * 48.f);
                return FReply::Handled();
            }
        }
        return FReply::Unhandled();
    }

    FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (OptionsWindow.IsValid())
        {
            return FReply::Handled();
        }
        if (Event.GetEffectingButton() == EKeys::LeftMouseButton && FooterWidget && FooterTexture.IsValid())
        {
            const auto Layout = GetLayout(Geometry.GetLocalSize());
            const FVector2D PlanePoint = Layout.Left.Unproject(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
            const FVector2D FooterPoint = (PlanePoint - Layout.FooterOrigin) / Layout.Scale;
            if (FooterWidget->IsOverClose(FooterPoint) && OnClose.ExecuteIfBound())
            {
                return FReply::Handled();
            }
            if (!HasSavedSets(ActionSection.Get()))
            {
                return FReply::Unhandled();
            }
            if (bSavedSetsOpen && FooterWidget->IsOverOptions(FooterPoint))
            {
                OpenSavedSetOptions();
                return FReply::Handled();
            }
            for (const auto Action : SavedSetHoldActions)
            {
                if (bSavedSetsOpen && FooterWidget->IsOverHold(Action, FooterPoint))
                {
                    if (HoldTarget(Action).IsValid())
                    {
                        BeginHold(Action, EKeys::LeftMouseButton, false);
                        return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse).CaptureMouse(SharedThis(this));
                    }
                    return FReply::Handled();
                }
            }
            if (bSavedSetsOpen && FooterWidget->IsOverConfirm(FooterPoint))
            {
                if (ConfirmAvailable.Get())
                {
                    OnConfirmSavedSet.ExecuteIfBound();
                }
                return FReply::Handled();
            }
            if (FooterWidget->IsOverSavedSets(FooterPoint))
            {
                ToggleSavedSets();
                return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
            }
            if (bSavedSetsOpen && SavedSetsTexture.IsValid() && SavedSetsPresentation.Reveal.GetProgress() == 1.0)
            {
                const auto Sidebar = GetSidebarGeometry(Layout, Geometry.GetLocalSize().X);
                const auto Point = (PlanePoint - Layout.MainOrigin + FVector2D(Sidebar.GetTravel(), 0))
                    / Layout.Scale;
                const auto& Theme = *InventoryTheme.Get();
                const double Heading = Theme.GroupLabelHeight + Theme.GroupLabelPadding.GetTotalSpaceAlong<Orient_Vertical>();
                if (Point.X >= 0 && Point.X < Sidebar.Width / Layout.Scale && Point.Y >= Heading && Point.Y < SavedSetsHeight)
                {
                    const double RowPitch = SavedSetsRowHeight + Theme.RowGap;
                    for (const auto& Row : SavedSetsPresentation.Rows.GetRows())
                    {
                        const auto Frame = SavedSetsPresentation.Rows.Sample(Row, SavedSetsPaintedAt);
                        const double Y = Point.Y - Heading - Frame.Position * RowPitch;
                        if (Row.Index == INDEX_NONE || Frame.Opacity <= 0.0 || Y < 0.0 || Y >= SavedSetsRowHeight)
                        {
                            continue;
                        }
                        const int32 Index = Row.bCreate ? SavedSets.Num() : SavedSets.IndexOfByPredicate(
                            [&](const FCIInventorySavedSet& Set) { return Set.Identifier == Row.Set.Identifier; });
                        if (Index != INDEX_NONE)
                        {
                            SelectSavedSet(Index);
                            return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
                        }
                    }
                }
            }
        }
        return FReply::Unhandled();
    }

    FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !HasMouseCapture())
        {
            return FReply::Unhandled();
        }
        ReleaseHoldInput(EKeys::LeftMouseButton);
        return FReply::Handled().ReleaseMouseCapture();
    }

    FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (FooterWidget)
        {
            for (const auto Action : SavedSetHoldActions)
            {
                if (Hold(Action).Inputs.Contains(EKeys::LeftMouseButton))
                {
                    const auto Layout = GetLayout(Geometry.GetLocalSize());
                    const auto PlanePoint = Layout.Left.Unproject(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
                    if (!FooterWidget->IsOverHold(Action, (PlanePoint - Layout.FooterOrigin) / Layout.Scale))
                    {
                        ReleaseHoldInput(EKeys::LeftMouseButton);
                    }
                    return FReply::Handled();
                }
            }
        }
        return FReply::Unhandled();
    }

    void OnMouseCaptureLost(const FCaptureLostEvent&) override
    {
        ReleaseHoldInput(EKeys::LeftMouseButton);
    }

    virtual void Tick(const FGeometry& Geometry, double, float DeltaTime) override
    {
        const double Now = FPlatformTime::Seconds();
        if (HasSavedSets(ActionSection.Get()) && !OptionsWindow.IsValid())
        {
            MoveNavigation(NavigationInput.Step(Now));
        }
        for (const auto Action : SavedSetHoldActions)
        {
            auto& State = Hold(Action);
            if (State.Confirmation.Update(!State.Inputs.IsEmpty(), ActiveHoldTarget(Action), Now))
            {
                State.bConfirmed = true;
                OnSavedSetHold.ExecuteIfBound(Action, SelectedSavedSet()->Identifier);
            }
        }
        if (Renderer && Menu.IsValid() && Menu->GetSelectedIndex() != RenderedSelection)
        {
            RedrawMenu();
        }

        UWidget* ActivePanel = MainPanel.IsValid() ? MainPanel->GetActiveWidget() : nullptr;
        UWidget* Content = GetPanelContent(ActivePanel);
        SetNavigationGrid(HasSavedSets(ActionSection.Get()) ? Cast<UCFSpanGrid>(Content) : nullptr);
        UpdateGridFocus(DeltaTime);
        LiftedTiles.Reset();
        if (MainContent.Get() != Content)
        {
            MainContent = Content;
            MainTexture.Reset();
            MainRenderRoot.Reset();
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        const auto Size = Geometry.GetLocalSize();
        const FName Section = ActionSection.Get();
        if (HasSavedSets(Section))
        {
            SavedSetsPresentation.Reveal.Update(bSavedSetsOpen, Now);
        }
        else
        {
            bSavedSetsOpen = false;
            SavedSetsPresentation.Reveal = {};
        }
        if (SavedSetsSection != Section || (!bSavedSetsOpen && SavedSetsPresentation.Reveal.GetProgress() <= 0.0))
        {
            SavedSetsPresentation.Rows = {};
            SavedSetsCanvas.Reset();
            SavedSetsSection = Section;
        }
        if (!Renderer || !Content || Size.GetMin() < 32.0)
        {
            FooterTexture.Reset();
            InspectorTexture.Reset();
            if (MainTexture.IsValid())
            {
                MainTexture.Reset();
                Invalidate(EInvalidateWidgetReason::Paint);
            }
            return;
        }

        const auto Layout = GetLayout(Size);
        RenderInspector(Layout, DeltaTime);
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
            UpdateLiftedTiles(Layout);
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
            if (bSavedSetsOpen || SavedSetsPresentation.Reveal.GetProgress() > 0.0)
            {
                const auto Sidebar = GetSidebarGeometry(Layout, Size.X);
                const double Height = Extent.Y / Layout.Scale;
                auto* Theme = InventoryTheme.Get();
                const bool bRowsChanged = SavedSetsPresentation.Rows.Update(SavedSets, SavedSetIndex, Now);
                if (bRowsChanged || !SavedSetsCanvas.IsValid() || SavedSetsTheme.Get() != Theme || SavedSetsHeight != Height)
                {
                    BuildSavedSetsCanvas(Section, *Theme, Height);
                }
                const FVector2D SidebarTargetSize(FMath::CeilToInt(Sidebar.GetFullWidth() * ProjectionRasterScale),
                    TargetSize.Y - MainBottomGuardPixels);
                LayoutSavedSetRows(Now, *Theme);
                LayoutSavedSetsCanvas(*Theme, Sidebar, Layout, SidebarTargetSize.X / PaintScale);
                DrawToTexture(SavedSetsTexture, SavedSetsCanvas->TakeWidget(), SidebarTargetSize, DeltaTime, PaintScale);
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
        if (SavedSetsTexture.IsValid() && SavedSetsPresentation.Reveal.GetProgress() > 0.0)
        {
            const auto Sidebar = GetSidebarGeometry(Layout, Size.X);
            const FVector2D Origin = Layout.MainOrigin - FVector2D(Sidebar.GetTravel(), 0.0);
            const double SurfaceWidth = Sidebar.GetFullWidth();
            Sidebar.PaintVisibleStrips(SurfaceWidth, [&](double Start, double End, double Opacity) {
                DrawSurface(Geometry, Elements, Layer + 1, Layout.Left, SavedSetsTexture.Get(), Origin,
                    {SurfaceWidth, Layout.MainExtent.Y}, Start, End, Opacity);
            });
        }
        PaintBackdrops(Geometry, Elements, Layer, Layout);
        if (MainTexture.IsValid() && Layout.MainExtent.GetMin() > 0.0)
        {
            DrawMainGrid(Geometry, Elements, Layer + 1, Layout);
        }
        if (InspectorTexture.IsValid() && Layout.InspectorExtent.GetMin() >= 1.0)
        {
            DrawSurface(Geometry, Elements, Layer + 1, Layout.Right, InspectorTexture.Get(),
                Layout.InspectorOrigin, Layout.InspectorExtent);
            DrawInspectorStrokes(Geometry, Elements, Layer + 2, Layout);
            DrawInspectorScrollbar(Geometry, Elements, Layer + 2, Layout);
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
        return Layer + 3;
    }

private:
    static UWidget* GetPanelContent(UWidget* Panel)
    {
        const auto* NamedSlot = Cast<UNamedSlot>(Panel);
        return NamedSlot ? NamedSlot->GetContent() : Panel;
    }

    bool UpdateNavigationTrigger()
    {
        const bool bToggle = NavigationInput.UpdateTrigger();
        const bool bHandled = HasSavedSets(ActionSection.Get());
        if (bToggle && bHandled && !OptionsWindow.IsValid())
        {
            ToggleSavedSets();
        }
        return bHandled;
    }

    void SetNavigationGrid(UCFSpanGrid* Grid)
    {
        if (NavigationGrid.Get() == Grid && (Grid || NavigationRows.IsEmpty()))
        {
            return;
        }
        for (const auto& Row : NavigationRows)
        {
            for (const auto& Cell : Row.Cells)
            {
                if (auto* Tile = Cell.Tile.Get(); Tile && Cell.FocusProgress > 0.f)
                {
                    Tile->SetVisualState(0.f, 1.f);
                }
            }
        }
        NavigationRows.Reset();
        NavigationGrid = Grid;
        if (!Grid)
        {
            return;
        }

        // The active panel can receive navigation before its Slate widgets are built.
        const auto IsVisible = [](const UWidget& Widget) {
            return UWidget::ConvertSerializedVisibilityToRuntime(Widget.GetVisibility()).IsVisible();
        };
        for (const auto* Child : Grid->GetAllChildren())
        {
            const auto* Box = Cast<USizeBox>(Child);
            const auto* Cells = Box ? Cast<UHorizontalBox>(Box->GetContent()) : nullptr;
            if (!Cells || !IsVisible(*Cells))
            {
                continue;
            }
            FCIInventoryNavigationRow Row;
            Row.Columns = Grid->GetColumnCount();
            for (auto* ChildCell : Cells->GetAllChildren())
            {
                if (auto* Tile = Cast<UCIInventoryTile>(ChildCell))
                {
                    if (Tile->GetVisibility() == ESlateVisibility::Collapsed)
                    {
                        Row.Columns -= Tile->GridColumnSpan;
                    }
                    else if (IsVisible(*Tile))
                    {
                        Row.Cells.Add({Tile->GridColumnSpan, Tile});
                    }
                }
            }
            if (!Row.Cells.IsEmpty())
            {
                NavigationRows.Add(MoveTemp(Row));
            }
        }
        if (!PendingSelectionId.IsEmpty())
        {
            const auto Identifier = MoveTemp(PendingSelectionId);
            PendingSelectionId.Reset();
            for (int32 Y = 0; Y < NavigationRows.Num(); ++Y)
            {
                const auto& Cells = NavigationRows[Y].Cells;
                const int32 X = Cells.IndexOfByPredicate([&Identifier](const auto& Cell)
                {
                    const auto* Tile = Cell.Tile.Get();
                    return Tile && Tile->GetInspectorItem()
                        && Identifier.Equals(UTF8_TO_TCHAR(Tile->GetInspectorItem()->ObjectId().c_str()), ESearchCase::CaseSensitive);
                });
                if (X != INDEX_NONE)
                {
                    GridFocus = FIntPoint(X, Y);
                    return;
                }
            }
        }
        GridFocus = StepInventoryGrid(NavigationRows, GridFocus, FIntPoint::ZeroValue);
    }

    void MoveNavigation(FIntPoint Direction)
    {
        if (Direction == FIntPoint::ZeroValue)
        {
            return;
        }
        if (bSavedSetsOpen)
        {
            if (Direction.Y != 0)
            {
                SelectSavedSet(SavedSetIndex + Direction.Y);
            }
            return;
        }
        SetNavigationGrid(MainPanel.IsValid()
            ? Cast<UCFSpanGrid>(GetPanelContent(MainPanel->GetActiveWidget())) : nullptr);
        GridFocus = StepInventoryGrid(NavigationRows, GridFocus, Direction);
    }

    void UpdateGridFocus(float DeltaTime)
    {
        const bool bSidebarVisible = bSavedSetsOpen || SavedSetsPresentation.Reveal.GetProgress() > 0.0;
        for (int32 Row = 0; Row < NavigationRows.Num(); ++Row)
        {
            auto& Cells = NavigationRows[Row].Cells;
            for (int32 Column = 0; Column < Cells.Num(); ++Column)
            {
                auto& Cell = Cells[Column];
                const float Target = !bSidebarVisible && GridFocus == FIntPoint(Column, Row) ? 1.f : 0.f;
                const float Progress = bSidebarVisible ? 0.f
                    : FMath::FInterpConstantTo(Cell.FocusProgress, Target, DeltaTime, 7.f);
                if (auto* Tile = Cell.Tile.Get(); Tile && Progress != Cell.FocusProgress)
                {
                    Cell.FocusProgress = Progress;
                    Tile->SetVisualState(FMath::SmoothStep(0.f, 1.f, Progress), 1.f);
                }
            }
        }
    }

    static FBox2D TileBounds(const SWidget& Tile, const SWidget& Grid, double Scale)
    {
        const auto& TileGeometry = Tile.GetPaintSpaceGeometry();
        const auto& GridGeometry = Grid.GetPaintSpaceGeometry();
        const auto GridPoint = [&](FVector2D Point) {
            return FVector2D(GridGeometry.AbsoluteToLocal(TileGeometry.LocalToAbsolute(Point))) * Scale;
        };
        return FBox2D(GridPoint(FVector2D::ZeroVector), GridPoint(TileGeometry.GetLocalSize()));
    }

    void UpdateLiftedTiles(const FInventoryShellLayout& Layout)
    {
        const auto* Grid = NavigationGrid.Get();
        const auto GridWidget = Grid ? Grid->GetCachedWidget() : nullptr;
        if (!GridWidget)
        {
            return;
        }
        for (int32 Y = 0; Y < NavigationRows.Num(); ++Y)
        {
            const auto& Cells = NavigationRows[Y].Cells;
            for (int32 X = 0; X < Cells.Num(); ++X)
            {
                const auto& Cell = Cells[X];
                const auto* Tile = Cell.Tile.Get();
                const auto Widget = Tile && Cell.FocusProgress > 0.f ? Tile->GetCachedWidget() : nullptr;
                if (!Widget)
                {
                    continue;
                }

                auto Source = TileBounds(*Widget, *GridWidget, Layout.Scale);
                Source.Min = Source.Min.ComponentMax(FVector2D::ZeroVector);
                Source.Max = Source.Max.ComponentMin(Layout.MainExtent);
                if (Source.GetSize().GetMin() <= 0.0)
                {
                    continue;
                }

                const double Focus = FMath::SmoothStep(0.f, 1.f, Cell.FocusProgress);
                const FVector2D Extent = Source.GetSize() * (1.0 + .035 * Focus);
                const double Lift = 19.0 * Layout.Scale * Focus;
                FVector2D Origin = Layout.MainOrigin + Source.GetCenter() - Extent * .5;
                if (Y == 0 || (ActionSection.Get() == ApparelSection && Y == 2))
                {
                    const auto& Plane = Layout.Left;
                    const double Depth = Plane.FocalLength + (Origin.X - Plane.Extent.X * .5) * Plane.SinYaw;
                    Origin.Y += (Extent.Y - Source.GetSize().Y) * .5
                        + FMath::Max(0.0, (Plane.Extent.Y * .5 - Layout.MainOrigin.Y - Source.Min.Y)
                            * Lift * Plane.CosYaw / Depth);
                }
                LiftedTiles.Add({Source, Origin, Extent, Lift, {X, Y}});
            }
        }
    }

    void DrawMainGrid(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
        const FInventoryShellLayout& Layout) const
    {
        const FVector2D RasterExtent(MainTexture->SizeX / ProjectionRasterScale, MainTexture->SizeY / ProjectionRasterScale);
        if (LiftedTiles.IsEmpty())
        {
            DrawSurface(Geometry, Elements, Layer, Layout.Left, MainTexture.Get(), Layout.MainOrigin, RasterExtent);
            return;
        }

        // Draw the flat grid around the animated cards, then sample each card
        // once at its projected depth. The original footprint stays clear.
        TArray<FBox2D> FlatRegions{FBox2D(FVector2D::ZeroVector, RasterExtent)};
        for (const auto& Tile : LiftedTiles)
        {
            TArray<FBox2D> Remaining;
            for (const auto& Region : FlatRegions)
            {
                const FVector2D Min = Region.Min.ComponentMax(Tile.Source.Min);
                const FVector2D Max = Region.Max.ComponentMin(Tile.Source.Max);
                if ((Max - Min).GetMin() <= 0.0)
                {
                    Remaining.Add(Region);
                    continue;
                }

                const FBox2D Pieces[] = {
                    {Region.Min, {Region.Max.X, Min.Y}},
                    {{Region.Min.X, Max.Y}, Region.Max},
                    {{Region.Min.X, Min.Y}, {Min.X, Max.Y}},
                    {{Max.X, Min.Y}, {Region.Max.X, Max.Y}}
                };
                for (const auto& Piece : Pieces)
                {
                    if (Piece.GetSize().GetMin() > 0.0)
                    {
                        Remaining.Add(Piece);
                    }
                }
            }
            FlatRegions = MoveTemp(Remaining);
        }
        const auto DrawRegion = [&](const FBox2D& Source, FVector2D Origin, FVector2D Extent, double Lift, int32 DrawLayer) {
            const FBox2D UV(Source.Min / RasterExtent, Source.Max / RasterExtent);
            DrawSurface(Geometry, Elements, DrawLayer, Layout.Left, MainTexture.Get(), Origin, Extent,
                0.0, 1.0, 1.0, &UV, Lift);
        };
        for (const auto& Region : FlatRegions)
        {
            DrawRegion(Region, Layout.MainOrigin + Region.Min, Region.GetSize(), 0.0, Layer);
        }
        for (const auto& Tile : LiftedTiles)
        {
            DrawRegion(Tile.Source, Tile.Origin, Tile.Extent, Tile.Depth, Tile.Cell == GridFocus ? Layer + 2 : Layer + 1);
        }
    }

    void ScrollInspector(float Amount)
    {
        const float Current = InspectorScroll->GetScrollOffset();
        const float Offset = FMath::Clamp(Current + Amount, 0.f,
            FMath::Max(0.f, InspectorScroll->GetScrollOffsetOfEnd()));
        if (Offset != Current)
        {
            InspectorScroll->SetScrollOffset(Offset);
            bInspectorScrollDirty = true;
        }
    }

    static bool IsInspectorFieldVisible(const magritte::Description& Field)
    {
        const auto Visible = Field.property("visible");
        const auto* Flag = std::get_if<bool>(&Visible.data);
        return !Flag || *Flag;
    }

    TSharedRef<STextBlock> InspectorText(const FText& Text, const FSlateFontInfo& Font, bool bWrap = false) const
    {
        return SNew(STextBlock).Text(Text).Font(Font)
            .AutoWrapText(bWrap).ColorAndOpacity(FSlateColor(InventoryTheme.Get()->TextTint));
    }

    static FText InspectorValue(const magritte::Value& Value)
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

    void DrawInspectorStrokes(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
        const FInventoryShellLayout& Layout) const
    {
        if (InspectorStrokes.IsEmpty() || !InspectorWidget || !InspectorScroll)
        {
            return;
        }

        const auto& Root = InspectorWidget->GetPaintSpaceGeometry();
        const auto& Body = InspectorScroll->GetPaintSpaceGeometry();
        const FVector2D RootSize(Root.GetLocalSize());
        const FVector2D BodySize(Body.GetLocalSize());
        if (RootSize.GetMin() <= 0.0 || BodySize.GetMin() <= 0.0)
        {
            return;
        }
        const auto Project = [&](FVector2D RasterPoint)
        {
            const FVector2D Position(Root.AbsoluteToLocal(RasterPoint));
            return Layout.Right.Project(Layout.InspectorOrigin + Position / RootSize * Layout.InspectorExtent);
        };
        const auto ClipPoint = [&](FVector2D BodyPoint)
        {
            return Geometry.LocalToAbsolute(Project(Body.LocalToAbsolute(BodyPoint)));
        };

        // Stroke in the final Slate pass so the projected render target does
        // not resample the antialiased edges.
        Elements.PushClip(FSlateClippingZone(ClipPoint({0.0, 0.0}), ClipPoint({BodySize.X, 0.0}),
            ClipPoint({0.0, BodySize.Y}), ClipPoint(BodySize)));
        for (const auto& Stroke : InspectorStrokes)
        {
            TArray<FVector2D> Projected;
            Projected.Reserve(Stroke.Points.Num());
            for (const auto& Point : Stroke.Points)
            {
                Projected.Add(Project(Point));
            }
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Projected,
                ESlateDrawEffect::NoPixelSnapping, Stroke.Tint, true, Stroke.Thickness);
        }
        Elements.PopClip();
    }

    void DrawInspectorScrollbar(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
        const FInventoryShellLayout& Layout) const
    {
        const double ViewFraction = InspectorScroll->GetViewFraction();
        if (ViewFraction >= 1.0)
        {
            return;
        }
        const auto* Theme = InventoryTheme.Get();
        const auto& Root = InspectorWidget->GetPaintSpaceGeometry();
        const auto& Body = InspectorScroll->GetPaintSpaceGeometry();
        const double RootHeight = Root.GetLocalSize().Y;
        const double BodyHeight = Body.GetLocalSize().Y;
        const double GutterWidth = Theme->InspectorPadding * Layout.Scale;
        if (RootHeight <= 0.0 || BodyHeight <= 0.0 || GutterWidth <= 0.0)
        {
            return;
        }

        const double X = Layout.InspectorOrigin.X + Layout.InspectorExtent.X - GutterWidth * .5;
        const auto Project = [&](double BodyY)
        {
            const double Y = Root.AbsoluteToLocal(Body.LocalToAbsolute({0.0, BodyY})).Y;
            return Layout.Right.Project({X, Layout.InspectorOrigin.Y + Y / RootHeight * Layout.InspectorExtent.Y});
        };
        const float Width = FMath::Min(GutterWidth, FMath::Max(.5f, Theme->Scrollbar.Width) * Layout.Scale);
        const auto Draw = [&](double Start, double End, FLinearColor Tint)
        {
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
                {Project(Start), Project(End)}, ESlateDrawEffect::NoPixelSnapping, Tint, true, Width);
        };
        const double ThumbStart = BodyHeight * InspectorScroll->GetViewOffsetFraction();
        const double ThumbHeight = BodyHeight * ViewFraction;
        Draw(0.0, BodyHeight, Theme->Scrollbar.TrackTint);
        Draw(ThumbStart, ThumbStart + ThumbHeight, Theme->Scrollbar.ThumbTint);
    }

    FInspectorStrokeSink InspectorStrokeSink()
    {
        return [this](FInspectorStroke Stroke) { InspectorStrokes.Add(MoveTemp(Stroke)); };
    }

    TSharedPtr<SWidget> BuildFalloffPlot(const FCFMagritteBinding& Section)
    {
        TArray<FVector2D> Points;
        for (const auto& Row : Section.Describe().orderedChildren())
        {
            if (!IsInspectorFieldVisible(*Row))
            {
                continue;
            }
            const auto Distance = Row->property("metadata:ciDistanceMetres");
            const auto Damage = Section.Read(*Row);
            const auto* Metres = std::get_if<std::int64_t>(&Distance.data);
            const auto* Percent = std::get_if<std::int64_t>(&Damage.data);
            if (!Metres || !Percent || *Metres < 0 || *Percent < 0 || *Percent > 100
                || (!Points.IsEmpty() && (*Metres <= Points.Last().X || *Percent > Points.Last().Y)))
            {
                return nullptr;
            }
            Points.Emplace(double(*Metres), double(*Percent));
        }
        if (Points.Num() < 2 || Points[0].X != 0.0)
        {
            return nullptr;
        }

        const auto* Theme = InventoryTheme.Get();
        const auto Label = [&](const FText& Text) { return InspectorText(Text, Theme->LabelFont); };
        const auto Metres = [&](int64 Value) {
            return Label(FText::Format(NSLOCTEXT("Inventory", "FalloffMetres", "{0} m"), FText::AsNumber(Value)));
        };
        const auto MaxDistance = Metres(int64(Points.Last().X));
        const double LongestRange = Points.Last().X;
        const double InitialDamage = Points[0].Y;
        const double DamageDrop = InitialDamage - Points.Last().Y;
        for (auto& Point : Points)
        {
            Point = {Point.X / LongestRange, DamageDrop > 0.0 ? (InitialDamage - Point.Y) / DamageDrop : 0.0};
        }
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SCIInspectorLines)
                .Stroke(MoveTemp(Points))
                .Height(40.f)
                .Tracks(TArray<TArray<FVector2D>>{{{0.0, 0.0}, {1.0, 0.0}}, {{0.0, 1.0}, {1.0, 1.0}}})
                .Tint(Theme->TextTint)
                .Sink(InspectorStrokeSink())
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)[Metres(0)]
                + SHorizontalBox::Slot().AutoWidth()[MaxDistance]
            ];
    }

    TSharedRef<SWidget> BuildInspectorStatsGrid(const FCFMagritteBinding& Section) const
    {
        constexpr int32 Columns = 3;
        const auto* Theme = InventoryTheme.Get();
        auto ValueFont = Theme->InspectorTitleFont;
        ValueFont.Size = 22;
        auto Grid = SNew(SVerticalBox);
        TSharedPtr<SHorizontalBox> Row;
        int32 Index = 0;
        for (const auto& Field : Section.Describe().orderedChildren())
        {
            if (!IsInspectorFieldVisible(*Field))
            {
                continue;
            }
            auto Data = Section.Read(*Field);
            if (auto* Text = std::get_if<std::string>(&Data.data))
            {
                for (const std::string_view Unit : {" RPM", " rounds"})
                {
                    if (Text->ends_with(Unit))
                    {
                        Text->resize(Text->size() - Unit.size());
                        break;
                    }
                }
            }
            const auto Value = InspectorValue(Data);
            if (Value.IsEmpty())
            {
                continue;
            }
            if (Index % Columns == 0)
            {
                Row = SNew(SHorizontalBox);
                Grid->AddSlot().AutoHeight().Padding(0.f, Index == 0 ? 0.f : 4.f, 0.f, 0.f)[Row.ToSharedRef()];
            }
            Row->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [InspectorText(InspectorValue(Field->property("label")), Theme->LabelFont)]
                + SVerticalBox::Slot().AutoHeight()
                [InspectorText(Value, ValueFont)]
            ];
            ++Index;
        }
        return Grid;
    }

    TSharedRef<SWidget> BuildInspectorAttributeLabel(const magritte::Description& Field, const FText& Label) const
    {
        const auto* Theme = InventoryTheme.Get();
        const auto TypeValue = Field.property("metadata:ciAttributeType");
        const auto* Type = std::get_if<std::string>(&TypeValue.data);
        const auto LabelWidget = InspectorText(Label, Theme->LabelFont, true);
        if (!Type)
        {
            return LabelWidget;
        }

        const auto* EquipmentTheme = Cast<UCIEquipmentTileTheme>(Theme);
        const auto& Palette = EquipmentTheme ? *EquipmentTheme : *GetDefault<UCIEquipmentTileTheme>();
        const FLinearColor* Tint = nullptr;
        if (*Type == "offensive")
        {
            Tint = &Palette.Offensive;
        }
        else if (*Type == "defensive")
        {
            Tint = &Palette.Defensive;
        }
        else if (*Type == "utility")
        {
            Tint = &Palette.Utility;
        }
        if (!Tint)
        {
            return LabelWidget;
        }

        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
            [
                SNew(SBox).WidthOverride(InspectorAttributeIconSize).HeightOverride(InspectorAttributeIconSize)
                [SNew(SImage).Image(&InspectorAttributeIconBrush).ColorAndOpacity(*Tint)]
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[LabelWidget];
    }

    TSharedRef<SWidget> BuildInspectorCard(
        const magritte::Description& Field, const FCFMagritteBinding& Section, bool bWeapon)
    {
        const auto* Theme = InventoryTheme.Get();
        const auto KindValue = Field.property("metadata:ciCard");
        const auto* Kind = std::get_if<std::string>(&KindValue.data);
        const bool bSlots = Kind && *Kind == "slots";
        const bool bTier = Kind && *Kind == "tier";
        const bool bMeters = Kind && *Kind == "meters";
        const bool bControls = Kind && *Kind == "controls";
        auto Content = SNew(SVerticalBox);
        if (bWeapon && Kind && *Kind == "stats")
        {
            Content->AddSlot().AutoHeight()[BuildInspectorStatsGrid(Section)];
            return SNew(SBorder).BorderImage(&InspectorBrush).Padding(8.f)[Content];
        }
        if (Kind && *Kind == "falloff")
        {
            if (const auto Plot = BuildFalloffPlot(Section))
            {
                Content->AddSlot().AutoHeight()[Plot.ToSharedRef()];
            }
            return SNew(SBorder).BorderImage(&InspectorBrush).Padding(8.f)[Content];
        }
        for (const auto& Row : Section.Describe().orderedChildren())
        {
            const auto Label = InspectorValue(Row->property("label"));
            if (!IsInspectorFieldVisible(*Row))
            {
                continue;
            }

            const auto Data = Section.Read(*Row);
            const auto Value = InspectorValue(Data);
            const auto LabelWidget = BuildInspectorAttributeLabel(*Row, Label);
            const auto Meter = Row->property("metadata:ciMeterMillionths");
            const auto* Millionths = std::get_if<std::int64_t>(&Meter.data);
            const auto* Percent = std::get_if<std::int64_t>(&Data.data);
            const bool bHandlingMeter = bMeters && Percent && *Percent >= 0 && *Percent <= 100;
            if (Millionths || bHandlingMeter)
            {
                const float Fraction = Millionths
                    ? FMath::Clamp(static_cast<float>(*Millionths) / 1000000.f, 0.f, 1.f)
                    : static_cast<float>(*Percent) / 100.f;
                const FText MeterValue = bHandlingMeter ? FText::AsPercent(Fraction) : Value;
                Content->AddSlot().AutoHeight().Padding(0.f, 3.f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.f)
                        [LabelWidget]
                        + SHorizontalBox::Slot().AutoWidth()
                        [InspectorText(MeterValue, Theme->LabelFont)]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SCIInspectorLines)
                        .Tracks(TArray<TArray<FVector2D>>{{{0.0, .5}, {1.0, .5}}})
                        .Stroke(Fraction > 0.f ? TArray<FVector2D>{{0.0, .5}, {Fraction, .5}} : TArray<FVector2D>{})
                        .Tint(Theme->TextTint)
                        .Height(5.f)
                        .Thickness(2.5f)
                        .Sink(InspectorStrokeSink())
                    ]
                ];
                continue;
            }
            TSharedPtr<SWidget> ValueWidget;
            if (bControls)
            {
                if (const auto* Symbol = std::get_if<magritte::Value::Symbol>(&Data.data))
                {
                    const FKey Key(FName(UTF8_TO_TCHAR(Symbol->utf8.c_str())));
                    if (const auto* CurrentMenu = Menu.Get(); CurrentMenu && Key.IsGamepadKey())
                    {
                        const auto& Style = CurrentMenu->GetMenuTheme();
                        const FCFControllerButtonStyle ButtonStyle{
                            Style.ActionLabelFont, Style.ActionGlyphFont, Style.Ink, Style.ShoulderFill, Style.ShoulderInk};
                        ValueWidget = SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 5.f, 0.f)
                            [
                                SNew(STextBlock)
                                .Text(NSLOCTEXT("Inventory", "TapGadgetControl", "Tap"))
                                .Font(ButtonStyle.LabelFont)
                                .ColorAndOpacity(FSlateColor(ButtonStyle.LabelTint))
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SCFControllerButton)
                                .ControllerKey(Key)
                                .Platform(ECFControllerPlatform::PlayStation)
                                .Style(ButtonStyle)
                                .Visibility(EVisibility::HitTestInvisible)
                            ];
                    }
                }
            }
            if (!ValueWidget)
            {
                if (Value.IsEmpty())
                {
                    continue;
                }
                ValueWidget = InspectorText(Value, bTier ? Theme->GroupLabelFont : Theme->LabelFont, bSlots);
            }
            if (bSlots)
            {
                Content->AddSlot().AutoHeight().Padding(0.f, 3.f)
                [
                    SNew(SBorder).BorderImage(&InspectorBrush).Padding(6.f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[LabelWidget]
                        + SVerticalBox::Slot().AutoHeight()[ValueWidget.ToSharedRef()]
                    ]
                ];
            }
            else
            {
                Content->AddSlot().AutoHeight().Padding(0.f, 2.f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)[LabelWidget]
                    + SHorizontalBox::Slot().AutoWidth()[ValueWidget.ToSharedRef()]
                ];
            }
        }
        return SNew(SBorder).BorderImage(&InspectorBrush).Padding(8.f)[Content];
    }

    void RenderInspector(const FInventoryShellLayout& Layout, float DeltaTime)
    {
        const auto* Tile = GetSelectedTile();
        if (!Tile || Tile->GetItemLabel().IsEmpty() || Layout.InspectorExtent.GetMin() < 1.0)
        {
            InspectorTexture.Reset();
            return;
        }

        const auto* Theme = InventoryTheme.Get();
        const float Scale = Layout.Scale * ProjectionRasterScale;
        const auto& Item = Tile->GetInspectorItem();
        const auto* Description = Item ? &Item->Describe() : nullptr;
        const bool bContentChanged = !InspectorWidget || InspectorTheme.Get() != Theme || InspectorTile.Get() != Tile
            || (InspectorItem ? &InspectorItem->Describe() : nullptr) != Description;
        if (bContentChanged)
        {
            InspectorBrush = FSlateRoundedBoxBrush(Theme->Style.Background, 0.f, Theme->Style.Border, Theme->Style.Stroke);
            auto Identity = TStrongObjectPtr<UCIInspectorIdentityTile>(NewObject<UCIInspectorIdentityTile>());
            Identity->Theme = Theme->CreateProjectedSurfaceTheme(Identity.Get());
            Identity->InitializeFrom(*Tile);
            InspectorHeader = Identity->TakeWidget();
            InspectorIdentityTile = MoveTemp(Identity);

            auto Content = SNew(SVerticalBox);
            if (Item)
            {
                bool bWeapon = false;
                for (const auto& Field : Item->Describe().orderedChildren())
                {
                    if (Field->id == "presentationKind")
                    {
                        const auto Kind = Item->Read(*Field);
                        const auto* Name = std::get_if<std::string>(&Kind.data);
                        bWeapon = Name && *Name == "weapon";
                        break;
                    }
                }
                if (const auto Inspector = Item->Follow("inspector"))
                {
                    for (const auto& Field : Inspector->Describe().orderedChildren())
                    {
                        if (!IsInspectorFieldVisible(*Field))
                        {
                            continue;
                        }
                        if (const auto Section = Inspector->Follow(*Field))
                        {
                            const auto Heading = InspectorValue(Field->property("label"));
                            if (!Heading.IsEmpty())
                            {
                                FMargin HeadingPadding = Theme->GroupLabelPadding;
                                HeadingPadding.Top += 6.f;
                                Content->AddSlot().AutoHeight().Padding(HeadingPadding)
                                [
                                    SNew(STextBlock)
                                    .Text(Heading)
                                    .Font(Theme->GroupLabelFont)
                                    .ColorAndOpacity(FSlateColor(Theme->GroupLabelTint))
                                    .AutoWrapText(true)
                                ];
                            }
                            Content->AddSlot().AutoHeight()[BuildInspectorCard(*Field, *Section, bWeapon)];
                        }
                        else if (const auto Text = InspectorValue(Inspector->Read(*Field)); !Text.IsEmpty())
                        {
                            Content->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
                            [InspectorText(Text, Theme->LabelFont, true)];
                        }
                    }
                }
            }
            InspectorWidget = SNew(SBorder).BorderImage(&InspectorBrush)
                .Padding(FMargin(Theme->InspectorPadding)).Clipping(EWidgetClipping::ClipToBounds)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        InspectorHeader.ToSharedRef()
                    ]
                    + SVerticalBox::Slot().FillHeight(1.f)
                    [
                        SAssignNew(InspectorScroll, SScrollBox)
                        .ScrollBarVisibility(EVisibility::Collapsed)
                        .Clipping(EWidgetClipping::ClipToBounds)
                        + SScrollBox::Slot()[Content]
                    ]
                ];
            InspectorTheme = Theme;
            InspectorTile = Tile;
            InspectorItem = Item;
            bInspectorScrollDirty = true;
        }
        const FVector2D Size(FMath::CeilToDouble(Layout.InspectorExtent.X * ProjectionRasterScale),
            FMath::CeilToDouble(Layout.InspectorExtent.Y * ProjectionRasterScale));
        const bool bMeasureWrapping = bContentChanged || !InspectorTexture.IsValid()
            || InspectorTexture->SizeX != Size.X || InspectorScale != Scale;
        if (InspectorTexture.IsValid() && (InspectorTexture->SizeX != Size.X || InspectorTexture->SizeY != Size.Y))
        {
            bInspectorScrollDirty = true;
        }
        if (InspectorScrollRate != 0.f && !bSavedSetsOpen && !OptionsWindow.IsValid())
        {
            ScrollInspector(InspectorScrollRate * DeltaTime);
        }
        if (bInspectorScrollDirty || !InspectorTexture.IsValid() || InspectorScale != Scale)
        {
            InspectorScale = Scale;
            bInspectorScrollDirty = false;
            if (bMeasureWrapping)
            {
                // Measure wrapping at the final width before presenting the new content.
                DrawToTexture(InspectorTexture, InspectorWidget.ToSharedRef(), Size, 0.f, Scale);
            }
            InspectorStrokes.Reset();
            DrawToTexture(InspectorTexture, InspectorWidget.ToSharedRef(), Size, DeltaTime, Scale);
#if WITH_DEV_AUTOMATION_TESTS
            ++InspectorPaintCount;
#endif
        }
    }

    void PaintBackdrops(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
        const FInventoryShellLayout& Layout) const
    {
        const float Strength = FMath::Clamp(InventoryTheme.Get()->Style.BackdropBlur, 0.f, 30.f);
        const auto* Window = Elements.GetPaintWindow();
        if (!ProjectedBlur || Strength <= 0.f || ProjectedBlur->IsUsingLowQualityFallbackBrush()
            || !Window || Window->IsVirtualWindow())
        {
            return;
        }

        int32 KernelSize = 0;
        int32 Downsample = 0;
        ProjectedBlur->ComputeEffectiveKernelSize(Strength, KernelSize, Downsample);
        const int32 Factor = FMath::Max(1, Downsample);
        const auto PaintBackdrop = [&](const FInventoryPlane& Plane, FVector2D Min, FVector2D Max, double Lift = 0.0) {
            const auto Project = [&](FVector2D Point) { return Geometry.LocalToAbsolute(Plane.Project(Point, Lift)); };
            const FVector2D TL = Project(Min);
            const FVector2D TR = Project({Max.X, Min.Y});
            const FVector2D BL = Project({Min.X, Max.Y});
            const FVector2D BR = Project(Max);
            const FVector2D Origin(FMath::Min(TL.X, BL.X), FMath::Min(TL.Y, TR.Y));
            const FVector2D End(FMath::Max(TR.X, BR.X), FMath::Max(BL.Y, BR.Y));
            const FVector2D Extent = End - Origin;
            Elements.PushClip(FSlateClippingZone(TL, TR, BL, BR));
            FSlateDrawElement::MakePostProcessBlur(Elements, Layer, FPaintGeometry(Origin, Extent, Geometry.Scale),
                FVector4f(KernelSize, FMath::Max(.5f, Strength) / Factor, FMath::CeilToInt(Extent.X / Factor),
                    FMath::CeilToInt(Extent.Y / Factor)), Downsample, FVector4f(0));
            Elements.PopClip();
        };

        // Sample the game viewport before compositing the clear inventory surfaces.
        if (InspectorTexture.IsValid() && Layout.InspectorExtent.GetMin() >= 1.0)
        {
            PaintBackdrop(Layout.Right, Layout.InspectorOrigin, Layout.InspectorOrigin + Layout.InspectorExtent);
        }
        const auto* Grid = Cast<UCFSpanGrid>(MainContent.Get());
        const auto GridWidget = Grid ? Grid->GetCachedWidget() : nullptr;
        if (!MainTexture.IsValid() || Layout.MainExtent.GetMin() <= 0.0 || !GridWidget)
        {
            return;
        }
        for (const auto* Child : Grid->GetAllChildren())
        {
            const auto* Row = Cast<USizeBox>(Child);
            const auto* Cells = Row ? Cast<UHorizontalBox>(Row->GetContent()) : nullptr;
            if (!Cells || !Cells->IsVisible())
            {
                continue;
            }
            for (const auto* Cell : Cells->GetAllChildren())
            {
                const auto Tile = Cell->IsVisible() && Cell->IsA<UCFTile>() ? Cell->GetCachedWidget() : nullptr;
                if (!Tile)
                {
                    continue;
                }
                const auto Bounds = TileBounds(*Tile, *GridWidget, Layout.Scale);
                const FVector2D Min = Bounds.Min.ComponentMax(FVector2D::ZeroVector);
                const FVector2D Max = Bounds.Max.ComponentMin(Layout.MainExtent);
                if ((Max - Min).GetMin() <= 0.0)
                {
                    continue;
                }
                if (const auto* Lifted = LiftedTiles.FindByPredicate([&](const auto& Entry) {
                    return Entry.Source.Min == Min && Entry.Source.Max == Max;
                }))
                {
                    PaintBackdrop(Layout.Left, Lifted->Origin, Lifted->Origin + Lifted->Extent, Lifted->Depth);
                }
                else
                {
                    PaintBackdrop(Layout.Left, Layout.MainOrigin + Min, Layout.MainOrigin + Max);
                }
            }
        }
    }

    bool CanOpenSavedSetOptions() const
    {
        return bSavedSetsOpen && HasSavedSets(ActionSection.Get()) && !OptionsWindow.IsValid()
            && CanRename.Get() && SelectedSavedSetId().IsValid();
    }

    FText ValidateSavedSetName(const FString& Identifier, const FString& Name) const
    {
        if (Name.IsEmpty() || Name.Len() > 48 || Name.Contains(TEXT("\n")) || Name.Contains(TEXT("\r")))
        {
            return NSLOCTEXT("Inventory", "InvalidGearSetName", "Enter a name between 1 and 48 characters.");
        }
        if (SavedSets.ContainsByPredicate([&](const FCIInventorySavedSet& Set) {
            return Set.Identifier != Identifier && Set.Name.Equals(Name, ESearchCase::IgnoreCase);
        }))
        {
            return NSLOCTEXT("Inventory", "DuplicateGearSetName", "That name is already in use.");
        }
        return FText::GetEmpty();
    }

    void OpenSavedSetOptions()
    {
        CancelSavedSetHolds();
        if (!CanOpenSavedSetOptions() || !FSlateApplication::IsInitialized() || !Menu.IsValid())
        {
            return;
        }

        const auto& Selected = *SelectedSavedSet();
        const auto Dialog = SNew(SWindow)
            .Title(ActionSection.Get() == ApparelSection ? NSLOCTEXT("Inventory", "RenameOutfitTitle", "Rename Outfit")
                : NSLOCTEXT("Inventory", "RenameGearSetTitle", "Rename Gear Set"))
            .ClientSize(FVector2D(420, 90))
            .SizingRule(ESizingRule::FixedSize)
            .SupportsMinimize(false)
            .SupportsMaximize(false);
        const TWeakPtr<SCIInventoryShell> WeakShell = SharedThis(this);
        const auto Field = MakeShared<TWeakPtr<SEditableTextBox>>();
        auto NameField = SNew(SEditableTextBox)
            .Text(FText::FromString(Selected.Name))
            .Font(Menu->GetMenuTheme().ActionLabelFont)
            .SelectAllTextWhenFocused(true)
            .SelectAllTextOnCommit(false)
            .ClearKeyboardFocusOnCommit(false)
            .RevertTextOnEscape(true)
            .OnTextCommitted_Lambda([WeakShell, Field, Identifier = Selected.Identifier](const FText& Value, ETextCommit::Type Commit) {
                const auto Self = WeakShell.Pin();
                if (Commit != ETextCommit::OnEnter || !Self || !Self->OptionsWindow.IsValid())
                {
                    return;
                }
                const FString Name = Value.ToString().TrimStartAndEnd();
                FText Error = Self->ValidateSavedSetName(Identifier, Name);
                if (Error.IsEmpty())
                {
                    if (Self->OnRenameSavedSet.IsBound() && Self->OnRenameSavedSet.Execute(Identifier, Name))
                    {
                        Self->CloseSavedSetOptions();
                        return;
                    }
                    Error = NSLOCTEXT("Inventory", "RenameSavedSetUnavailable", "Unable to rename right now.");
                }
                if (const auto LiveField = Field->Pin())
                {
                    LiveField->SetError(Error);
                }
            });
        *Field = NameField;
        Dialog->SetContent(SNew(SBorder).Padding(FMargin(18))[NameField]);
        Dialog->SetWidgetToFocusOnActivate(NameField);
        Dialog->SetOnWindowClosed(FOnWindowClosed::CreateLambda([WeakShell](const TSharedRef<SWindow>& Closed) {
            if (const auto Self = WeakShell.Pin(); Self && Self->OptionsWindow.Pin() == Closed)
            {
                Self->OptionsWindow.Reset();
            }
        }));
        OptionsWindow = Dialog;
        FSlateApplication::Get().AddWindow(Dialog);
    }

    FSavedSetHold& Hold(ESavedSetHold Action) { return Holds[static_cast<int32>(Action)]; }
    const FSavedSetHold& Hold(ESavedSetHold Action) const { return Holds[static_cast<int32>(Action)]; }

    FCFControllerHoldIndicator HoldIndicator(ESavedSetHold Action) const
    {
        return Hold(Action).Confirmation.GetIndicator(FPlatformTime::Seconds());
    }

    FGuid SelectedSavedSetId() const
    {
        FGuid Target;
        if (const auto* Selected = SelectedSavedSet())
        {
            FGuid::ParseExact(Selected->Identifier, EGuidFormats::DigitsWithHyphens, Target);
        }
        return Target;
    }

    FGuid HoldTarget(ESavedSetHold Action) const
    {
        const bool bAvailable = Action == ESavedSetHold::Overwrite ? CanOverwrite.Get() : CanDelete.Get();
        return bSavedSetsOpen && HasSavedSets(ActionSection.Get()) && bAvailable && !OptionsWindow.IsValid()
            ? SelectedSavedSetId() : FGuid();
    }

    FGuid ActiveHoldTarget(ESavedSetHold Action) const
    {
        const auto Other = Action == ESavedSetHold::Overwrite ? ESavedSetHold::Delete : ESavedSetHold::Overwrite;
        return Hold(Other).Inputs.IsEmpty() ? HoldTarget(Action) : FGuid();
    }

    void BeginHold(ESavedSetHold Action, const FKey& Key, bool bRepeat)
    {
        auto& State = Hold(Action);
        if (bRepeat || State.Inputs.Contains(Key))
        {
            return;
        }
        const bool bFirst = State.Inputs.IsEmpty();
        State.Inputs.Add(Key);
        if (!ActiveHoldTarget(Action).IsValid())
        {
            CancelSavedSetHolds();
        }
        else if (bFirst)
        {
            const double Now = FPlatformTime::Seconds();
            State.bConfirmed = State.bConfirmed && State.Confirmation.GetIndicator(Now).bAnimating;
            State.Confirmation.Update(true, HoldTarget(Action), Now);
        }
    }

    bool ReleaseHoldInput(const FKey& Key)
    {
        bool bReleased = false;
        for (const auto Action : SavedSetHoldActions)
        {
            auto& State = Hold(Action);
            if (State.Inputs.Remove(Key) > 0)
            {
                if (State.Inputs.IsEmpty())
                {
                    State.Confirmation.Update(false, FGuid(), FPlatformTime::Seconds());
                }
                bReleased = true;
            }
        }
        return bReleased;
    }

    void SelectSavedSet(int32 Index)
    {
        const int32 Selection = FMath::Clamp(Index, 0, SavedSets.Num());
        if (SavedSetIndex != Selection)
        {
            CloseSavedSetOptions();
            CancelSavedSetHolds();
            SavedSetIndex = Selection;
        }
    }

    void ToggleSavedSets()
    {
        SetSavedSetsState(!bSavedSetsOpen, bSavedSetsOpen ? SavedSetIndex : 0);
    }

    void BuildSavedSetsCanvas(FName Section, UCIInventoryTileTheme& Theme, double PanelExtent)
    {
        const FMargin Padding = Theme.GroupLabelPadding;
        const double HeadingHeight = Theme.GroupLabelHeight + Padding.GetTotalSpaceAlong<Orient_Vertical>();
        SavedSetsRowHeight = FMath::Max(1.0, (PanelExtent - HeadingHeight
            - (FCIInventorySavedSetRows::VisibleCount - 1) * Theme.RowGap) / FCIInventorySavedSetRows::VisibleCount);
        SavedSetsCanvas.Reset(NewObject<UCanvasPanel>());
        auto* Heading = NewObject<UTextBlock>(SavedSetsCanvas.Get());
        Heading->SetText(SavedSetsLabel(Section));
        Heading->SetFont(Theme.GroupLabelFont);
        Heading->SetColorAndOpacity(FSlateColor(Theme.GroupLabelTint));
        SavedSetsHeadingSlot = SavedSetsCanvas->AddChildToCanvas(Heading);
        SavedSetsHeadingSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));

        auto* Body = NewObject<UCanvasPanel>(SavedSetsCanvas.Get());
        Body->SetClipping(EWidgetClipping::ClipToBounds);
        SavedSetsBodySlot = SavedSetsCanvas->AddChildToCanvas(Body);
        SavedSetsBodySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        auto* SurfaceTheme = Theme.CreateProjectedSurfaceTheme(Body);

        SavedSetTiles.Reset();
        for (const auto& Row : SavedSetsPresentation.Rows.GetRows())
        {
            auto* Tile = NewObject<UCFTile>(Body);
            Tile->Theme = SurfaceTheme;
            auto* Label = NewObject<UTextBlock>(Tile);
            Label->SetText(Row.bCreate ? NSLOCTEXT("Inventory", "NewGearSet", "Create")
                : FText::AsCultureInvariant(Row.Set.Name));
            Label->SetFont(Theme.LabelFont);
            Label->SetColorAndOpacity(FSlateColor(Theme.TextTint));
            auto* Inset = NewObject<UBorder>(Tile);
            Inset->SetBrushColor(FLinearColor::Transparent);
            Inset->SetPadding(FMargin(Theme.Spacing, 0.f));
            Inset->SetVerticalAlignment(VAlign_Center);
            Inset->SetContent(Label);
            Tile->SetContent(Inset);
            auto* Slot = Body->AddChildToCanvas(Tile);
            Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
            Slot->SetZOrder(Row.Index == INDEX_NONE ? 0 : 1);
            SavedSetTiles.Add(Tile);
        }

        auto* Track = NewObject<UImage>(SavedSetsCanvas.Get());
        auto* Thumb = NewObject<UImage>(SavedSetsCanvas.Get());
        const auto& White = *FCoreStyle::Get().GetBrush("WhiteBrush");
        Track->SetBrush(White);
        Thumb->SetBrush(White);
        Track->SetColorAndOpacity(Theme.Scrollbar.TrackTint);
        Thumb->SetColorAndOpacity(Theme.Scrollbar.ThumbTint);
        SavedSetsTrackSlot = SavedSetsCanvas->AddChildToCanvas(Track);
        SavedSetsThumbSlot = SavedSetsCanvas->AddChildToCanvas(Thumb);
        SavedSetsThumbSlot->SetZOrder(1);

        SavedSetsTheme = &Theme;
        SavedSetsHeight = PanelExtent;
    }

    void LayoutSavedSetRows(double Now, const UCIInventoryTileTheme& Theme)
    {
        const double Pitch = SavedSetsRowHeight + Theme.RowGap;
        const auto& Rows = SavedSetsPresentation.Rows.GetRows();
        for (int32 Index = 0; Index < Rows.Num(); ++Index)
        {
            const auto Frame = SavedSetsPresentation.Rows.Sample(Rows[Index], Now);
            const double Y = Frame.Position * Pitch;
            auto* Tile = SavedSetTiles[Index];
            const bool bVisible = Frame.Opacity > 0.0 && Y + SavedSetsRowHeight > 0.0
                && Frame.Position < FCIInventorySavedSetRows::VisibleCount;
            Tile->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
            if (bVisible)
            {
                CastChecked<UCanvasPanelSlot>(Tile->Slot)->SetOffsets(FMargin(0.f, Y, 0.f, SavedSetsRowHeight));
                Tile->SetVisualState(Frame.Focus, Frame.Opacity);
            }
        }
        SavedSetsPaintedAt = Now;
    }

    void LayoutSavedSetsCanvas(const UCIInventoryTileTheme& Theme, const FCIInventorySidebarGeometry& Sidebar,
        const FInventoryShellLayout& Layout, double CanvasWidth)
    {
        const double CanvasScale = CanvasWidth / Sidebar.GetFullWidth();
        const double ContentWidth = Sidebar.Width * CanvasScale;
        const double Gap = CanvasWidth - ContentWidth;
        const FMargin Padding = Theme.GroupLabelPadding;
        const double Heading = Theme.GroupLabelHeight + Padding.GetTotalSpaceAlong<Orient_Vertical>();
        SavedSetsHeadingSlot->SetOffsets(FMargin(Padding.Left, Padding.Top, Padding.Right + Gap, Theme.GroupLabelHeight));
        SavedSetsBodySlot->SetOffsets(FMargin(0.f, Heading, Gap, 0.f));

        const auto& Rows = SavedSetsPresentation.Rows;
        const int32 Total = Rows.GetRowCount();
        const int32 Visible = FCIInventorySavedSetRows::VisibleCount;
        const bool bOverflow = Total > Visible && Gap > 0.0;
        const auto Visibility = bOverflow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
        SavedSetsTrackSlot->Content->SetVisibility(Visibility);
        SavedSetsThumbSlot->Content->SetVisibility(Visibility);
        if (!bOverflow)
        {
            return;
        }

        // Place the center halfway between the projected edges, even under perspective.
        const auto RightEdge = Layout.MainOrigin;
        const auto LeftEdge = RightEdge - FVector2D(Sidebar.GetFullWidth() - Sidebar.Width, 0.0);
        const auto Center = Layout.Left.Unproject((Layout.Left.Project(LeftEdge) + Layout.Left.Project(RightEdge)) * .5);
        const double CenterX = ContentWidth + (Center.X - LeftEdge.X) * CanvasScale;
        const double Width = FMath::Min(FMath::Max(.5f, Theme.Scrollbar.Width),
            2.0 * FMath::Min(CenterX - ContentWidth, CanvasWidth - CenterX));
        const double X = CenterX - Width * .5;
        const double TrackHeight = Visible * SavedSetsRowHeight + (Visible - 1) * Theme.RowGap;
        const double ThumbHeight = TrackHeight * double(Visible) / Total;
        const double Scroll = FMath::Clamp(double(Rows.GetFirstIndex()) / (Total - Visible), 0.0, 1.0);
        SavedSetsTrackSlot->SetOffsets(FMargin(X, Heading, Width, TrackHeight));
        SavedSetsThumbSlot->SetOffsets(FMargin(X, Heading + (TrackHeight - ThumbHeight) * Scroll, Width, ThumbHeight));
    }

    FInventoryShellLayout GetLayout(const FVector2D& Size) const
    {
        const FVector2D MenuSize = MenuTexture.IsValid()
            ? FVector2D(MenuTexture->SizeX, MenuTexture->SizeY) : FVector2D::ZeroVector;
        return LayoutFor(Size, MenuSize, MenuPanelGap.Get(), PanelHeight.Get(), FooterGap.Get());
    }

    FCIInventorySidebarGeometry GetSidebarGeometry(const FInventoryShellLayout& Layout, double ViewportWidth) const
    {
        return {Layout.MainExtent.X, ViewportWidth, InventoryTheme.Get()->TileGap * Layout.Scale,
            SavedSetsPresentation.Reveal.GetProgress()};
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
        const FVector2D& Extent, double Start = 0.0, double End = 1.0, double Opacity = 1.0,
        const FBox2D* Source = nullptr, double Lift = 0.0) const
    {
        const uint8 Fade = FMath::RoundToInt(255.0 * Opacity);
        // The render target already contains premultiplied pixels.
        const FColor Tint(Fade, Fade, Fade, Fade);
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        Vertices.Reserve((ProjectionStrips + 1) * 2);
        Indices.Reserve(ProjectionStrips * 6);
        for (int32 Strip = 0; Strip <= ProjectionStrips; ++Strip)
        {
            const double U = FMath::Lerp(Start, End, double(Strip) / ProjectionStrips);
            for (const float V : {0.f, 1.f})
            {
                const auto Point = Plane.Project(Origin + FVector2D(U * Extent.X, V * Extent.Y), Lift);
                const auto UV = Source ? Source->Min + FVector2D(U, V) * Source->GetSize() : FVector2D(U, V);
                Vertices.Add(FSlateVertex::Make(Geometry.GetAccumulatedRenderTransform(),
                    FVector2f(Point), FVector2f(UV), Tint));
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
    TAttribute<bool> ConfirmEquips;
    TAttribute<bool> ConfirmAvailable;
    FSimpleDelegate OnConfirmSavedSet;
    TAttribute<bool> CanOverwrite;
    TAttribute<bool> CanDelete;
    FSavedSetHoldAction OnSavedSetHold;
    TAttribute<bool> CanRename;
    FRenameSavedSet OnRenameSavedSet;
    FSimpleDelegate OnClose;
    TWeakPtr<SWindow> OptionsWindow;
    TStaticArray<FSavedSetHold, 2> Holds;
    TWeakObjectPtr<UCFMenu> Menu;
    TWeakObjectPtr<UWidgetSwitcher> MainPanel;
    TWeakObjectPtr<UWidget> MainContent;
    TWeakObjectPtr<UCFSpanGrid> NavigationGrid;
    TArray<FCIInventoryNavigationRow> NavigationRows;
    FIntPoint GridFocus = FIntPoint::ZeroValue;
    TMap<FName, TPair<FIntPoint, FString>> TabSelections;
    FString PendingSelectionId;
    FCIInventoryNavigationInput NavigationInput;
    TSharedPtr<SWidget> MenuWidget;
    TSharedPtr<SBox> MainRenderRoot;
    TSharedPtr<SCIInventoryActionFooter> FooterWidget;
    TStrongObjectPtr<UCanvasPanel> SavedSetsCanvas;
    UCanvasPanelSlot* SavedSetsHeadingSlot = nullptr;
    UCanvasPanelSlot* SavedSetsBodySlot = nullptr;
    UCanvasPanelSlot* SavedSetsTrackSlot = nullptr;
    UCanvasPanelSlot* SavedSetsThumbSlot = nullptr;
    TArray<UCFTile*> SavedSetTiles;
    TWeakObjectPtr<UCIInventoryTileTheme> SavedSetsTheme;
    FName SavedSetsSection;
    double SavedSetsHeight = 0.0;
    TStrongObjectPtr<UTextureRenderTarget2D> MenuTexture;
    TStrongObjectPtr<UTextureRenderTarget2D> MainTexture;
    TArray<FInventoryTileLift> LiftedTiles;
    static constexpr float InspectorAttributeIconSize = 15.f;
    FSlateRoundedBoxBrush InspectorAttributeIconBrush{FLinearColor::White, InspectorAttributeIconSize * .5f};
    FSlateRoundedBoxBrush InspectorBrush{FLinearColor::Transparent, 0.f};
    TSharedPtr<SBorder> InspectorWidget;
    TWeakObjectPtr<const UCIInventoryTile> InspectorTile;
    TOptional<FCFMagritteBinding> InspectorItem;
    TSharedPtr<SScrollBox> InspectorScroll;
    TStrongObjectPtr<UCIInspectorIdentityTile> InspectorIdentityTile;
    TSharedPtr<SWidget> InspectorHeader;
    float InspectorScrollRate = 0.f;
    bool bInspectorScrollDirty = false;
    TStrongObjectPtr<UTextureRenderTarget2D> InspectorTexture;
    TArray<FInspectorStroke> InspectorStrokes;
    TWeakObjectPtr<const UCIInventoryTileTheme> InspectorTheme;
    float InspectorScale = 0.f;
#if WITH_DEV_AUTOMATION_TESTS
    int32 InspectorPaintCount = 0;
#endif
    TStrongObjectPtr<UTextureRenderTarget2D> FooterTexture;
    TStrongObjectPtr<UTextureRenderTarget2D> SavedSetsTexture;
    FCIInventorySavedSetsPresentation SavedSetsPresentation;
    double SavedSetsPaintedAt = 0.0;
    bool bSavedSetsOpen = false;
    TArray<FCIInventorySavedSet> SavedSets;
    int32 SavedSetIndex = 0;
    float SavedSetsRowHeight = 0.f;
    FWidgetRenderer* Renderer = nullptr;
    TSharedPtr<SCIProjectedBlur> ProjectedBlur;
    FVector2D LastGridViewport = FVector2D::ZeroVector;
    double LastGridPixelScale = 0.0;
    double FirstMenuItemX = 0.0;
    int32 RenderedSelection = INDEX_NONE;
};

void UCIInventoryShell::SetActionSection(FName Section)
{
    if (ActionSection != Section)
    {
        SavedSetConfirm.Unbind();
        bConfirmEquips = false;
        if (Shell)
        {
            Shell->SwitchSectionNavigation(Section);
        }
    }
    ActionSection = Section;
}

void UCIInventoryShell::SetSavedSets(TArray<FCIInventorySavedSet> Sets)
{
    SavedSets = MoveTemp(Sets);
    if (Shell)
    {
        Shell->SetSavedSets(SavedSets);
    }
}

void UCIInventoryShell::SetCreateSavedSetAction(TFunction<void(bool)> Action)
{
    CreateSavedSet = MoveTemp(Action);
}

void UCIInventoryShell::SetEquipSavedSetAction(TFunction<void(FString)> Action)
{
    EquipSavedSet = MoveTemp(Action);
}

void UCIInventoryShell::SetOverwriteSavedSetAction(TFunction<void(FString)> Action)
{
    OverwriteSavedSet = MoveTemp(Action);
}

void UCIInventoryShell::SetDeleteSavedSetAction(TFunction<void(FString)> Action)
{
    DeleteSavedSet = MoveTemp(Action);
}

void UCIInventoryShell::SetRenameSavedSetAction(TFunction<bool(FString, FString)> Action)
{
    RenameSavedSet = MoveTemp(Action);
}

void UCIInventoryShell::CloseSavedSetOptions()
{
    if (Shell)
    {
        Shell->CloseSavedSetOptions();
    }
}

void UCIInventoryShell::SetCloseAction(FSimpleDelegate Action)
{
    CloseAction = MoveTemp(Action);
    if (Shell)
    {
        Shell->SetCloseAction(CloseAction);
    }
}

void UCIInventoryShell::RestoreSavedSetsState(const UCIInventoryShell& Previous)
{
    RestoredTabSelections = Previous.Shell ? Previous.Shell->GetTabSelections() : Previous.RestoredTabSelections;
    if (ActionSection != Previous.ActionSection)
    {
        if (Shell)
        {
            Shell->RestoreNavigation(FIntPoint::ZeroValue, RestoredTabSelections, nullptr);
        }
        return;
    }
    RestoredGridFocus = Previous.Shell ? Previous.Shell->GetGridFocus() : Previous.RestoredGridFocus;
    RestoredNavigationInput = Previous.Shell ? MakeShared<FCIInventoryNavigationInput>(Previous.Shell->GetNavigationInput())
        : Previous.RestoredNavigationInput;
    bRestoreSavedSetsOpen = Previous.Shell ? Previous.Shell->IsSavedSetsOpen() : Previous.bRestoreSavedSetsOpen;
    RestoredSavedSetIndex = Previous.Shell ? Previous.Shell->GetSavedSetIndex() : Previous.RestoredSavedSetIndex;
    RestoredSavedSetsPresentation = Previous.Shell ? MakeShared<FCIInventorySavedSetsPresentation>(Previous.Shell->GetSavedSetsPresentation())
        : Previous.RestoredSavedSetsPresentation;
    RestoredOverwriteConfirmation.Reset();
    RestoredDeleteConfirmation = bRestoreSavedSetsOpen && Previous.Shell
        ? Previous.Shell->CompletedHold(ESavedSetHold::Delete) : TOptional<FCFControllerHold>();
    if (bRestoreSavedSetsOpen && Previous.Shell && SavedSets.IsValidIndex(RestoredSavedSetIndex)
        && Previous.Shell->SelectedSavedSet()
        && SavedSets[RestoredSavedSetIndex].Identifier == Previous.Shell->SelectedSavedSet()->Identifier)
    {
        RestoredOverwriteConfirmation = Previous.Shell->CompletedHold(ESavedSetHold::Overwrite);
    }
    if (Shell)
    {
        Shell->RestoreNavigation(RestoredGridFocus, RestoredTabSelections, RestoredNavigationInput);
        Shell->SetSavedSetsState(bRestoreSavedSetsOpen, RestoredSavedSetIndex);
        Shell->RestoreSavedSetsPresentation(RestoredSavedSetsPresentation);
        Shell->RestoreHoldConfirmation(ESavedSetHold::Overwrite, RestoredOverwriteConfirmation);
        Shell->RestoreHoldConfirmation(ESavedSetHold::Delete, RestoredDeleteConfirmation);
    }
}

#if WITH_DEV_AUTOMATION_TESTS
FIntPoint UCIInventoryShell::GetFocusedGridCellForTest() const
{
    return Shell ? Shell->GetGridFocus() : RestoredGridFocus;
}

int32 UCIInventoryShell::GetInspectorStrokeCountForTest() const
{
    return Shell ? Shell->GetInspectorStrokeCount() : 0;
}

TPair<bool, int32> UCIInventoryShell::GetInspectorScrollbarStateForTest() const
{
    return Shell ? Shell->GetInspectorScrollbarState() : TPair<bool, int32>{false, 0};
}

FVector UCIInventoryShell::GetInspectorScrollStateForTest(FVector2D ViewSize) const
{
    return Shell ? Shell->GetInspectorScrollState(ViewSize) : FVector::ZeroVector;
}

double UCIInventoryShell::GetGridTileLiftForTest(FIntPoint Cell) const
{
    return Shell ? Shell->GetGridTileLift(Cell) : 0.0;
}

FText UCIInventoryShell::GetInspectorLabelForTest() const
{
    return Shell ? Shell->GetInspectorLabel() : FText::GetEmpty();
}
#endif

void UCIInventoryShell::SetSavedSetConfirmAction(bool bEquip, FSimpleDelegate Action)
{
    bConfirmEquips = bEquip;
    SavedSetConfirm = MoveTemp(Action);
}

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
        })
        .ConfirmEquips_Lambda([this] { return bConfirmEquips || (Shell && Shell->SelectedSavedSet()); })
        .CanOverwrite_Lambda([this] { return static_cast<bool>(OverwriteSavedSet); })
        .CanDelete_Lambda([this] { return static_cast<bool>(DeleteSavedSet); })
        .CanRename_Lambda([this] { return static_cast<bool>(RenameSavedSet); })
        .OnClose(CloseAction)
        .OnRenameSavedSet(FRenameSavedSet::CreateWeakLambda(this, [this](FString Identifier, FString Name) {
            return RenameSavedSet && RenameSavedSet(MoveTemp(Identifier), MoveTemp(Name));
        }))
        .OnSavedSetHold(FSavedSetHoldAction::CreateWeakLambda(this, [this](ESavedSetHold Action, FString Identifier) {
            const auto& Callback = Action == ESavedSetHold::Overwrite ? OverwriteSavedSet : DeleteSavedSet;
            if (Callback)
            {
                Callback(MoveTemp(Identifier));
            }
        }))
        .ConfirmAvailable_Lambda([this] {
            if (SavedSetConfirm.IsBound() || bConfirmEquips || !Shell)
            {
                return SavedSetConfirm.IsBound();
            }
            const auto* Selected = Shell->SelectedSavedSet();
            return Selected ? EquipSavedSet && !Selected->Identifier.IsEmpty() : CreateSavedSet && SavedSets.Num() < 32;
        })
        .OnConfirmSavedSet(FSimpleDelegate::CreateWeakLambda(this, [this] {
            if (SavedSetConfirm.ExecuteIfBound())
            {
                return;
            }
            if (const auto* Selected = Shell->SelectedSavedSet())
            {
                EquipSavedSet(Selected->Identifier);
            }
            else
            {
                CreateSavedSet(ActionSection == ApparelSection);
            }
        }));
    UpdateContent();
    Shell->SetSavedSets(SavedSets);
    Shell->RestoreNavigation(RestoredGridFocus, RestoredTabSelections, RestoredNavigationInput);
    Shell->SetSavedSetsState(bRestoreSavedSetsOpen, RestoredSavedSetIndex);
    Shell->RestoreSavedSetsPresentation(RestoredSavedSetsPresentation);
    Shell->RestoreHoldConfirmation(ESavedSetHold::Overwrite, RestoredOverwriteConfirmation);
    Shell->RestoreHoldConfirmation(ESavedSetHold::Delete, RestoredDeleteConfirmation);
    return Shell.ToSharedRef();
}

void UCIInventoryShell::ReleaseSlateResources(bool bReleaseChildren)
{
    CloseSavedSetOptions();
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
