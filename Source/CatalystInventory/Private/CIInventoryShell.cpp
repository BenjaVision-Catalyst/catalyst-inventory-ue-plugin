#include "CIInventoryShell.h"
#include "CIInventoryRowData.h"
#include "CIResourceTile.h"
#include "CIInventoryAttributeIcon.h"

#include "CIApparelRow.h"
#include "CIEquipmentTile.h"
#include "CIGearRow.h"
#include "CIWeaponToolRow.h"
#include "Widgets/SOverlay.h"
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

bool HasNavigableGrid(FName Section)
{
    return HasSavedSets(Section) || Section == ResourcesSection;
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
    bool bClipToGrid = false;
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
        SLATE_ATTRIBUTE(bool, SlotPickerOpen)
        SLATE_ATTRIBUTE(bool, SlotPickerAvailable)
        SLATE_ATTRIBUTE(bool, SlotChoiceAvailable)
        SLATE_ATTRIBUTE(bool, ResourceOpen)
        SLATE_ATTRIBUTE(bool, ResourceAvailable)
        SLATE_ATTRIBUTE(bool, ResourceCanSplit)
        SLATE_ATTRIBUTE(bool, ResourceCanJoin)
        SLATE_ATTRIBUTE(bool, ResourceCanDrop)
        SLATE_ATTRIBUTE(FCFControllerHoldIndicator, ResourceDropIndicator)
        SLATE_ATTRIBUTE(bool, RadialsAvailable)
        SLATE_ATTRIBUTE(bool, RadialsOpen)
        SLATE_ATTRIBUTE(bool, RadialsCanPlace)
        SLATE_ATTRIBUTE(bool, RadialsCanClear)
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
        SlotPickerOpen = Args._SlotPickerOpen;
        SlotPickerAvailable = Args._SlotPickerAvailable;
        SlotChoiceAvailable = Args._SlotChoiceAvailable;
        ResourceOpen = Args._ResourceOpen;
        ResourceAvailable = Args._ResourceAvailable;
        ResourceCanSplit = Args._ResourceCanSplit;
        ResourceCanJoin = Args._ResourceCanJoin;
        ResourceCanDrop = Args._ResourceCanDrop;
        ResourceDropIndicator = Args._ResourceDropIndicator;
        RadialsAvailable = Args._RadialsAvailable;
        RadialsOpen = Args._RadialsOpen;
        RadialsCanPlace = Args._RadialsCanPlace;
        RadialsCanClear = Args._RadialsCanClear;
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
    bool IsOverResourceDrop(const FVector2D& Position) const { return ResourceDropBounds.ContainsPoint(Position); }
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
        ResourceDropBounds = FSlateRect(0, 0, 0, 0);
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

        DrawPrompt(SlotPickerOpen.Get() || ResourceOpen.Get() ? NSLOCTEXT("Inventory", "SlotPickerBack", "Back")
            : NSLOCTEXT("Inventory", "CloseActionLabel", "Close"), EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right);
        CloseBounds = FSlateRect(Right + ActionPromptGap, 0.0, Geometry.GetLocalSize().X, 25.0);
        if (SlotPickerOpen.Get())
        {
            const double ConfirmRight = Right;
            DrawPrompt(NSLOCTEXT("Inventory", "EquipActionLabel", "Equip"), EKeys::Enter,
                EKeys::Gamepad_FaceButton_Bottom, SlotChoiceAvailable.Get());
            ConfirmBounds = FSlateRect(Right + ActionPromptGap, 0.0, ConfirmRight, 25.0);
            return Layer + 2;
        }
        const FText SectionAction = bResources
            ? NSLOCTEXT("Inventory", "ResourceCategories", "Categories") : SavedSetsLabel(CurrentSection);
        const double ActionRight = Right;
        const bool bResourceSelected = bResources && ResourceOpen.Get();
        DrawPrompt(SectionAction, bResources ? EKeys::C : EKeys::G, EKeys::Gamepad_LeftTrigger, !bResourceSelected);
        if (HasNavigableGrid(CurrentSection) && !bResourceSelected)
        {
            SavedSetsBounds = FSlateRect(Right + ActionPromptGap, 0.0, ActionRight, 25.0);
        }
        if (bResources)
        {
            const bool bCanToggleRadials = RadialsAvailable.Get() && !bResourceSelected;
            const double RadialsRight = Right;
            DrawPrompt(NSLOCTEXT("Inventory", "ResourceRadials", "Radials"), EKeys::F2,
                EKeys::Gamepad_FaceButton_Left, bCanToggleRadials);
            if (bCanToggleRadials)
            {
                OptionsBounds = FSlateRect(Right + ActionPromptGap, 0.0, RadialsRight, 25.0);
            }
            Right = Geometry.GetLocalSize().X;
            Y += 28.0;
            if (RadialsOpen.Get())
            {
                const double ClearRight = Right;
                DrawPrompt(NSLOCTEXT("Inventory", "ClearResourceRadials", "Clear"), EKeys::Delete,
                    EKeys::Gamepad_RightThumbstick, RadialsCanClear.Get());
                DeleteBounds = FSlateRect(Right + ActionPromptGap, 25.0, ClearRight, FooterHeight);
                const double PlaceRight = Right;
                DrawPrompt(NSLOCTEXT("Inventory", "PlaceResourceRadials", "Place"), EKeys::Enter,
                    EKeys::Gamepad_FaceButton_Bottom, RadialsCanPlace.Get());
                ConfirmBounds = FSlateRect(Right + ActionPromptGap, 25.0, PlaceRight, FooterHeight);
                return Layer + 2;
            }
            const double SelectRight = Right;
            DrawPrompt(NSLOCTEXT("Inventory", "SelectResourceStack", "Select"), EKeys::Enter,
                EKeys::Gamepad_FaceButton_Bottom, ResourceAvailable.Get() && !SavedSetsOpen.Get() && !bResourceSelected);
            if (!bResourceSelected)
            {
                ConfirmBounds = FSlateRect(Right + ActionPromptGap, 25.0, SelectRight, FooterHeight);
            }
            const double DropRight = Right;
            const auto DropIndicator = ResourceDropIndicator.Get();
            DrawPrompt(NSLOCTEXT("Inventory", "ResourceDrop", "Drop"), EKeys::Delete, EKeys::Gamepad_RightThumbstick,
                ResourceCanDrop.Get() || DropIndicator.bAnimating, true, DropIndicator);
            ResourceDropBounds = FSlateRect(Right + ActionPromptGap, 25.0, DropRight, FooterHeight);
            if (bResourceSelected)
            {
                const double SplitRight = Right;
                DrawPrompt(NSLOCTEXT("Inventory", "SplitResource", "Split"), EKeys::S, EKeys::Gamepad_FaceButton_Top,
                    ResourceCanSplit.Get());
                OptionsBounds = FSlateRect(Right + ActionPromptGap, 25.0, SplitRight, FooterHeight);
                const double JoinRight = Right;
                DrawPrompt(NSLOCTEXT("Inventory", "JoinResource", "Join"), EKeys::F2, EKeys::Gamepad_FaceButton_Left,
                    ResourceCanJoin.Get());
                ConfirmBounds = FSlateRect(Right + ActionPromptGap, 25.0, JoinRight, FooterHeight);
            }
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
        else if (SlotPickerAvailable.Get())
        {
            const double ConfirmRight = Right;
            DrawPrompt(NSLOCTEXT("Inventory", "SelectSlotItem", "Select"), EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom);
            ConfirmBounds = FSlateRect(Right + ActionPromptGap, 0.0, ConfirmRight, 25.0);
        }
        return Layer + 2;
    }

private:
    TAttribute<FName> Section;
    TAttribute<bool> SavedSetsOpen;
    TAttribute<bool> SlotPickerOpen;
    TAttribute<bool> SlotPickerAvailable;
    TAttribute<bool> SlotChoiceAvailable;
    TAttribute<bool> ResourceOpen;
    TAttribute<bool> ResourceAvailable;
    TAttribute<bool> ResourceCanSplit;
    TAttribute<bool> ResourceCanJoin;
    TAttribute<bool> ResourceCanDrop;
    TAttribute<FCFControllerHoldIndicator> ResourceDropIndicator;
    mutable FSlateRect ResourceDropBounds;
    TAttribute<bool> RadialsAvailable, RadialsOpen, RadialsCanPlace, RadialsCanClear;
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
                .SlotPickerOpen_Lambda([this] { return SlotPicker.IsSet(); })
                .SlotPickerAvailable_Lambda([this] { return SlotPickerSource && HasSavedSets(ActionSection.Get()); })
                .SlotChoiceAvailable_Lambda([this] { return SlotPicker && SlotPicker->Choices.IsValidIndex(SlotChoice); })
                .ResourceOpen_Lambda([this] { return ResourcePreviewTile.IsValid() && ResourceFrameHoldUntil == 0.0 && !IsResourceCommitted(); })
                .ResourceAvailable_Lambda([this] { return ResourceActions.Read && GetSelectedTile() && !ResourceDropRequest.IsValid(); })
                .ResourceCanSplit_Lambda([this] { return CanCommitResource(false); })
                .ResourceCanJoin_Lambda([this] { return CanCommitResource(true); })
                .ResourceCanDrop_Lambda([this] { return GetResourceDropTarget().IsSet(); })
                .ResourceDropIndicator_Lambda([this] { return ResourceDropHold.GetIndicator(ResourceDropClock); })
                .RadialsAvailable_Lambda([this] { return ResourceActions.UpdateRadials && ResourceActions.SetRadialsOpen; })
                .RadialsOpen_Lambda([this] { return bResourceRadialsOpen; })
                .RadialsCanPlace_Lambda([this] { return ResourceRadialAvailability.bCanPlace; })
                .RadialsCanClear_Lambda([this] { return ResourceRadialAvailability.bCanClear; })
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
    void SetSlotPickerSource(FCIInventorySlotPickerSource Source) { SlotPickerSource = MoveTemp(Source); }
    void SetResourceActions(FCIInventoryResourceActions Actions) { ResourceActions = MoveTemp(Actions); }
    bool HasResourceInteraction() const { return ResourceDropRequest.IsValid() || (ResourcePreviewTile.IsValid() && ResourceFrameHoldUntil == 0.0); }

    void SetResourceRadialsOpen(bool bOpen)
    {
        if (bResourceRadialsOpen == bOpen)
        {
            return;
        }
        bResourceRadialsOpen = bOpen;
        ResourceRadialInput.bPlace = ResourceRadialInput.bClear = false;
        ResourceRadialAvailability = {};
        NavigationInput = {};
        RightStickScroll = 0.f;
        if (ResourceActions.SetRadialsOpen)
        {
            ResourceActions.SetRadialsOpen(bOpen);
        }
    }

    bool AreResourceRadialsOpen() const { return bResourceRadialsOpen; }
    bool IsResourceCommitted() const { return ResourceResult.IsSet() && ResourceActions.bPublishesAfterLanding; }
    FKey GetResourceFollowupKey() const { return ResourceFollowupKey; }

    bool AcceptResourceFollowup(FKey Key)
    {
        if (!IsResourceCommitted())
        {
            return false;
        }
        if (ResourceFollowupKey.IsValid())
        {
            return true;
        }
        ResourceFollowupKey = Key;
        ResourceLandingId = ResourceResult->Destination;
        // Release publication immediately. The rebuilt grid will dispatch the input against the committed stack.
        ResourceFrameHoldUntil = FPlatformTime::Seconds() + .5;
        return true;
    }

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

    TMap<FName, TPair<FIntPoint, FString>> GetTabSelections(bool bPreferResourceLanding = false) const
    {
        auto Selections = TabSelections;
        if (bPreferResourceLanding && !ResourceLandingId.IsEmpty())
        {
            Selections.Add(ResourcesSection, {GridFocus, ResourceLandingId});
        }
        else if (const auto* Tile = GetSelectedTile())
        {
            const auto& Item = Tile->GetInspectorItem();
            Selections.Add(ActionSection.Get(), {GridFocus,
                Item ? FString(UTF8_TO_TCHAR(Item->ObjectId().c_str())) : FString()});
        }
        return Selections;
    }

    void SwitchSectionNavigation(FName Section)
    {
        ReleaseResourceDrop(ResourceDropKey);
        SetResourceRadialsOpen(false);
        TabSelections = GetTabSelections();
        SlotPicker.Reset();
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

    int32 GetResourceReflowTileCount() const
    {
        return LiftedTiles.FilterByPredicate([](const auto& Tile) { return Tile.bClipToGrid; }).Num();
    }

    double GetGridTileLift(FIntPoint Cell) const
    {
        const auto* Tile = LiftedTiles.FindByPredicate([Cell](const auto& Entry) { return Entry.Cell == Cell; });
        return Tile ? Tile->Depth : 0.0;
    }
#endif

    const UCIInventoryTile* GetSelectedTile() const
    {
        if (HasNavigableGrid(ActionSection.Get()) && NavigationRows.IsValidIndex(GridFocus.Y))
        {
            const auto& Cells = NavigationRows[GridFocus.Y].Cells;
            return Cells.IsValidIndex(GridFocus.X) ? Cells[GridFocus.X].Tile.Get() : nullptr;
        }
        return nullptr;
    }

    const UCIInventoryTile* GetInspectedTile() const
    {
        if (SlotPicker && SlotPickerRows.IsValidIndex(SlotChoice))
        {
            return SlotPickerRows[SlotChoice].Get();
        }
        return GetSelectedTile();
    }

    FText GetInspectorLabel() const
    {
        const auto* Tile = GetInspectedTile();
        return Tile ? Tile->GetItemLabel() : FText::GetEmpty();
    }
    const FCIInventoryNavigationInput& GetNavigationInput() const { return NavigationInput; }

    FString GetLiftedResourceLandingId() const
    {
        return ResourceFrameHoldUntil > 0.0
            ? ResourceLandingId : FString();
    }

    void RestoreNavigation(FIntPoint Focus, const TMap<FName, TPair<FIntPoint, FString>>& Selections,
        const TSharedPtr<const FCIInventoryNavigationInput>& Input, const FString& LiftedResourceId = {},
        FKey FollowupKey = {})
    {
        PendingLiftedResourceId = LiftedResourceId;
        PendingResourceFollowupKey = FollowupKey;
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

    TOptional<float> GetResourceScrollOffset() const
    {
        const auto* Grid = NavigationGrid.Get();
        return ActionSection.Get() == ResourcesSection && Grid
            ? TOptional<float>(Grid->GetScrollOffset()) : ResourceScrollToRestore;
    }

    void RestoreResourceScrollOffset(TOptional<float> Offset)
    {
        ResourceScrollToRestore = Offset;
        if (ActionSection.Get() == ResourcesSection)
        {
            if (auto* Grid = NavigationGrid.Get(); Grid && ResourceScrollToRestore)
            {
                Grid->ScrollWidgetIntoView(nullptr);
                Grid->SetScrollOffset(*ResourceScrollToRestore);
                ResourceScrollToRestore.Reset();
            }
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
        const bool bConfirmSlot = Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom;
        const bool bBackSlot = Key == EKeys::BackSpace || Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right;
        const bool bRadials = Key == EKeys::F2 || Key == EKeys::Gamepad_FaceButton_Left;
        if (bResourceRadialsOpen)
        {
            if (bBackSlot || bRadials)
            {
                if (!Event.IsRepeat()) { SetResourceRadialsOpen(false); }
                return FReply::Handled();
            }
            if (bConfirmSlot || Key == EKeys::Delete || Key == EKeys::Gamepad_RightThumbstick)
            {
                if (!Event.IsRepeat())
                {
                    ResourceRadialInput.bPlace |= bConfirmSlot && ResourceRadialAvailability.bCanPlace;
                    ResourceRadialInput.bClear |= !bConfirmSlot && ResourceRadialAvailability.bCanClear;
                }
                return FReply::Handled();
            }
        }
        if (ActionSection.Get() == ResourcesSection && !bResourceRadialsOpen
            && (Key == EKeys::Delete || Key == EKeys::Gamepad_RightThumbstick))
        {
            if (!Event.IsRepeat()) { BeginResourceDrop(Key); }
            return FReply::Handled();
        }
        if (ResourceDropRequest.IsValid())
        {
            return FReply::Handled();
        }
        if (!Event.IsRepeat() && IsResourceCommitted()
            && (bConfirmSlot || bBackSlot || Key == EKeys::Tab || Key == EKeys::Gamepad_Special_Right
                || bRadials || Key == EKeys::C || Key == EKeys::Gamepad_LeftTrigger || Key == EKeys::Q || Key == EKeys::E
                || Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder
                || NavigationDirection(Key) != FIntPoint::ZeroValue))
        {
            AcceptResourceFollowup(Key);
            return FReply::Handled();
        }
        if (HasResourceInteraction())
        {
            if (bBackSlot || Key == EKeys::Tab || Key == EKeys::Gamepad_Special_Right)
            {
                if (!ResourceRequest.IsValid() && !ResourceResult)
                {
                    bResourceClosing = true;
                }
                return FReply::Handled();
            }
            if (Key == EKeys::S || Key == EKeys::Gamepad_FaceButton_Top)
            {
                if (!Event.IsRepeat()) { CommitResource(false); }
                return FReply::Handled();
            }
            if (Key == EKeys::F2 || Key == EKeys::Gamepad_FaceButton_Left)
            {
                if (!Event.IsRepeat()) { CommitResource(true); }
                return FReply::Handled();
            }
            const FIntPoint Direction = NavigationDirection(Key);
            if (Key.IsGamepadKey() && Direction != FIntPoint::ZeroValue)
            {
                NavigationInput.SetPad(Direction, true);
                MoveNavigation(NavigationInput.Step(FPlatformTime::Seconds()));
            }
            else
            {
                ChangeResourceQuantity(Direction);
            }
            return FReply::Handled();
        }
        if (ActionSection.Get() == ResourcesSection && bRadials
            && ResourceActions.UpdateRadials && ResourceActions.SetRadialsOpen)
        {
            if (!Event.IsRepeat()) { SetResourceRadialsOpen(true); }
            return FReply::Handled();
        }
        if (ActionSection.Get() == ResourcesSection && !bSavedSetsOpen && bConfirmSlot
            && !Event.IsRepeat() && OpenResourcePreview())
        {
            return FReply::Handled();
        }
        if (SlotPicker && bBackSlot)
        {
            SlotPicker.Reset();
            return FReply::Handled();
        }
        if (SlotPicker && (Key == EKeys::Tab || Key == EKeys::Gamepad_Special_Right))
        {
            SlotPicker.Reset();
        }
        if (SlotPicker && bConfirmSlot)
        {
            if (!Event.IsRepeat()) { ConfirmSlotChoice(); }
            return FReply::Handled();
        }
        if (SlotPicker && (Key == EKeys::G || Key == EKeys::Q || Key == EKeys::E
            || Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Gamepad_RightShoulder
            || Key == EKeys::Gamepad_LeftTrigger))
        {
            return FReply::Handled();
        }
        if (!SlotPicker && !bSavedSetsOpen && bConfirmSlot && !Event.IsRepeat() && OpenSlotPicker())
        {
            return FReply::Handled();
        }
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
        if (ActionSection.Get() == ResourcesSection && bSavedSetsOpen && bConfirmSlot)
        {
            if (!Event.IsRepeat())
            {
                SetSavedSetsState(false, SavedSetIndex);
            }
            return FReply::Handled();
        }
        if (Key == EKeys::Gamepad_LeftTrigger)
        {
            NavigationInput.bTriggerButton = true;
            return UpdateNavigationTrigger() ? FReply::Handled() : FReply::Unhandled();
        }
        if (HasNavigableGrid(ActionSection.Get()))
        {
            if (Key == EKeys::C && ActionSection.Get() == ResourcesSection)
            {
                if (!Event.IsRepeat())
                {
                    ToggleResourceCategories();
                }
                return FReply::Handled();
            }
            if (Key == EKeys::G && HasSavedSets(ActionSection.Get()))
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
                return HasNavigableGrid(ActionSection.Get()) ? FReply::Handled() : FReply::Unhandled();
            }
        }
        return (ReleaseResourceDrop(Key) || ReleaseHoldInput(Key)) ? FReply::Handled() : FReply::Unhandled();
    }

    FReply OnAnalogValueChanged(const FGeometry&, const FAnalogInputEvent& Event) override
    {
        const FKey Key = Event.GetKey();
        if (Key == EKeys::Gamepad_LeftX) { ResourceRadialInput.Sticks[0].X = Event.GetAnalogValue(); }
        else if (Key == EKeys::Gamepad_LeftY) { ResourceRadialInput.Sticks[0].Y = Event.GetAnalogValue(); }
        else if (Key == EKeys::Gamepad_RightX) { ResourceRadialInput.Sticks[1].X = Event.GetAnalogValue(); }
        // Radials use raw Slate stick directions, before viewport or camera transforms.
        else if (Key == EKeys::Gamepad_RightY) { ResourceRadialInput.Sticks[1].Y = Event.GetAnalogValue(); }
        if (bResourceRadialsOpen && (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY
            || Key == EKeys::Gamepad_RightX || Key == EKeys::Gamepad_RightY))
        {
            return FReply::Handled();
        }
        if (Key == EKeys::Gamepad_RightY)
        {
            const float Axis = Event.GetAnalogValue();
            const float Strength = FMath::Clamp((FMath::Abs(Axis) - .2f) / .8f, 0.f, 1.f);
            RightStickScroll = -FMath::Sign(Axis) * Strength;
            return HasNavigableGrid(ActionSection.Get()) ? FReply::Handled() : FReply::Unhandled();
        }
        if (Key == EKeys::Gamepad_LeftTriggerAxis)
        {
            if (SlotPicker) { return FReply::Handled(); }
            NavigationInput.TriggerAxis = Event.GetAnalogValue();
            return UpdateNavigationTrigger() ? FReply::Handled() : FReply::Unhandled();
        }
        if (Key == EKeys::Gamepad_LeftX || Key == EKeys::Gamepad_LeftY)
        {
            if (HasResourceInteraction())
            {
                if (Key == EKeys::Gamepad_LeftX) { ResourceStickX = Event.GetAnalogValue(); }
                else { ResourceStickY = Event.GetAnalogValue(); }
                return FReply::Handled();
            }
            NavigationInput.SetStickAxis(Key == EKeys::Gamepad_LeftX, Event.GetAnalogValue());
            return HasNavigableGrid(ActionSection.Get()) ? FReply::Handled() : FReply::Unhandled();
        }
        return FReply::Unhandled();
    }

    void OnFocusLost(const FFocusEvent&) override
    {
        ReleaseResourceDrop(ResourceDropKey);
        RightStickScroll = 0.f;
        ResourceStickX = ResourceStickY = 0.f;
        NavigationInput = {};
        for (const auto Action : SavedSetHoldActions)
        {
            Hold(Action).Inputs.Reset();
        }
        CancelSavedSetHolds();
    }

    FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (HasResourceInteraction())
        {
            return FReply::Handled();
        }
        if (SlotPicker && !OptionsWindow.IsValid())
        {
            const auto Layout = GetLayout(Geometry.GetLocalSize());
            const FBox2D Bounds(Layout.MainOrigin, Layout.MainOrigin + Layout.MainExtent);
            const auto Point = Layout.Left.Unproject(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
            if (Bounds.IsInside(Point) && SlotPicker->Choices.Num() > VisibleSlotChoices(Layout))
            {
                const float Delta = Event.GetWheelDelta();
                if (!FMath::IsNearlyZero(Delta))
                {
                    MoveNavigation({0, Delta > 0.f ? -1 : 1});
                }
                return FReply::Handled();
            }
        }
        if (ActionSection.Get() == ResourcesSection && NavigationGrid.IsValid())
        {
            const auto Layout = GetLayout(Geometry.GetLocalSize());
            const auto Point = Layout.Left.Unproject(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
            const FBox2D Bounds(Layout.MainOrigin, Layout.MainOrigin + Layout.MainExtent);
            if (Bounds.IsInside(Point))
            {
                ScrollResources(-Event.GetWheelDelta() * 64.f);
                return FReply::Handled();
            }
        }
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
            if (bResourceRadialsOpen)
            {
                if (FooterWidget->IsOverOptions(FooterPoint) || FooterWidget->IsOverClose(FooterPoint))
                {
                    SetResourceRadialsOpen(false);
                    return FReply::Handled();
                }
                if (FooterWidget->IsOverConfirm(FooterPoint))
                {
                    ResourceRadialInput.bPlace |= ResourceRadialAvailability.bCanPlace;
                    return FReply::Handled();
                }
                if (FooterWidget->IsOverHold(ESavedSetHold::Delete, FooterPoint))
                {
                    ResourceRadialInput.bClear |= ResourceRadialAvailability.bCanClear;
                    return FReply::Handled();
                }
            }
            if (ActionSection.Get() == ResourcesSection && FooterWidget->IsOverResourceDrop(FooterPoint))
            {
                if (BeginResourceDrop(EKeys::LeftMouseButton))
                {
                    return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse).CaptureMouse(SharedThis(this));
                }
                return FReply::Handled();
            }
            if (ResourceDropRequest.IsValid())
            {
                return FReply::Handled();
            }
            if (IsResourceCommitted())
            {
                if (FooterWidget->IsOverConfirm(FooterPoint)) { AcceptResourceFollowup(EKeys::Enter); }
                else if (FooterWidget->IsOverClose(FooterPoint)) { AcceptResourceFollowup(EKeys::BackSpace); }
                else if (FooterWidget->IsOverSavedSets(FooterPoint)) { AcceptResourceFollowup(EKeys::C); }
                else if (FooterWidget->IsOverOptions(FooterPoint)) { AcceptResourceFollowup(EKeys::F2); }
                return FReply::Handled();
            }
            if (HasResourceInteraction())
            {
                if (FooterWidget->IsOverClose(FooterPoint) && !ResourceRequest.IsValid() && !ResourceResult)
                {
                    bResourceClosing = true;
                }
                else if (FooterWidget->IsOverOptions(FooterPoint)) { CommitResource(false); }
                else if (FooterWidget->IsOverConfirm(FooterPoint)) { CommitResource(true); }
                return FReply::Handled();
            }
            if (ActionSection.Get() == ResourcesSection && FooterWidget->IsOverOptions(FooterPoint)
                && ResourceActions.UpdateRadials && ResourceActions.SetRadialsOpen)
            {
                SetResourceRadialsOpen(true);
                return FReply::Handled();
            }
            if (ActionSection.Get() == ResourcesSection && !bSavedSetsOpen
                && FooterWidget->IsOverConfirm(FooterPoint) && OpenResourcePreview())
            {
                return FReply::Handled();
            }
            if (SlotPicker)
            {
                if (FooterWidget->IsOverClose(FooterPoint))
                {
                    SlotPicker.Reset();
                }
                else if (FooterWidget->IsOverConfirm(FooterPoint))
                {
                    ConfirmSlotChoice();
                }
                else
                {
                    const FBox2D Bounds(Layout.MainOrigin, Layout.MainOrigin + Layout.MainExtent);
                    const auto Point = (PlanePoint - Bounds.Min) / Layout.Scale;
                    const int32 Count = SlotPicker->Choices.Num();
                    const double EndX = Bounds.GetSize().X / Layout.Scale;
                    if (Count > VisibleSlotChoices(Layout) && Point.X >= EndX && Point.X <= EndX + SlotPickerScrollbarGutter
                        && Point.Y >= SlotPickerHeadingHeight && Point.Y <= Bounds.GetSize().Y / Layout.Scale)
                    {
                        const double Fraction = (Point.Y - SlotPickerHeadingHeight)
                            / (Bounds.GetSize().Y / Layout.Scale - SlotPickerHeadingHeight);
                        SlotChoice = FMath::Clamp(FMath::RoundToInt(Fraction * (Count - 1)), 0, Count - 1);
                    }
                    else if (Point.X >= 0.0 && Point.X < EndX && Point.Y >= SlotPickerHeadingHeight)
                    {
                        const int32 Row = FMath::FloorToInt((Point.Y - SlotPickerHeadingHeight) / (SlotPickerRowHeight + SlotPickerRowGap));
                        if (Point.Y - SlotPickerHeadingHeight - Row * (SlotPickerRowHeight + SlotPickerRowGap)
                            < SlotPickerRowHeight && Row < VisibleSlotChoices(Layout)
                            && SlotPicker->Choices.IsValidIndex(FirstSlotChoice + Row))
                        {
                            SlotChoice = FirstSlotChoice + Row;
                        }
                    }
                }
                return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
            }
            if (!bSavedSetsOpen && FooterWidget->IsOverConfirm(FooterPoint) && OpenSlotPicker())
            {
                return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
            }
            if (FooterWidget->IsOverClose(FooterPoint) && OnClose.ExecuteIfBound())
            {
                return FReply::Handled();
            }
            if (bSavedSetsOpen && SavedSetsTexture.IsValid() && SavedSetsPresentation.Reveal.GetProgress() == 1.0)
            {
                const auto Sidebar = GetSidebarGeometry(Layout, Geometry.GetLocalSize().X);
                const auto Point = (PlanePoint - Layout.MainOrigin + FVector2D(Sidebar.GetTravel(), 0))
                    / Layout.Scale;
                const auto& Theme = *InventoryTheme.Get();
                const double Heading = SidebarHeadingHeight(Theme);
                const double RowGap = SidebarRowGap(Theme);
                if (Point.X >= 0 && Point.X < Sidebar.Width / Layout.Scale && Point.Y >= Heading && Point.Y < SavedSetsHeight)
                {
                    const double RowPitch = SavedSetsRowHeight + RowGap;
                    for (const auto& Row : SavedSetsPresentation.Rows.GetRows())
                    {
                        const auto Frame = SavedSetsPresentation.Rows.Sample(Row, SavedSetsPaintedAt);
                        const double Y = Point.Y - Heading - Frame.Position * RowPitch;
                        if (Row.Index == INDEX_NONE || Frame.Opacity <= 0.0 || Y < 0.0 || Y >= SavedSetsRowHeight)
                        {
                            continue;
                        }
                        if (ActionSection.Get() == ResourcesSection)
                        {
                            SelectResourceCategory(Row.Index);
                            return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
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
            if (ActionSection.Get() == ResourcesSection)
            {
                if (FooterWidget->IsOverSavedSets(FooterPoint))
                {
                    ToggleResourceCategories();
                    return FReply::Handled();
                }
                const auto* Grid = NavigationGrid.Get();
                const auto GridWidget = Grid ? Grid->GetCachedWidget() : nullptr;
                const FBox2D Bounds(Layout.MainOrigin, Layout.MainOrigin + Layout.MainExtent);
                if (!bSavedSetsOpen && GridWidget && Bounds.IsInside(PlanePoint))
                {
                    for (int32 Y = 0; Y < NavigationRows.Num(); ++Y)
                    {
                        const auto& Cells = NavigationRows[Y].Cells;
                        for (int32 X = 0; X < Cells.Num(); ++X)
                        {
                            const auto* Tile = Cells[X].Tile.Get();
                            const auto Widget = Tile ? Tile->GetCachedWidget() : nullptr;
                            if (Widget && TileBounds(*Widget, *GridWidget, Layout.Scale).IsInside(PlanePoint - Bounds.Min))
                            {
                                GridFocus = {X, Y};
                                return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
                            }
                        }
                    }
                }
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

        }
        return FReply::Unhandled();
    }

    FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !HasMouseCapture())
        {
            return FReply::Unhandled();
        }
        ReleaseResourceDrop(EKeys::LeftMouseButton);
        ReleaseHoldInput(EKeys::LeftMouseButton);
        return FReply::Handled().ReleaseMouseCapture();
    }

    FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (FooterWidget && ResourceDropKey == EKeys::LeftMouseButton)
        {
            const auto Layout = GetLayout(Geometry.GetLocalSize());
            const auto PlanePoint = Layout.Left.Unproject(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
            if (!FooterWidget->IsOverResourceDrop((PlanePoint - Layout.FooterOrigin) / Layout.Scale))
            {
                ReleaseResourceDrop(EKeys::LeftMouseButton);
            }
            return FReply::Handled();
        }
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
        ReleaseResourceDrop(EKeys::LeftMouseButton);
        ReleaseHoldInput(EKeys::LeftMouseButton);
    }

    virtual void Tick(const FGeometry& Geometry, double, float DeltaTime) override
    {
        const double Now = FPlatformTime::Seconds();
        if (ResourceFrameHoldUntil > 0.0)
        {
            if (Now < ResourceFrameHoldUntil && ActionSection.Get() == ResourcesSection)
            {
                return;
            }
            ResetResourcePreview();
        }
        if (HasNavigableGrid(ActionSection.Get()) && !OptionsWindow.IsValid())
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
        SetNavigationGrid(HasNavigableGrid(ActionSection.Get()) ? Cast<UCFSpanGrid>(Content) : nullptr);
        if (ActionSection.Get() == ResourcesSection && RightStickScroll != 0.f && !OptionsWindow.IsValid() && !HasResourceInteraction())
        {
            const float Speed = 640.f * (1.f + 2.f * FMath::Square(RightStickScroll));
            ScrollResources(RightStickScroll * Speed * DeltaTime);
        }
        UpdateResourceDrop(DeltaTime);
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
        if (HasNavigableGrid(Section))
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
        if (!SlotPicker && SlotPickerWidget)
        {
            SlotPickerWidget.Reset();
        }
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
                    ProjectedGridWidth = (EndX - StartX) * PixelScale;
                    ProjectedGridX = [ProjectedEdges, StartX, PixelScale, StripWidth, Scale = Layout.Scale](double OutputX) {
                        const double ScreenX = StartX + OutputX / PixelScale;
                        // Invert the same piecewise-linear mapping used by the surface mesh.
                        const int32 Strip = FMath::Clamp(Algo::UpperBound(ProjectedEdges, ScreenX) - 1,
                            0, ProjectionStrips - 1);
                        const double Alpha = (ScreenX - ProjectedEdges[Strip])
                            / (ProjectedEdges[Strip + 1] - ProjectedEdges[Strip]);
                        return (Strip + Alpha) * StripWidth / Scale;
                    };
                    Grid->ApplyHorizontalProjection(ProjectedGridWidth, ProjectedGridX);
                }
                LastGridViewport = Size;
                LastGridPixelScale = PixelScale;
            }
            DrawToTexture(MainTexture, MainRenderRoot.ToSharedRef(), TargetSize, DeltaTime, PaintScale);
            UpdateLiftedTiles(Layout);
            TickResourcePreview(Layout, DeltaTime);
            UpdateResourceRadials(Layout, Size);
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
            if (SlotPicker)
            {
                const auto ExtentInPixels = Layout.MainExtent * ProjectionRasterScale;
                const FVector2D PickerSize(FMath::CeilToInt(ExtentInPixels.X), FMath::CeilToInt(ExtentInPixels.Y));
                if (!SlotPickerWidget || SlotPickerWidgetSize != PickerSize || RenderedSlotChoice != SlotChoice
                    || SlotPickerPaintScale != PaintScale)
                {
                    SlotPickerWidget = BuildSlotPicker(Layout);
                    SlotPickerWidgetSize = PickerSize;
                    RenderedSlotChoice = SlotChoice;
                    SlotPickerPaintScale = PaintScale;
                }
                DrawToTexture(SlotPickerTexture, SlotPickerWidget.ToSharedRef(), PickerSize, DeltaTime, PaintScale);
            }
            if (bSavedSetsOpen || SavedSetsPresentation.Reveal.GetProgress() > 0.0)
            {
                const auto Sidebar = GetSidebarGeometry(Layout, Size.X);
                const double Height = Extent.Y / Layout.Scale;
                auto* Theme = InventoryTheme.Get();
                const bool bCategories = Section == ResourcesSection;
                const bool bRowsChanged = bCategories
                    ? SavedSetsPresentation.Rows.Update(ResourceCategoryEntries, ResourceCategoryIndex, Now, false)
                    : SavedSetsPresentation.Rows.Update(SavedSets, SavedSetIndex, Now);
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
        if (NavigationGrid.IsValid() && PendingResourceFollowupKey.IsValid())
        {
            const FKey Key = PendingResourceFollowupKey;
            PendingResourceFollowupKey = FKey();
            const auto Direction = NavigationDirection(Key);
            if (Direction != FIntPoint::ZeroValue)
            {
                MoveNavigation(Direction);
            }
            else if (Key == EKeys::Gamepad_LeftTrigger)
            {
                ToggleResourceCategories();
            }
            else
            {
                const FKey ActionKey = Key == EKeys::Escape ? EKeys::BackSpace : Key;
                OnKeyDown(Geometry, FKeyEvent(ActionKey, FModifierKeysState(), 0, false, 0, 0));
            }
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
        if (SlotPicker && SlotPickerTexture.IsValid() && Layout.MainExtent.GetMin() > 0.0)
        {
            const FBox2D Bounds(Layout.MainOrigin, Layout.MainOrigin + Layout.MainExtent);
            DrawSurface(Geometry, Elements, Layer + 1, Layout.Left, SlotPickerTexture.Get(),
                Bounds.Min, Bounds.GetSize());
            DrawSlotPickerScrollbar(Geometry, Elements, Layer + 2, Layout);
        }
        else if (MainTexture.IsValid() && Layout.MainExtent.GetMin() > 0.0)
        {
            DrawMainGrid(Geometry, Elements, Layer + 1, Layout);
            if (ActionSection.Get() == ResourcesSection)
            {
                if (const auto* Grid = NavigationGrid.Get(); Grid && Grid->GetViewFraction() < 1.f)
                {
                    DrawMainScrollbar(Geometry, Elements, Layer + 2, Layout, 0.0, Layout.MainExtent.Y,
                        Grid->GetViewFraction(), Grid->GetViewOffsetFraction());
                }
            }
        }
        if (ResourcePreviewTile.IsValid() && ResourcePreviewTexture.IsValid() && ResourcePreviewBounds.GetSize().GetMin() > 0.0)
        {
            if (ResourceRemainderTexture.IsValid() && ResourceRemainderAlpha > 0.0)
            {
                DrawSurface(Geometry, Elements, Layer + 3, Layout.Left, ResourceRemainderTexture.Get(),
                    ResourceSourceBounds.Min, ResourceSourceBounds.GetSize(), 0.0, 1.0, ResourceRemainderAlpha);
            }
            if (ResourceMovingAlpha > 0.0)
            {
                DrawSurface(Geometry, Elements, Layer + 4, Layout.Left, ResourcePreviewTexture.Get(),
                    ResourcePreviewBounds.Min, ResourcePreviewBounds.GetSize(), 0.0, 1.0, ResourceMovingAlpha,
                    nullptr, ResourcePreviewDepth);
            }
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
        return Layer + (HasResourceInteraction() ? 5 : 3);
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
        const FName Section = ActionSection.Get();
        const bool bHandled = HasNavigableGrid(Section);
        if (bToggle && bHandled && !OptionsWindow.IsValid())
        {
            if (Section == ResourcesSection)
            {
                ToggleResourceCategories();
            }
            else
            {
                ToggleSavedSets();
            }
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
        ResourceCategoryRows.Reset();
        ResourceCategoryEntries.Reset();
        ResourceCategoryHeadings.Reset();
        NavigationGrid = Grid;
        if (!Grid)
        {
            return;
        }

        // The active panel can receive navigation before its Slate widgets are built.
        const auto IsVisible = [](const UWidget& Widget) {
            return UWidget::ConvertSerializedVisibilityToRuntime(Widget.GetVisibility()).IsVisible();
        };
        UTextBlock* PendingCategory = nullptr;
        for (auto* Child : Grid->GetAllChildren())
        {
            auto* Box = Cast<USizeBox>(Child);
            const auto* Cells = Box ? Cast<UHorizontalBox>(Box->GetContent()) : nullptr;
            if (!Cells || !IsVisible(*Cells))
            {
                auto* Heading = Box ? Cast<UTextBlock>(Box->GetContent()) : nullptr;
                if (ActionSection.Get() == ResourcesSection && Heading)
                {
                    PendingCategory = Heading;
                }
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
                if (PendingCategory)
                {
                    ResourceCategoryEntries.Add({PendingCategory->GetText().ToString(), FString::FromInt(NavigationRows.Num())});
                    ResourceCategoryRows.Add(NavigationRows.Num());
                    ResourceCategoryHeadings.Add(PendingCategory);
                    PendingCategory = nullptr;
                }
                NavigationRows.Add(MoveTemp(Row));
            }
        }
        if (ActionSection.Get() == ResourcesSection)
        {
            ResourceCategoryIndex = FMath::Clamp(ResourceCategoryIndex, 0, FMath::Max(0, ResourceCategoryRows.Num() - 1));
            if (ResourceCategoryRows.IsEmpty() && bSavedSetsOpen)
            {
                SetSavedSetsState(false, SavedSetIndex);
            }
        }
        const bool bKeepResourceScroll = ActionSection.Get() == ResourcesSection && ResourceScrollToRestore.IsSet();
        if (bKeepResourceScroll)
        {
            Grid->SetScrollOffset(*ResourceScrollToRestore);
            ResourceScrollToRestore.Reset();
        }
        if (!PendingSelectionId.IsEmpty())
        {
            const auto Identifier = MoveTemp(PendingSelectionId);
            PendingSelectionId.Reset();
            const bool bAlreadyLifted = Identifier == PendingLiftedResourceId;
            PendingLiftedResourceId.Reset();
            for (int32 Y = 0; Y < NavigationRows.Num(); ++Y)
            {
                auto& Cells = NavigationRows[Y].Cells;
                const int32 X = Cells.IndexOfByPredicate([this, &Identifier](const auto& Cell)
                {
                    const auto* Tile = Cell.Tile.Get();
                    return Tile && Tile->GetInspectorItem()
                        && (Identifier.Equals(UTF8_TO_TCHAR(Tile->GetInspectorItem()->ObjectId().c_str()), ESearchCase::CaseSensitive)
                            || (ActionSection.Get() == ResourcesSection && Identifier == ResourceItemId(*Tile)));
                });
                if (X != INDEX_NONE)
                {
                    GridFocus = FIntPoint(X, Y);
                    if (bAlreadyLifted)
                    {
                        Cells[X].FocusProgress = 1.f;
                        Cells[X].Tile->SetVisualState(1.f, 1.f);
                    }
                    return;
                }
            }
        }
        GridFocus = StepInventoryGrid(NavigationRows, GridFocus, FIntPoint::ZeroValue);
        if (ActionSection.Get() == ResourcesSection && !bKeepResourceScroll)
        {
            RevealResourceSelection();
        }
    }

    bool OpenSlotPicker()
    {
        const auto* Tile = GetSelectedTile();
        if (!SlotPickerSource || !Tile || !Tile->GetInspectorItem() || !HasSavedSets(ActionSection.Get()))
        {
            return false;
        }
        SlotPicker = SlotPickerSource(*Tile->GetInspectorItem());
        if (!SlotPicker)
        {
            return false;
        }
        SlotChoice = FMath::Max(0, SlotPicker->Choices.IndexOfByPredicate(
            [](const FCIInventorySlotChoice& Choice) { return Choice.bEquipped; }));
        FirstSlotChoice = SlotChoice;
        CreateSlotPickerRows(Tile->GetInspectorItem());
        RenderedSlotChoice = INDEX_NONE;
        SetSavedSetsState(false, 0);
        return true;
    }

    void ConfirmSlotChoice()
    {
        if (!SlotPicker || !SlotPicker->Equip || !SlotPicker->Choices.IsValidIndex(SlotChoice))
        {
            return;
        }
        const auto Identifier = SlotPicker->Choices[SlotChoice].Identifier;
        auto Picker = MoveTemp(*SlotPicker);
        SlotPicker.Reset();
        // The host may replace the entire view synchronously when an item is equipped.
        const auto KeepAlive = SharedThis(this);
        if (!Picker.Equip(Identifier))
        {
            SlotPicker = MoveTemp(Picker);
        }
    }

    void CreateSlotPickerRows(const TOptional<FCFMagritteBinding>& Equipped)
    {
        SlotPickerWidget.Reset();
        SlotPickerRows.Reset();
        SlotPickerRowGap = ActionSection.Get() == ApparelSection ? 0.f : 8.f;
        const auto* Theme = InventoryTheme.Get();
        SlotPickerRowHeight = GetDefault<UCIApparelRowTheme>()->RowHeight;
        for (const auto& Choice : SlotPicker->Choices)
        {
            FString Kind;
            if (Choice.Item)
            {
                for (const auto& Field : Choice.Item->Describe().orderedChildren())
                {
                    if (Field->id == "presentationKind")
                    {
                        Kind = InspectorValue(Choice.Item->Read(*Field)).ToString();
                        break;
                    }
                }
            }

            UCIInventoryTile* Row;
            if (ActionSection.Get() == ApparelSection || Kind.IsEmpty())
            {
                Row = NewObject<UCIApparelRow>();
            }
            else if (Kind == TEXT("gear"))
            {
                Row = NewObject<UCIGearRow>();
            }
            else
            {
                Row = NewObject<UCIWeaponToolRow>();
            }
            auto* RowTheme = CastChecked<UCIInventoryTileTheme>(
                &Row->GetTileTheme())->CreateProjectedSurfaceTheme(Row);
            RowTheme->Style.FocusedBackground = Theme->Style.FocusedBackground;
            RowTheme->TierColors = Theme->TierColors;
            RowTheme->TierStyle = Theme->TierStyle;
            if (Row->IsA<UCIApparelRow>())
            {
                RowTheme->Style = Theme->Style;
                RowTheme->Style.BackdropBlur = 0.f;
                RowTheme->TextTint = Theme->TextTint;
            }
            Row->Theme = RowTheme;
            auto* EquipmentRowTheme = CastChecked<UCIEquipmentRowTheme>(RowTheme);
            if (const auto* Palette = Cast<UCIEquipmentTileTheme>(Theme))
            {
                EquipmentRowTheme->Offensive = Palette->Offensive;
                EquipmentRowTheme->Defensive = Palette->Defensive;
                EquipmentRowTheme->Utility = Palette->Utility;
            }
            if (!Row->IsA<UCIApparelRow>())
            {
                EquipmentRowTheme->ThumbnailWidth = 144.f;
                EquipmentRowTheme->ContentInset = 8.f;
                EquipmentRowTheme->ThumbnailInset.Left = 10.f;
                EquipmentRowTheme->ThumbnailInset.Right = 10.f;
                EquipmentRowTheme->IconWidth = 118.f;
                EquipmentRowTheme->SlotIconFont.Size = 48.f;
            }
            if (auto* WeaponTheme = Cast<UCIWeaponToolRowTheme>(RowTheme))
            {
                WeaponTheme->StatWidth = 66.f;
                WeaponTheme->TalentAreaWidth = 36.f;
            }
            if (auto* GearTheme = Cast<UCIGearRowTheme>(RowTheme))
            {
                GearTheme->PrimaryMarkerSize = 30.f;
                GearTheme->BrandWidth = 72.f;
            }
            SlotPickerRowHeight = FMath::Max(SlotPickerRowHeight, EquipmentRowTheme->RowHeight);
            if (Choice.Item)
            {
                Row->BindItem(*Choice.Item);
            }
            else
            {
                Row->bShowTier = false;
            }
            Row->Label = Choice.Label;
            Row->Icon = Choice.Icon.Get();
            Row->FallbackIconIdentifier = Choice.FallbackIconIdentifier;
            if (auto* Weapon = Cast<UCIWeaponToolRow>(Row))
            {
                Weapon->SetComparisonItem(Equipped);
            }
            SlotPickerRows.Emplace(Row);
        }
    }

    int32 VisibleSlotChoices(const FInventoryShellLayout& Layout) const
    {
        return FMath::Max(1, FMath::FloorToInt((Layout.MainExtent.Y / Layout.Scale - SlotPickerHeadingHeight + SlotPickerRowGap)
            / (SlotPickerRowHeight + SlotPickerRowGap)));
    }

    void DrawSlotPickerScrollbar(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
        const FInventoryShellLayout& Layout) const
    {
        const int32 Total = SlotPicker->Choices.Num();
        const int32 Visible = VisibleSlotChoices(Layout);
        const double TrackHeight = Layout.MainExtent.Y - SlotPickerHeadingHeight * Layout.Scale;
        if (Total <= Visible || TrackHeight <= 0.0)
        {
            return;
        }

        DrawMainScrollbar(Geometry, Elements, Layer, Layout, SlotPickerHeadingHeight * Layout.Scale,
            TrackHeight, double(Visible) / Total, double(FirstSlotChoice) / Total);
    }

    void DrawMainScrollbar(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer,
        const FInventoryShellLayout& Layout, double Top, double TrackHeight, double ViewFraction, double OffsetFraction) const
    {
        const auto& Style = InventoryTheme.Get()->Scrollbar;
        const double X = Layout.MainOrigin.X + Layout.MainExtent.X + SlotPickerScrollbarGutter * Layout.Scale * .5;
        const double Y = Layout.MainOrigin.Y + Top;
        const float Width = FMath::Clamp(Style.Width, .5f, SlotPickerScrollbarGutter) * Layout.Scale;
        const auto Draw = [&](double Start, double End, const FLinearColor& Tint)
        {
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
                {Layout.Left.Project({X, Y + Start}), Layout.Left.Project({X, Y + End})},
                ESlateDrawEffect::NoPixelSnapping, Tint, true, Width);
        };
        const double ThumbHeight = TrackHeight * ViewFraction;
        const double ThumbOffset = TrackHeight * FMath::Clamp(OffsetFraction, 0.0, 1.0 - ViewFraction);
        Draw(0.0, TrackHeight, Style.TrackTint);
        Draw(ThumbOffset, ThumbOffset + ThumbHeight, Style.ThumbTint);
    }

    TSharedRef<SWidget> BuildSlotPicker(const FInventoryShellLayout& Layout)
    {
        const auto* Theme = InventoryTheme.Get();
        const int32 Visible = VisibleSlotChoices(Layout);
        FirstSlotChoice = FMath::Clamp(FirstSlotChoice, FMath::Max(0, SlotChoice - Visible + 1), SlotChoice);
        FirstSlotChoice = FMath::Min(FirstSlotChoice, FMath::Max(0, SlotPicker->Choices.Num() - Visible));
        auto Content = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox).HeightOverride(SlotPickerHeadingHeight).VAlign(VAlign_Center)
                [InspectorText(SlotPicker->Heading, Theme->GroupLabelFont)]
            ];
        if (SlotPicker->Choices.IsEmpty())
        {
            Content->AddSlot().AutoHeight()
            [InspectorText(NSLOCTEXT("Inventory", "NoCompatibleSlotItems", "No compatible items"), Theme->LabelFont)];
        }
        for (int32 Index = FirstSlotChoice; Index < FMath::Min(FirstSlotChoice + Visible, SlotPicker->Choices.Num()); ++Index)
        {
            const auto& Choice = SlotPicker->Choices[Index];
            auto* Row = SlotPickerRows[Index].Get();
            Row->SetVisualState(Index == SlotChoice ? 1.f : 0.f, 1.f);
            auto RowOverlay = SNew(SOverlay)
                + SOverlay::Slot()[Row->TakeWidget()];
            if (Choice.bEquipped)
            {
                if (Row->IsA<UCIApparelRow>())
                {
                    RowOverlay->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8.f)
                    [InspectorText(NSLOCTEXT("Inventory", "SlotItemEquipped", "Equipped"), Theme->LabelFont)];
                }
                else
                {
                    const auto& Style = CastChecked<UCIEquipmentRowTheme>(&Row->GetTileTheme());
                    RowOverlay->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
                    [
                        SNew(SBox).WidthOverride(Style->ThumbnailWidth).HAlign(HAlign_Right).Padding(FMargin(0, 0, 8, 4))
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("✓"))).Font(Style->CountFont)
                            .ColorAndOpacity(Style->TextTint)
                            .ToolTipText(NSLOCTEXT("Inventory", "SlotItemEquipped", "Equipped"))
                        ]
                    ];
                }
            }
            Content->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, SlotPickerRowGap)
            [SNew(SBox).HeightOverride(SlotPickerRowHeight)[RowOverlay]];
        }
        return Content;
    }

    void MoveNavigation(FIntPoint Direction)
    {
        if (Direction == FIntPoint::ZeroValue)
        {
            return;
        }
        if (HasResourceInteraction())
        {
            ChangeResourceQuantity(Direction);
            return;
        }
        if (SlotPicker)
        {
            SlotChoice = FMath::Clamp(SlotChoice + Direction.Y, 0, FMath::Max(0, SlotPicker->Choices.Num() - 1));
            return;
        }
        if (bSavedSetsOpen)
        {
            if (Direction.Y != 0)
            {
                if (ActionSection.Get() == ResourcesSection)
                {
                    SelectResourceCategory(ResourceCategoryIndex + Direction.Y);
                }
                else
                {
                    SelectSavedSet(SavedSetIndex + Direction.Y);
                }
            }
            return;
        }
        SetNavigationGrid(MainPanel.IsValid()
            ? Cast<UCFSpanGrid>(GetPanelContent(MainPanel->GetActiveWidget())) : nullptr);
        const auto Previous = GridFocus;
        GridFocus = StepInventoryGrid(NavigationRows, GridFocus, Direction);
        if (ActionSection.Get() == ResourcesSection
            && (Previous != GridFocus || (Direction.Y < 0 && ResourceCategoryRows.Contains(GridFocus.Y))))
        {
            RevealResourceSelection(Direction.Y < 0);
        }
    }

    void RevealResourceSelection(bool bIncludeCategoryHeading = false)
    {
        if (auto* Grid = NavigationGrid.Get())
        {
            if (GridFocus.Y == 0)
            {
                Grid->ScrollWidgetIntoView(nullptr);
                Grid->SetScrollOffset(0.f);
                return;
            }
            if (bIncludeCategoryHeading)
            {
                const int32 CategoryIndex = ResourceCategoryRows.IndexOfByKey(GridFocus.Y);
                if (ResourceCategoryHeadings.IsValidIndex(CategoryIndex))
                {
                    Grid->ScrollWidgetIntoView(ResourceCategoryHeadings[CategoryIndex].Get(), false,
                        EDescendantScrollDestination::TopOrLeft);
                    return;
                }
            }
            Grid->ScrollWidgetIntoView(const_cast<UCIInventoryTile*>(GetSelectedTile()), false,
                EDescendantScrollDestination::IntoView, 8.f);
        }
    }

    static FString ResourceItemId(const UCIInventoryTile& Tile)
    {
        const auto& Item = Tile.GetInspectorItem();
        return Item ? CIInventoryRowData::Read(*Item, "itemIdentifier").ToString() : FString();
    }

    void UpdateResourceRadials(const FInventoryShellLayout& Layout, FVector2D ViewSize)
    {
        if (!bResourceRadialsOpen || !ResourceActions.UpdateRadials)
        {
            return;
        }
        const auto* Tile = bSavedSetsOpen ? nullptr : GetSelectedTile();
        const auto Widget = Tile ? Tile->GetCachedWidget() : nullptr;
        const auto* Grid = NavigationGrid.Get();
        const auto GridWidget = Grid ? Grid->GetCachedWidget() : nullptr;
        ResourceRadialInput.StackId.Reset();
        ResourceRadialInput.DefinitionId = NAME_None;
        ResourceRadialInput.ViewSize = ViewSize;
        ResourceRadialInput.Scale = Layout.Scale;
        if (Widget && GridWidget)
        {
            const auto Bounds = TileBounds(*Widget, *GridWidget, Layout.Scale);
            const auto Lift = FocusedTilePlacement(Layout, Bounds, GridFocus, 1.0);
            ResourceRadialInput.StackId = ResourceItemId(*Tile);
            if (const auto& Item = Tile->GetInspectorItem())
            {
                ResourceRadialInput.DefinitionId = FName(*CIInventoryRowData::Read(*Item, "definitionIdentifier").ToString());
            }
            ResourceRadialInput.IconCenter = Layout.Left.Project(Lift.Origin + Lift.Extent * .5, Lift.Depth);
            ResourceRadialInput.IconSize = Bounds.GetSize().GetMin() * .4;
        }
        ResourceRadialAvailability = ResourceActions.UpdateRadials(ResourceRadialInput);
        ResourceRadialInput.bPlace = ResourceRadialInput.bClear = false;
    }

    bool OpenResourcePreview()
    {
        if (bResourceRadialsOpen)
        {
            return false;
        }
        auto* Tile = const_cast<UCIInventoryTile*>(GetSelectedTile());
        if (!Tile || !ResourceActions.Read)
        {
            return false;
        }
        const FString Id = ResourceItemId(*Tile);
        const auto State = ResourceActions.Read(Id);
        if (!State || State->Count <= 0)
        {
            return false;
        }

        ResourceLandingId.Reset();
        ResourceId = Id;
        ResourceState = *State;
        ResourceQuantity = State->Count;
        ResourceSourceTile = Tile;
        RevealResourceSelection();
        ResourcePreviewTile.Reset(NewObject<UCIResourceTile>());
        ResourceRemainderTile.Reset(NewObject<UCIResourceTile>());
        for (auto* Preview : {ResourcePreviewTile.Get(), ResourceRemainderTile.Get()})
        {
            Preview->Theme = InventoryTheme.Get()->CreateProjectedSurfaceTheme(Preview);
            if (const auto& Item = Tile->GetInspectorItem())
            {
                Preview->BindItem(*Item);
            }
            Preview->Icon = Tile->GetInspectorIcon();
            Preview->FallbackIconIdentifier = Tile->FallbackIconIdentifier;
        }
        ResourcePreviewTile->bShowZeroCount = true;
        ResourcePreviewTile->SetVisualState(1.f, 1.f);
        ResourceOpenBlend = 0.0;
        ResourceQuantityFraction = 0.0;
        return true;
    }

    struct FResourceDropTarget
    {
        FString Stack;
        int32 Count;
        int32 Quantity;
        bool operator==(const FResourceDropTarget&) const = default;
    };

    TOptional<FResourceDropTarget> GetResourceDropTarget() const
    {
        if (ActionSection.Get() != ResourcesSection || bSavedSetsOpen || bResourceRadialsOpen
            || !ResourceActions.Read || !ResourceActions.Drop || !ResourceActions.Poll
            || ResourceRequest.IsValid() || ResourceDropRequest.IsValid() || ResourceResult || bResourceClosing)
        {
            return {};
        }
        const auto* Tile = GetSelectedTile();
        const FString Stack = ResourcePreviewTile.IsValid() ? ResourceId : Tile ? ResourceItemId(*Tile) : FString();
        const auto State = Stack.IsEmpty() ? TOptional<FCIInventoryStackState>() : ResourceActions.Read(Stack);
        if (!State || !State->bCanDrop)
        {
            return {};
        }
        const int32 Quantity = ResourcePreviewTile.IsValid() ? ResourceQuantity : State->Count;
        if (Quantity <= 0 || Quantity > State->Count
            || (ResourcePreviewTile.IsValid() && ResourceState.Count != State->Count))
        {
            return {};
        }
        return FResourceDropTarget{Stack, State->Count, Quantity};
    }

    bool BeginResourceDrop(const FKey& Key)
    {
        if (ResourceDropKey.IsValid())
        {
            return false;
        }
        const auto Target = GetResourceDropTarget();
        if (!Target)
        {
            return false;
        }
        ResourceDropKey = Key;
        ResourceDropTarget = *Target;
        ResourceDropToken = FGuid::NewGuid();
        ResourceDropHold.Update(true, ResourceDropToken, ResourceDropClock);
        return true;
    }

    bool ReleaseResourceDrop(const FKey& Key)
    {
        if (!ResourceDropKey.IsValid() || ResourceDropKey != Key)
        {
            return false;
        }
        ResourceDropHold.Update(false, {}, ResourceDropClock);
        ResourceDropKey = {};
        ResourceDropToken.Invalidate();
        return true;
    }

    void UpdateResourceDrop(float DeltaTime)
    {
        ResourceDropClock += DeltaTime;
        if (ResourceDropRequest.IsValid())
        {
            const auto Result = ResourceActions.Poll ? ResourceActions.Poll(ResourceDropRequest)
                : TOptional<FCIInventoryStackResult>();
            if (!Result && FPlatformTime::Seconds() < ResourceDropDeadline)
            {
                return;
            }
            ResourceDropRequest.Invalidate();
            if (Result && !Result->Destination.IsEmpty())
            {
                if (auto* Tile = FindResourceTile(ResourceDropTarget.Stack))
                {
                    Tile->Count = ResourceDropTarget.Count - ResourceDropTarget.Quantity;
                }
                if (ResourcePreviewTile.IsValid())
                {
                    ResetResourcePreview();
                }
            }
            return;
        }
        if (!ResourceDropToken.IsValid())
        {
            return;
        }
        const auto Current = GetResourceDropTarget();
        if (!Current || *Current != ResourceDropTarget)
        {
            ResourceDropHold.Update(true, {}, ResourceDropClock);
            ResourceDropToken.Invalidate();
            return;
        }
        if (ResourceDropHold.Update(true, ResourceDropToken, ResourceDropClock))
        {
            ResourceDropToken.Invalidate();
            const FGuid Request = FGuid::NewGuid();
            if (ResourceActions.Drop(Request, Current->Stack, Current->Count, Current->Quantity))
            {
                ResourceDropRequest = Request;
                ResourceDropDeadline = FPlatformTime::Seconds() + 8.0;
            }
        }
    }

    bool CanCommitResource(bool bJoin) const
    {
        return HasResourceInteraction() && ResourceActions.Commit && !bResourceClosing && !ResourceRequest.IsValid()
            && !ResourceResult && !ResourceDropToken.IsValid() && !ResourceDropRequest.IsValid() && ResourceQuantity > 0 && (bJoin ? ResourceState.bCanJoin
                : ResourceState.bCanSplit && ResourceQuantity < ResourceState.Count);
    }

    void ChangeResourceQuantity(FIntPoint Direction)
    {
        if (!HasResourceInteraction() || bResourceClosing || ResourceRequest.IsValid() || ResourceResult || ResourceDropRequest.IsValid())
        {
            return;
        }
        if (Direction.X != 0)
        {
            ResourceQuantity = Direction.X > 0 ? ResourceState.Count : 0;
        }
        else if (Direction.Y != 0)
        {
            ResourceQuantity = static_cast<int32>(FMath::Clamp<int64>(int64(ResourceQuantity) - Direction.Y,
                0, ResourceState.Count));
        }
    }

    void CommitResource(bool bJoin)
    {
        if (!CanCommitResource(bJoin))
        {
            return;
        }
        const FGuid Request = FGuid::NewGuid();
        if (ResourceActions.Commit(Request, ResourceId, ResourceState.Count, ResourceQuantity, bJoin))
        {
            ResourceRequest = Request;
            ResourceDeadline = FPlatformTime::Seconds() + 8.0;
        }
    }

    void ResetResourcePreview()
    {
        if (auto* Source = ResourceSourceTile.Get(); Source)
        {
            Source->SetRenderOpacity(1.f);
        }
        for (const auto& Entry : ResourceReceiverTiles)
        {
            if (auto* Tile = Entry.Key.Get(); Tile) { Tile->Count = Entry.Value; }
        }
        if (auto* Receiver = SelectedResourceReceiver.Get())
        {
            Receiver->SetVisualState(0.f, 1.f);
        }
        SelectedResourceReceiver.Reset();
        ResourceReceiverTiles.Reset();
        ResourceFrameHoldUntil = 0.0;
        ResourceFollowupKey = FKey();
        ResourcePreviewTile.Reset();
        ResourceRemainderTile.Reset();
        ResourcePreviewTexture.Reset();
        ResourceRemainderTexture.Reset();
        ResourceRequest.Invalidate();
        ResourceResult.Reset();
        ResourceSourceBounds = FBox2D(ForceInit);
        ResourcePreviewBounds = FBox2D(ForceInit);
        ResourceStickX = ResourceStickY = 0.f;
        ResourceAnimationTime = 0.0;
        ResourceCollapseScrollStart.Reset();
        ResourceRemainderAlpha = 0.0;
        ResourceMovingAlpha = 1.0;
        ResourcePreviewDepth = 0.0;
        bResourceClosing = false;
    }

    UCIInventoryTile* FindResourceTile(const FString& Id) const
    {
        for (const auto& Row : NavigationRows)
        {
            for (const auto& Cell : Row.Cells)
            {
                auto* Tile = Cell.Tile.Get();
                if (Tile && ResourceItemId(*Tile) == Id) { return Tile; }
            }
        }
        return nullptr;
    }

    TOptional<FInventoryTileLift> ResourceReceiverPlacement(const UCIInventoryTile* Tile,
        const SWidget& GridWidget, const FInventoryShellLayout& Layout, bool bSelected) const
    {
        const auto Widget = Tile ? Tile->GetCachedWidget() : nullptr;
        if (!Widget)
        {
            return {};
        }
        for (int32 Y = 0; Y < NavigationRows.Num(); ++Y)
        {
            const int32 X = NavigationRows[Y].Cells.IndexOfByPredicate(
                [Tile](const auto& Cell) { return Cell.Tile.Get() == Tile; });
            if (X == INDEX_NONE)
            {
                continue;
            }
            const FIntPoint Cell(X, Y);
            const FBox2D Bounds = TileBounds(*Widget, GridWidget, Layout.Scale);
            const auto* Reflow = LiftedTiles.FindByPredicate(
                [Cell](const auto& Entry) { return Entry.Cell == Cell && Entry.bClipToGrid; });
            const FBox2D Source(Bounds.Min.ComponentMax(FVector2D::ZeroVector),
                Bounds.Max.ComponentMin(Layout.MainExtent));
            if (Source.GetSize().GetMin() <= 0.0)
            {
                return {};
            }
            const FVector2D MovedMin = Reflow ? Reflow->Origin - Layout.MainOrigin : Source.Min;
            const FBox2D MovedBounds(MovedMin, MovedMin + (Reflow ? Reflow->Extent : Source.GetSize()));
            const double OffsetY = MovedMin.Y - Source.Min.Y;
            auto Placement = FocusedTilePlacement(Layout, MovedBounds, Cell, bSelected ? 1.0 : 0.0);
            if (bSelected && Y == 1 && OffsetY < 0.0)
            {
                const auto FirstRow = FocusedTilePlacement(Layout, MovedBounds, {X, 0}, 1.0);
                const double Pitch = Bounds.GetSize().Y + InventoryTheme.Get()->RowGap * Layout.Scale;
                Placement.Origin = FMath::Lerp(Placement.Origin, FirstRow.Origin,
                    FMath::Clamp(-OffsetY / Pitch, 0.0, 1.0));
            }
            Placement.Source = Source;
            Placement.Cell = Cell;
            return Placement;
        }
        return {};
    }

    void AppendResourceCategoryShift(const FInventoryShellLayout& Layout, const SWidget& GridWidget,
        int32 EndRow, double Offset)
    {
        const int32 CategoryIndex = ResourceCategoryRows.IndexOfByKey(EndRow);
        if (Offset == 0.0 || !ResourceCategoryHeadings.IsValidIndex(CategoryIndex))
        {
            return;
        }
        const auto* Heading = ResourceCategoryHeadings[CategoryIndex].Get();
        const auto Widget = Heading ? Heading->GetCachedWidget() : nullptr;
        if (Widget)
        {
            const double Top = TileBounds(*Widget, GridWidget, Layout.Scale).Min.Y
                - InventoryTheme.Get()->GroupLabelPadding.Top * Layout.Scale;
            const FBox2D Strip({0.0, FMath::Max(0.0, Top)}, Layout.MainExtent);
            if (Strip.GetSize().Y > 0.0)
            {
                LiftedTiles.Add({Strip, Layout.MainOrigin + Strip.Min + FVector2D(0.0, Offset),
                    Strip.GetSize(), 0.0, {-1, -1}, true});
            }
        }
    }

    void AppendResourceTileShift(const FInventoryShellLayout& Layout, FIntPoint Cell,
        const FBox2D& Bounds, const FBox2D& Destination, double Progress)
    {
        const FBox2D Source(Bounds.Min.ComponentMax(FVector2D::ZeroVector),
            Bounds.Max.ComponentMin(Layout.MainExtent));
        if (Source.GetSize().GetMin() > 0.0
            && (!Bounds.Min.Equals(Destination.Min) || !Bounds.Max.Equals(Destination.Max)))
        {
            const FVector2D Scale = Destination.GetSize() / Bounds.GetSize();
            const FVector2D TargetMin = Destination.Min + (Source.Min - Bounds.Min) * Scale;
            LiftedTiles.RemoveAll([Cell](const auto& Entry) { return Entry.Cell == Cell; });
            LiftedTiles.Add({Source, Layout.MainOrigin + FMath::Lerp(Source.Min, TargetMin, Progress),
                FMath::Lerp(Source.GetSize(), Source.GetSize() * Scale, Progress), 0.0, Cell, true});
        }
    }

    FInventoryTileLift AppendResourceSplitReflow(const SWidget& GridWidget, const FInventoryShellLayout& Layout,
        int32 Columns, double Progress)
    {
        const auto& SourceRow = NavigationRows[GridFocus.Y];
        const int32 SourceSpan = SourceRow.Cells[GridFocus.X].Span;
        const auto Extent = ResourceSourceBounds.GetSize();
        const auto* Theme = InventoryTheme.Get();
        const double Gap = Theme->TileGap * Layout.Scale;
        const FVector2D Pitch((Extent.X + Gap) / SourceSpan, Extent.Y + Theme->RowGap * Layout.Scale);
        int32 Column = 0;
        for (int32 X = 0; X <= GridFocus.X; ++X)
        {
            Column += SourceRow.Cells[X].Span;
        }
        FVector2D Position = ResourceSourceBounds.Min - Layout.MainOrigin
            - FVector2D((Column - SourceSpan) * Pitch.X, 0.0);
        const auto Place = [&](int32 Span) {
            if (Column + Span > Columns)
            {
                Position.Y += Pitch.Y;
                Column = 0;
            }
            const FVector2D Start = Position + FVector2D(Column * Pitch.X, 0.0);
            Column += Span;
            return Start;
        };
        const FVector2D Landing = Place(SourceSpan);
        const int32 LandingRow = GridFocus.Y + (Landing.Y > ResourceSourceBounds.Min.Y - Layout.MainOrigin.Y ? 1 : 0);
        const auto* NextCategory = ResourceCategoryRows.FindByPredicate(
            [this](int32 Row) { return Row > GridFocus.Y; });
        const int32 EndRow = NextCategory ? *NextCategory : NavigationRows.Num();
        if (Progress > 0.0)
        {
            for (int32 Y = GridFocus.Y; Y < EndRow; ++Y)
            {
                const auto& Cells = NavigationRows[Y].Cells;
                for (int32 X = Y == GridFocus.Y ? GridFocus.X + 1 : 0; X < Cells.Num(); ++X)
                {
                    const FVector2D Destination = Place(Cells[X].Span);
                    const auto* Tile = Cells[X].Tile.Get();
                    const auto Widget = Tile ? Tile->GetCachedWidget() : nullptr;
                    if (Widget)
                    {
                        const auto Bounds = TileBounds(*Widget, GridWidget, Layout.Scale);
                        AppendResourceTileShift(Layout, {X, Y}, Bounds, FBox2D(Destination, Destination + Bounds.GetSize()), Progress);
                    }
                }
            }
            const double OriginalTail = ResourceSourceBounds.Min.Y - Layout.MainOrigin.Y
                + (EndRow - GridFocus.Y - 1) * Pitch.Y;
            AppendResourceCategoryShift(Layout, GridWidget, EndRow, (Position.Y - OriginalTail) * Progress);
        }
        return FocusedTilePlacement(Layout, FBox2D(Landing, Landing + Extent), {0, LandingRow}, 1.0);
    }

    void AppendResourceGapReflow(const SWidget& GridWidget, const FInventoryShellLayout& Layout, double Progress)
    {
        auto* Grid = NavigationGrid.Get();
        if (Progress <= 0.0 || !Grid || !ProjectedGridX || !NavigationRows.IsValidIndex(GridFocus.Y))
        {
            return;
        }
        const auto* NextCategory = ResourceCategoryRows.FindByPredicate(
            [this](int32 Row) { return Row > GridFocus.Y; });
        const int32 EndRow = NextCategory ? *NextCategory : NavigationRows.Num();
        const int32 CategoryIndex = ResourceCategoryRows.FindLastByPredicate(
            [this](int32 Row) { return Row <= GridFocus.Y; });
        const int32 FirstRow = CategoryIndex != INDEX_NONE ? ResourceCategoryRows[CategoryIndex] : 0;
        const int32 Columns = Grid->GetColumnCount();
        const auto* Theme = InventoryTheme.Get();
        const double Height = ResourceSourceBounds.GetSize().Y;
        const double Pitch = Height + Theme->RowGap * Layout.Scale;

        struct FResourceRow
        {
            TArray<FIntPoint> Cells;
            int32 UsedColumns = 0;
        };
        TArray<FResourceRow> Rows;
        Rows.AddDefaulted();
        for (int32 Y = FirstRow; Y < EndRow; ++Y)
        {
            for (int32 X = 0; X < NavigationRows[Y].Cells.Num(); ++X)
            {
                const FIntPoint Cell(X, Y);
                if (Cell == GridFocus)
                {
                    continue;
                }
                const int32 Span = NavigationRows[Y].Cells[X].Span;
                if (Rows.Last().UsedColumns + Span > Columns)
                {
                    Rows.AddDefaulted();
                }
                Rows.Last().Cells.Add(Cell);
                Rows.Last().UsedColumns += Span;
            }
        }

        const double CollapsedHeight = (EndRow - FirstRow - Rows.Num()) * Pitch;
        if (CollapsedHeight > 0.0)
        {
            if (!ResourceCollapseScrollStart)
            {
                ResourceCollapseScrollStart = Grid->GetScrollOffset();
            }
            const float EndOffset = FMath::Max(0.f, Grid->GetScrollOffsetOfEnd()
                - static_cast<float>(CollapsedHeight / Layout.Scale));
            const float Offset = FMath::Lerp(*ResourceCollapseScrollStart,
                FMath::Min(*ResourceCollapseScrollStart, EndOffset), Progress);
            if (Grid->GetScrollOffset() != Offset)
            {
                Grid->SetScrollOffset(Offset);
                // Sample and place the moving tiles from the same scroll position.
                RedrawResourceGrid(Layout);
            }
            AppendResourceCategoryShift(Layout, GridWidget, EndRow, -CollapsedHeight * Progress);
        }

        const auto* SourceTile = ResourceSourceTile.Get();
        const auto SourceWidget = SourceTile ? SourceTile->GetCachedWidget() : nullptr;
        if (!SourceWidget)
        {
            return;
        }
        double Top = TileBounds(*SourceWidget, GridWidget, Layout.Scale).Min.Y - (GridFocus.Y - FirstRow) * Pitch;
        for (const auto& Row : Rows)
        {
            // The span grid reserves a gap before its trailing spacer in a partial row.
            // Removing a tile can therefore change both the position and width of surviving tiles.
            const int32 Gaps = Row.Cells.Num() - (Row.UsedColumns == Columns ? 1 : 0);
            const double ColumnWidth = (ProjectedGridWidth - Gaps * Theme->TileGap) / Columns;
            double Left = 0.0;
            for (const FIntPoint Cell : Row.Cells)
            {
                const auto& Entry = NavigationRows[Cell.Y].Cells[Cell.X];
                const double Right = Left + Entry.Span * ColumnWidth;
                const auto* Tile = Entry.Tile.Get();
                const auto Widget = Tile ? Tile->GetCachedWidget() : nullptr;
                if (Widget)
                {
                    const FBox2D Destination({ProjectedGridX(Left) * Layout.Scale, Top},
                        {ProjectedGridX(Right) * Layout.Scale, Top + Height});
                    AppendResourceTileShift(Layout, Cell, TileBounds(*Widget, GridWidget, Layout.Scale), Destination, Progress);
                }
                Left = Right + Theme->TileGap;
            }
            Top += Pitch;
        }
    }

    void RedrawResourceGrid(const FInventoryShellLayout& Layout)
    {
        const FVector2D Raster(MainTexture->SizeX, MainTexture->SizeY);
        DrawToTexture(MainTexture, MainRenderRoot.ToSharedRef(), Raster, 0.f, Layout.Scale * ProjectionRasterScale);
    }

    void TickResourcePreview(const FInventoryShellLayout& Layout, float DeltaTime)
    {
        if (!ResourcePreviewTile.IsValid())
        {
            return;
        }

        constexpr double MotionSeconds = .22;
        constexpr double CountSeconds = .5;
        constexpr double CountStart = MotionSeconds * (220.0 / 260.0);
        const auto* Grid = NavigationGrid.Get();
        const auto GridWidget = Grid ? Grid->GetCachedWidget() : nullptr;
        auto* Source = ResourceSourceTile.Get();
        const auto TileWidget = Source ? Source->GetCachedWidget() : nullptr;
        if (!GridWidget || !TileWidget)
        {
            ResetResourcePreview();
            return;
        }
        if (!ResourceSourceBounds.bIsValid)
        {
            const auto Bounds = TileBounds(*TileWidget, *GridWidget, Layout.Scale);
            if (Bounds.GetSize().GetMin() <= 0.0)
            {
                return;
            }
            ResourceSourceBounds = FBox2D(Layout.MainOrigin + Bounds.Min, Layout.MainOrigin + Bounds.Max);
        }
        Source->SetRenderOpacity(0.f);

        if (ResourceRequest.IsValid() && ResourceActions.Poll)
        {
            if (auto Result = ResourceActions.Poll(ResourceRequest))
            {
                ResourceRequest.Invalidate();
                if (Result->Destination.IsEmpty())
                {
                    bResourceClosing = true;
                }
                else
                {
                    ResourceResult = MoveTemp(Result);
                    ResourceAnimationTime = 0.0;
                    for (const auto& Transfer : ResourceResult->Transfers)
                    {
                        if (auto* Receiver = FindResourceTile(Transfer.Destination))
                        {
                            ResourceReceiverTiles.Emplace(Receiver, Receiver->Count);
                        }
                    }
                }
            }
            else if (FPlatformTime::Seconds() >= ResourceDeadline)
            {
                ResourceRequest.Invalidate();
                bResourceClosing = true;
            }
        }
        else if (!bResourceClosing && !ResourceResult && !ResourceDropRequest.IsValid())
        {
            if (FMath::Abs(ResourceStickX) >= FMath::Abs(ResourceStickY))
            {
                ResourceQuantityFraction = 0.0;
                if (FMath::Abs(ResourceStickX) >= .6f)
                {
                    ChangeResourceQuantity({ResourceStickX > 0.f ? 1 : -1, 0});
                }
            }
            else
            {
                const double Deflection = FMath::Clamp((FMath::Abs(ResourceStickY) - .23) / .77, 0.0, 1.0);
                if (Deflection <= 0.0 || ResourceQuantityFraction * ResourceStickY < 0.0)
                {
                    ResourceQuantityFraction = 0.0;
                }
                ResourceQuantityFraction += FMath::Sign(ResourceStickY) * FMath::Square(Deflection)
                    * FMath::Max(30.0, ResourceState.Count * 1.2) * FMath::Min(double(DeltaTime), .1);
                const int32 Change = FMath::TruncToInt(ResourceQuantityFraction);
                ResourceQuantity = static_cast<int32>(FMath::Clamp<int64>(int64(ResourceQuantity) + Change,
                    0, ResourceState.Count));
                ResourceQuantityFraction -= Change;
            }
        }

        const FVector2D Extent = ResourceSourceBounds.GetSize();
        const FVector2D SourceCenter = ResourceSourceBounds.GetCenter();
        FVector2D ExpandedExtent = Extent * 1.75;
        const FVector2D Relative = SourceCenter - Layout.MainOrigin;
        const double Gap = InventoryTheme.Get()->TileGap * Layout.Scale;
        const int32 Columns = FMath::Max(1, Grid->GetColumnCount() / FMath::Max(1, Source->GridColumnSpan));
        const int32 Column = FMath::Clamp(FMath::FloorToInt(Relative.X / Layout.MainExtent.X * Columns), 0, Columns - 1);
        const FVector2D Expanded = Layout.MainOrigin + CIResourcePreviewAnchor(Relative, Extent, ExpandedExtent,
            Column, Columns, 0.0, Layout.MainExtent.X, Layout.MainExtent.Y, Gap);

        if (!ResourceResult)
        {
            ResourceOpenBlend = FMath::FInterpConstantTo(ResourceOpenBlend, bResourceClosing ? 0.0 : 1.0,
                double(DeltaTime), 1.0 / MotionSeconds);
        }
        const double Blend = FMath::SmoothStep(0.0, 1.0, ResourceOpenBlend);
        FVector2D Center = FMath::Lerp(SourceCenter, Expanded, Blend);
        FVector2D AnimatedExtent = FMath::Lerp(Extent, ExpandedExtent, Blend);
        const FVector2D TravelOrigin = Center;
        const FVector2D TravelExtent = AnimatedExtent;
        int32 Shown = ResourceQuantity;
        int32 Remainder = ResourceState.Count - ResourceQuantity;
        bool bLanded = false;
        bool bKeepFrame = false;
        bool bRedrawGrid = false;
        if (ResourceResult)
        {
            ResourceAnimationTime += DeltaTime;
            const auto& Transfers = ResourceResult->Transfers;
            const int32 TransferIndex = Transfers.IsEmpty() ? 0 : FMath::Min(
                FMath::FloorToInt(ResourceAnimationTime / MotionSeconds), Transfers.Num() - 1);
            const double Time = ResourceAnimationTime - TransferIndex * MotionSeconds;
            const double Travel = FMath::SmoothStep(0.0, 1.0, FMath::Clamp(Time / MotionSeconds, 0.0, 1.0));
            const FString& Target = Transfers.IsEmpty() ? ResourceResult->Destination : Transfers[TransferIndex].Destination;
            const bool bSplit = Transfers.IsEmpty();
            int32 Moved = 0;
            for (const auto& Transfer : Transfers)
            {
                Moved += Transfer.Moved;
            }
            const bool bConsumesSource = !bSplit && Moved == ResourceState.Count;
            const double FinalTransferStart = (Transfers.Num() - 1) * MotionSeconds;
            for (int32 Index = 0; Index < Transfers.Num(); ++Index)
            {
                const auto& Transfer = Transfers[Index];
                const bool bClosesGap = bConsumesSource && Index == Transfers.Num() - 1;
                const double CountDelay = bClosesGap ? 0.0 : CountStart;
                const double Progress = FMath::Clamp((ResourceAnimationTime - Index * MotionSeconds - CountDelay)
                    / CountSeconds, 0.0, 1.0);
                if (auto* Destination = FindResourceTile(Transfer.Destination))
                {
                    Destination->Count = Transfer.PreviousCount + FMath::FloorToInt(Transfer.Moved
                        * FMath::InterpEaseOut(0.0, 1.0, Progress, 4.f));
                }
            }
            if (!Transfers.IsEmpty())
            {
                Shown = Transfers[TransferIndex].Moved;
                ResourceMovingAlpha = Time < MotionSeconds ? 1.0 : 0.0;
                Remainder = ResourceState.Count - Moved;
                if (bConsumesSource)
                {
                    const double Reflow = FMath::SmoothStep(0.0, 1.0,
                        FMath::Clamp((ResourceAnimationTime - FinalTransferStart) / CountSeconds, 0.0, 1.0));
                    AppendResourceGapReflow(*GridWidget, Layout, Reflow);
                }
            }
            if (bSplit)
            {
                const auto Landing = AppendResourceSplitReflow(*GridWidget, Layout, Grid->GetColumnCount(), Travel);
                Center = FMath::Lerp(TravelOrigin, Landing.Origin + Landing.Extent * .5, Travel);
                AnimatedExtent = FMath::Lerp(TravelExtent, Landing.Extent, Travel);
                ResourcePreviewDepth = Landing.Depth * Travel;
            }
            else
            {
                const bool bSelected = Target == ResourceResult->Destination;
                const auto Landing = ResourceReceiverPlacement(FindResourceTile(Target), *GridWidget, Layout, bSelected);
                if (Landing)
                {
                    Center = FMath::Lerp(TravelOrigin, Landing->Origin + Landing->Extent * .5, Travel);
                    AnimatedExtent = FMath::Lerp(TravelExtent, Landing->Extent, Travel);
                    ResourcePreviewDepth = Landing->Depth * Travel;
                }
                else
                {
                    Center = FMath::Lerp(TravelOrigin, SourceCenter, Travel);
                    AnimatedExtent = FMath::Lerp(TravelExtent, Extent, Travel);
                    ResourcePreviewDepth = 0.0;
                }
                auto* Receiver = FindResourceTile(ResourceResult->Destination);
                const int32 ReceiverIndex = Transfers.IndexOfByPredicate([this](const auto& Transfer) {
                    return Transfer.Destination == ResourceResult->Destination;
                });
                if (Receiver && ReceiverIndex != INDEX_NONE
                    && ResourceAnimationTime >= (ReceiverIndex + 1) * MotionSeconds)
                {
                    if (SelectedResourceReceiver.Get() != Receiver)
                    {
                        SelectedResourceReceiver = Receiver;
                        Receiver->SetVisualState(1.f, 1.f);
                        bRedrawGrid = true;
                    }
                    if (const auto Selected = ResourceReceiverPlacement(Receiver, *GridWidget, Layout, true))
                    {
                        LiftedTiles.RemoveAll([&](const auto& Entry) { return Entry.Cell == Selected->Cell; });
                        LiftedTiles.Add(*Selected);
                    }
                }
                ResourcePreviewTile->SetVisualState(bSelected ? 1.f : 0.f, 1.f);
            }
            const double Finish = bSplit ? MotionSeconds :
                FinalTransferStart + (bConsumesSource ? 0.0 : CountStart) + CountSeconds;
            bLanded = ResourceAnimationTime >= Finish;
            bKeepFrame = ResourceActions.bPublishesAfterLanding;
        }
        else if (bResourceClosing)
        {
            ResourceAnimationTime += DeltaTime;
            const bool bReturningRemainder = ResourceQuantity < ResourceState.Count;
            const double CountProgress = FMath::Clamp((ResourceAnimationTime - CountStart) / CountSeconds, 0.0, 1.0);
            Shown = FMath::Lerp(ResourceQuantity, ResourceState.Count,
                FMath::InterpEaseOut(0.0, 1.0, CountProgress, 4.f));
            if (ResourceOpenBlend <= 0.0 && (!bReturningRemainder || CountProgress >= 1.0))
            {
                ResetResourcePreview();
                return;
            }
        }
        else
        {
            ResourcePreviewTile->bShowCount = true;
        }

        ResourcePreviewBounds = FBox2D(Center - AnimatedExtent * .5, Center + AnimatedExtent * .5);
        ResourcePreviewTile->Count = Shown;
        ResourceRemainderTile->Count = Remainder;
        ResourceRemainderAlpha = FMath::FInterpConstantTo(ResourceRemainderAlpha,
            Remainder > 0 && !bResourceClosing ? 1.0 : 0.0, double(DeltaTime), 1.0 / (MotionSeconds * 125.0 / 260.0));
        const FVector2D Raster(FMath::Max(1, FMath::CeilToInt(Extent.X * ProjectionRasterScale)),
            FMath::Max(1, FMath::CeilToInt(Extent.Y * ProjectionRasterScale)));
        DrawToTexture(ResourcePreviewTexture, ResourcePreviewTile->TakeWidget(), Raster, DeltaTime,
            Layout.Scale * ProjectionRasterScale);
        if (ResourceRemainderAlpha > 0.0)
        {
            DrawToTexture(ResourceRemainderTexture, ResourceRemainderTile->TakeWidget(), Raster, DeltaTime,
                Layout.Scale * ProjectionRasterScale);
        }
        if ((bRedrawGrid || (bLanded && bKeepFrame)) && MainRenderRoot)
        {
            RedrawResourceGrid(Layout);
        }
        if (bLanded)
        {
            ResourceLandingId = ResourceResult->Destination;
            if (bKeepFrame)
            {
                ResourceFrameHoldUntil = FPlatformTime::Seconds() + .5;
            }
            else
            {
                ResetResourcePreview();
            }
        }
    }

    void ScrollResources(float Amount)
    {
        if (auto* Grid = NavigationGrid.Get())
        {
            Grid->ScrollWidgetIntoView(nullptr);
            Grid->SetScrollOffset(FMath::Clamp(Grid->GetScrollOffset() + Amount,
                0.f, FMath::Max(0.f, Grid->GetScrollOffsetOfEnd())));
        }
    }

    void ToggleResourceCategories()
    {
        if (bSavedSetsOpen)
        {
            SetSavedSetsState(false, SavedSetIndex);
            return;
        }
        if (ResourceCategoryRows.IsEmpty())
        {
            return;
        }
        ResourceCategoryIndex = 0;
        while (ResourceCategoryRows.IsValidIndex(ResourceCategoryIndex + 1)
            && ResourceCategoryRows[ResourceCategoryIndex + 1] <= GridFocus.Y)
        {
            ++ResourceCategoryIndex;
        }
        SetSavedSetsState(true, SavedSetIndex);
    }

    void SelectResourceCategory(int32 Index)
    {
        if (ResourceCategoryRows.IsEmpty())
        {
            return;
        }
        const int32 Selection = FMath::Clamp(Index, 0, ResourceCategoryRows.Num() - 1);
        if (Selection != ResourceCategoryIndex)
        {
            ResourceCategoryIndex = Selection;
            GridFocus = {0, ResourceCategoryRows[Selection]};
            if (auto* Grid = NavigationGrid.Get())
            {
                Grid->ScrollWidgetIntoView(ResourceCategoryHeadings[Selection].Get(), false,
                    EDescendantScrollDestination::TopOrLeft);
            }
        }
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

    FInventoryTileLift FocusedTilePlacement(const FInventoryShellLayout& Layout, const FBox2D& Source,
        FIntPoint Cell, double Focus) const
    {
        const FVector2D Extent = Source.GetSize() * (1.0 + .035 * Focus);
        const double Lift = 19.0 * Layout.Scale * Focus;
        FVector2D Origin = Layout.MainOrigin + Source.GetCenter() - Extent * .5;
        if (Cell.Y == 0 || (ActionSection.Get() == ApparelSection && Cell.Y == 2))
        {
            const auto& Plane = Layout.Left;
            const double Depth = Plane.FocalLength + (Origin.X - Plane.Extent.X * .5) * Plane.SinYaw;
            Origin.Y += (Extent.Y - Source.GetSize().Y) * .5
                + FMath::Max(0.0, (Plane.Extent.Y * .5 - Layout.MainOrigin.Y - Source.Min.Y)
                    * Lift * Plane.CosYaw / Depth);
        }
        return {Source, Origin, Extent, Lift, Cell};
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

                LiftedTiles.Add(FocusedTilePlacement(Layout, Source, {X, Y},
                    FMath::SmoothStep(0.f, 1.f, Cell.FocusProgress)));
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
            if (Tile.bClipToGrid)
            {
                const auto Point = [&](FVector2D Offset)
                {
                    return Geometry.LocalToAbsolute(Layout.Left.Project(Layout.MainOrigin + Offset));
                };
                Elements.PushClip(FSlateClippingZone(Point({0.0, 0.0}), Point({Layout.MainExtent.X, 0.0}),
                    Point({0.0, Layout.MainExtent.Y}), Point(Layout.MainExtent)));
            }
            DrawRegion(Tile.Source, Tile.Origin, Tile.Extent, Tile.Depth, Tile.Cell == GridFocus ? Layer + 2 : Layer + 1);
            if (Tile.bClipToGrid)
            {
                Elements.PopClip();
            }
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
        return BuildFalloffPlot(MoveTemp(Points));
    }

    TSharedPtr<SWidget> BuildFalloffPlot(const magritte::Description& Field)
    {
        const auto Curve = Field.property("metadata:ciFalloff");
        const auto* Series = std::get_if<std::string>(&Curve.data);
        if (!Series)
        {
            return nullptr;
        }
        TArray<FString> Encoded;
        FString(UTF8_TO_TCHAR(Series->c_str())).ParseIntoArray(Encoded, TEXT(","));
        TArray<FVector2D> Points;
        for (const auto& Point : Encoded)
        {
            FString Distance, Damage;
            int64 Metres, Percent;
            if (!Point.Split(TEXT(":"), &Distance, &Damage)
                || !LexTryParseString(Metres, *Distance) || !LexTryParseString(Percent, *Damage))
            {
                return nullptr;
            }
            Points.Emplace(double(Metres), double(Percent));
        }
        return BuildFalloffPlot(MoveTemp(Points));
    }

    TSharedPtr<SWidget> BuildFalloffPlot(TArray<FVector2D> Points)
    {
        if (Points.Num() < 2 || Points[0].X != 0.0)
        {
            return nullptr;
        }

        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            const auto& Point = Points[Index];
            if (Point.Y < 0.0 || Point.Y > 100.0
                || (Index > 0 && (Point.X <= Points[Index - 1].X || Point.Y > Points[Index - 1].Y)))
            {
                return nullptr;
            }
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
        const auto AddStat = [&](const FText& Label, const FText& Value, const FString& Unit = FString())
        {
            if (Index % Columns == 0)
            {
                Row = SNew(SHorizontalBox);
                Grid->AddSlot().AutoHeight().Padding(0.f, Index == 0 ? 0.f : 4.f, 0.f, 0.f)[Row.ToSharedRef()];
            }
            auto ValueLine = SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [SNew(STextBlock).Text(Value).Font(ValueFont).ColorAndOpacity(FLinearColor::White)];
            if (!Unit.IsEmpty())
            {
                ValueLine->AddSlot().AutoWidth()
                [
                    SNew(STextBlock).Text(FText::FromString(Unit)).Font(ValueFont)
                    .ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, .55f))
                ];
            }
            Row->AddSlot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[InspectorText(Label, Theme->LabelFont)]
                + SVerticalBox::Slot().AutoHeight()[ValueLine]
            ];
            ++Index;
        };

        for (const auto& Field : Section.Describe().orderedChildren())
        {
            if (!IsInspectorFieldVisible(*Field))
            {
                continue;
            }
            const auto Label = InspectorValue(Field->property("label"));
            const FString Name = Label.ToString();
            if (Name.Equals(TEXT("DPS"), ESearchCase::IgnoreCase)
                || Name.Equals(TEXT("Damage per second"), ESearchCase::IgnoreCase))
            {
                continue;
            }
            auto Data = Section.Read(*Field);
            const bool bRange = Name.Equals(TEXT("Optimal Range"), ESearchCase::IgnoreCase);
            const bool bReload = Name.Equals(TEXT("Reload Time"), ESearchCase::IgnoreCase);
            const FString Unit = bRange ? TEXT("m") : bReload ? TEXT("s") : TEXT("");
            if (auto* Text = std::get_if<std::string>(&Data.data))
            {
                if ((bRange && Text->ends_with('m')) || (bReload && Text->ends_with('s')))
                {
                    Text->pop_back();
                    while (Text->ends_with(' ')) { Text->pop_back(); }
                }
                for (const std::string_view RemovedUnit : {" RPM", " rounds"})
                {
                    if (Text->ends_with(RemovedUnit))
                    {
                        Text->resize(Text->size() - RemovedUnit.size());
                        break;
                    }
                }
            }
            auto Value = InspectorValue(Data);
            if (Value.IsEmpty())
            {
                continue;
            }
            const bool bDamage = Name.Equals(TEXT("Damage"), ESearchCase::IgnoreCase)
                || Name.Equals(TEXT("DMG"), ESearchCase::IgnoreCase);
            if (bDamage)
            {
                if (const auto Number = CIInventoryRowData::Number(Data))
                {
                    Value = CIInventoryRowData::FormatCompactNumber(*Number);
                }
            }
            AddStat(Label, Value, Unit);
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

        const auto Tint = CIInventoryAttributeIcon::Tint(*Theme, UTF8_TO_TCHAR(Type->c_str()));
        if (!Tint)
        {
            return LabelWidget;
        }

        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
            [CIInventoryAttributeIcon::Build(*Tint)]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[LabelWidget];
    }

    TSharedRef<SWidget> BuildInspectorCard(
        const magritte::Description& Field, const FCFMagritteBinding& Section, bool bWeapon)
    {
        const auto* Theme = InventoryTheme.Get();
        const auto KindValue = Field.property("metadata:ciCard");
        const auto* Kind = std::get_if<std::string>(&KindValue.data);
        const bool bSlots = Kind && *Kind == "slots";
        const bool bMeters = Kind && *Kind == "meters";
        const bool bControls = Kind && *Kind == "controls";
        auto Content = SNew(SVerticalBox);
        if (Kind && *Kind == "talent")
        {
            for (const auto& Row : Section.Describe().orderedChildren())
            {
                const auto Label = InspectorValue(Row->property("label")).ToString();
                const bool bName = Label == TEXT("Name");
                if (bName || Label == TEXT("Comment"))
                {
                    Content->AddSlot().AutoHeight().Padding(0.f, 2.f)
                    [InspectorText(InspectorValue(Section.Read(*Row)),
                        bName ? Theme->GroupLabelFont : Theme->LabelFont, true)];
                }
            }
            return SNew(SBorder).BorderImage(&InspectorBrush).Padding(8.f)[Content];
        }
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
            const auto Label = CIInventoryRowData::NormalizeLabel(InspectorValue(Row->property("label")));
            if (!IsInspectorFieldVisible(*Row))
            {
                continue;
            }

            const auto Data = Section.Read(*Row);
            const auto Value = Label.ToString().Equals(TEXT("Armour"), ESearchCase::IgnoreCase)
                || Label.ToString().Equals(TEXT("Armor"), ESearchCase::IgnoreCase)
                ? CIInventoryRowData::FormatArmour(Data) : InspectorValue(Data);
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
                ValueWidget = InspectorText(Value, Theme->LabelFont, bSlots);
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

    void AddInspectorHeading(SVerticalBox& Content, const FText& Heading) const
    {
        if (Heading.IsEmpty())
        {
            return;
        }
        const auto* Theme = InventoryTheme.Get();
        FMargin Padding = Theme->GroupLabelPadding;
        Padding.Top += 6.f;
        Content.AddSlot().AutoHeight().Padding(Padding)
        [
            SNew(STextBlock).Text(Heading).Font(Theme->GroupLabelFont)
            .ColorAndOpacity(FSlateColor(Theme->GroupLabelTint)).AutoWrapText(true)
        ];
    }

    void RenderInspector(const FInventoryShellLayout& Layout, float DeltaTime)
    {
        const auto* Tile = GetInspectedTile();
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
                            AddInspectorHeading(*Content,
                                CIInventoryRowData::NormalizeLabel(InspectorValue(Field->property("label"))));
                            Content->AddSlot().AutoHeight()[BuildInspectorCard(*Field, *Section, bWeapon)];
                            if (const auto Plot = BuildFalloffPlot(*Field))
                            {
                                AddInspectorHeading(*Content, NSLOCTEXT("Inventory", "FalloffHeading", "Damage Falloff"));
                                Content->AddSlot().AutoHeight()
                                [SNew(SBorder).BorderImage(&InspectorBrush).Padding(8.f)[Plot.ToSharedRef()]];
                            }
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
        if (ActionSection.Get() != ResourcesSection && RightStickScroll != 0.f
            && !bSavedSetsOpen && !OptionsWindow.IsValid())
        {
            ScrollInspector(RightStickScroll * 640.f * DeltaTime);
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
        if (SlotPicker && SlotPickerTexture.IsValid())
        {
            const int32 End = FMath::Min(FirstSlotChoice + VisibleSlotChoices(Layout), SlotPicker->Choices.Num());
            const double RowHeight = SlotPickerRowHeight * Layout.Scale;
            const double Pitch = (SlotPickerRowHeight + SlotPickerRowGap) * Layout.Scale;
            const double PanelBottom = Layout.MainOrigin.Y + Layout.MainExtent.Y;
            const double PanelRight = Layout.MainOrigin.X + Layout.MainExtent.X;
            for (int32 Index = FirstSlotChoice; Index < End; ++Index)
            {
                const double Top = Layout.MainOrigin.Y + SlotPickerHeadingHeight * Layout.Scale
                    + (Index - FirstSlotChoice) * Pitch;
                const double Bottom = FMath::Min(Top + RowHeight, PanelBottom);
                if (Bottom > Top)
                {
                    PaintBackdrop(Layout.Left, {Layout.MainOrigin.X, Top}, {PanelRight, Bottom});
                }
            }
            return;
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
                    return (Entry.Source.Min == Min && Entry.Source.Max == Max)
                        || (Entry.Cell.Y == INDEX_NONE && Min.Y >= Entry.Source.Min.Y);
                }))
                {
                    const auto Offset = Lifted->Cell.Y == INDEX_NONE ? Lifted->Origin - Lifted->Source.Min : FVector2D::ZeroVector;
                    const auto Begin = Lifted->Cell.Y == INDEX_NONE ? Offset + Min : Lifted->Origin;
                    const auto End = Lifted->Cell.Y == INDEX_NONE ? Offset + Max : Lifted->Origin + Lifted->Extent;
                    PaintBackdrop(Layout.Left, Begin, End, Lifted->Depth);
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
        const bool bCategories = Section == ResourcesSection;
        const double HeadingHeight = SidebarHeadingHeight(Theme);
        SavedSetsRowHeight = bCategories ? 54.0 : FMath::Max(1.0, (PanelExtent - HeadingHeight
            - (FCIInventorySavedSetRows::VisibleCount - 1) * Theme.RowGap) / FCIInventorySavedSetRows::VisibleCount);
        SavedSetsCanvas.Reset(NewObject<UCanvasPanel>());
        SavedSetsRailFrameSlot = nullptr;
        if (bCategories)
        {
            auto* Frame = NewObject<UCFTile>(SavedSetsCanvas.Get());
            Frame->Theme = Theme.CreateProjectedSurfaceTheme(Frame);
            Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
            SavedSetsRailFrameSlot = SavedSetsCanvas->AddChildToCanvas(Frame);
            SavedSetsRailFrameSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));
            SavedSetsRailFrameSlot->SetZOrder(-1);
        }
        auto* Heading = NewObject<UTextBlock>(SavedSetsCanvas.Get());
        Heading->SetText(Section == ResourcesSection ? NSLOCTEXT("Inventory", "ResourceCategories", "Categories")
            : SavedSetsLabel(Section));
        Heading->SetVisibility(bCategories ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
        Heading->SetFont(Theme.GroupLabelFont);
        Heading->SetColorAndOpacity(FSlateColor(Theme.GroupLabelTint));
        SavedSetsHeadingSlot = SavedSetsCanvas->AddChildToCanvas(Heading);
        SavedSetsHeadingSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 0.f));

        auto* Body = NewObject<UCanvasPanel>(SavedSetsCanvas.Get());
        Body->SetClipping(EWidgetClipping::ClipToBounds);
        SavedSetsBodySlot = SavedSetsCanvas->AddChildToCanvas(Body);
        SavedSetsBodySlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        auto* SurfaceTheme = Theme.CreateProjectedSurfaceTheme(Body);
        if (bCategories)
        {
            SurfaceTheme->Style.Stroke = 0.f;
            SurfaceTheme->Style.Background = FLinearColor::Transparent;
            SurfaceTheme->MinimumSize = FVector2D::ZeroVector;
            SurfaceTheme->IconSize = 36.f;
        }

        SavedSetTiles.Reset();
        for (const auto& Row : SavedSetsPresentation.Rows.GetRows())
        {
            UTexture* CategoryIcon = nullptr;
            if (bCategories && ResourceCategoryRows.IsValidIndex(Row.Index))
            {
                const int32 GridRow = ResourceCategoryRows[Row.Index];
                if (NavigationRows.IsValidIndex(GridRow) && !NavigationRows[GridRow].Cells.IsEmpty())
                {
                    if (const auto* Resource = Cast<UCIResourceTile>(NavigationRows[GridRow].Cells[0].Tile.Get()))
                    {
                        CategoryIcon = Resource->CategoryIcon;
                    }
                }
            }

            UCFTile* Tile;
            if (CategoryIcon)
            {
                auto* IconTile = NewObject<UCIInventoryTile>(Body);
                IconTile->Icon = CategoryIcon;
                IconTile->bShowTier = false;
                Tile = IconTile;
            }
            else
            {
                Tile = NewObject<UCFTile>(Body);
                auto* Label = NewObject<UTextBlock>(Tile);
                Label->SetText(Row.bCreate ? NSLOCTEXT("Inventory", "NewGearSet", "Create")
                    : FText::AsCultureInvariant(Row.Set.Name));
                auto Font = Theme.LabelFont;
                if (bCategories) { Font.Size = 10.f; }
                Label->SetFont(Font);
                Label->SetColorAndOpacity(FSlateColor(Theme.TextTint));
                Label->SetAutoWrapText(bCategories);
                auto* Inset = NewObject<UBorder>(Tile);
                Inset->SetBrushColor(FLinearColor::Transparent);
                Inset->SetPadding(FMargin(bCategories ? 4.f : Theme.Spacing, 0.f));
                Inset->SetVerticalAlignment(VAlign_Center);
                Inset->SetContent(Label);
                Tile->SetContent(Inset);
            }
            Tile->Theme = SurfaceTheme;
            if (bCategories)
            {
                Tile->SetToolTipText(FText::AsCultureInvariant(Row.Set.Name));
            }
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
        const double Pitch = SavedSetsRowHeight + SidebarRowGap(Theme);
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
        const double Heading = SidebarHeadingHeight(Theme);
        SavedSetsHeadingSlot->SetOffsets(FMargin(Padding.Left, Padding.Top, Padding.Right + Gap, Theme.GroupLabelHeight));
        SavedSetsBodySlot->SetOffsets(FMargin(0.f, Heading, Gap, 0.f));

        const auto& Rows = SavedSetsPresentation.Rows;
        const int32 Total = Rows.GetRowCount();
        const int32 Visible = FCIInventorySavedSetRows::VisibleCount;
        if (SavedSetsRailFrameSlot)
        {
            SavedSetsRailFrameSlot->SetOffsets(FMargin(0.f, 0.f, Gap,
                FMath::Min(SavedSetsHeight, SavedSetsRowHeight * FMath::Min(Total, Visible))));
        }
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
        const double TrackHeight = Visible * SavedSetsRowHeight + (Visible - 1) * SidebarRowGap(Theme);
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

    double SidebarRowGap(const UCIInventoryTileTheme& Theme) const
    {
        return ActionSection.Get() == ResourcesSection ? 0.0 : Theme.RowGap;
    }

    double SidebarHeadingHeight(const UCIInventoryTileTheme& Theme) const
    {
        return ActionSection.Get() == ResourcesSection ? 0.0
            : Theme.GroupLabelHeight + Theme.GroupLabelPadding.GetTotalSpaceAlong<Orient_Vertical>();
    }

    FCIInventorySidebarGeometry GetSidebarGeometry(const FInventoryShellLayout& Layout, double ViewportWidth) const
    {
        return {Layout.MainExtent.X, ViewportWidth, InventoryTheme.Get()->TileGap * Layout.Scale,
            SavedSetsPresentation.Reveal.GetProgress(), ActionSection.Get() == ResourcesSection ? 44.0 * Layout.Scale : 0.0};
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
    TArray<int32> ResourceCategoryRows;
    TArray<FCIInventorySavedSet> ResourceCategoryEntries;
    TArray<TWeakObjectPtr<UTextBlock>> ResourceCategoryHeadings;
    int32 ResourceCategoryIndex = 0;
    TArray<FCIInventoryNavigationRow> NavigationRows;
    FIntPoint GridFocus = FIntPoint::ZeroValue;
    TMap<FName, TPair<FIntPoint, FString>> TabSelections;
    FString PendingSelectionId, PendingLiftedResourceId;
    FCIInventoryNavigationInput NavigationInput;
    TSharedPtr<SWidget> MenuWidget;
    TSharedPtr<SBox> MainRenderRoot;
    TSharedPtr<SCIInventoryActionFooter> FooterWidget;
    TStrongObjectPtr<UCanvasPanel> SavedSetsCanvas;
    UCanvasPanelSlot* SavedSetsRailFrameSlot = nullptr;
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
    FSlateRoundedBoxBrush InspectorBrush{FLinearColor::Transparent, 0.f};
    TSharedPtr<SBorder> InspectorWidget;
    TWeakObjectPtr<const UCIInventoryTile> InspectorTile;
    TOptional<FCFMagritteBinding> InspectorItem;
    TSharedPtr<SScrollBox> InspectorScroll;
    TStrongObjectPtr<UCIInspectorIdentityTile> InspectorIdentityTile;
    TSharedPtr<SWidget> InspectorHeader;
    float RightStickScroll = 0.f;
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
    TStrongObjectPtr<UTextureRenderTarget2D> SlotPickerTexture;
    TSharedPtr<SWidget> SlotPickerWidget;
    FVector2D SlotPickerWidgetSize = FVector2D::ZeroVector;
    FCIInventorySlotPickerSource SlotPickerSource;
    FCIInventoryResourceActions ResourceActions;
    FResourceDropTarget ResourceDropTarget{};
    FCFControllerHold ResourceDropHold{1000};
    FKey ResourceDropKey;
    FGuid ResourceDropToken, ResourceDropRequest;
    double ResourceDropClock = 0.0, ResourceDropDeadline = 0.0;
    FCIInventoryResourceRadialInput ResourceRadialInput;
    FCIInventoryResourceRadialAvailability ResourceRadialAvailability;
    bool bResourceRadialsOpen = false;
    FCIInventoryStackState ResourceState;
    TOptional<FCIInventoryStackResult> ResourceResult;
    TWeakObjectPtr<UCIInventoryTile> ResourceSourceTile;
    TArray<TPair<TWeakObjectPtr<UCIInventoryTile>, int64>> ResourceReceiverTiles;
    TWeakObjectPtr<UCIInventoryTile> SelectedResourceReceiver;
    TStrongObjectPtr<UCIResourceTile> ResourcePreviewTile, ResourceRemainderTile;
    TStrongObjectPtr<UTextureRenderTarget2D> ResourcePreviewTexture, ResourceRemainderTexture;
    FBox2D ResourceSourceBounds{ForceInit}, ResourcePreviewBounds{ForceInit};
    FString ResourceId, ResourceLandingId;
    FGuid ResourceRequest;
    int32 ResourceQuantity = 0;
    float ResourceStickX = 0.f, ResourceStickY = 0.f;
    double ResourceDeadline = 0.0, ResourceOpenBlend = 0.0, ResourceAnimationTime = 0.0;
    double ResourcePreviewDepth = 0.0;
    double ResourceFrameHoldUntil = 0.0;
    FKey ResourceFollowupKey, PendingResourceFollowupKey;
    TOptional<float> ResourceCollapseScrollStart, ResourceScrollToRestore;
    double ResourceQuantityFraction = 0.0, ResourceRemainderAlpha = 0.0, ResourceMovingAlpha = 1.0;
    bool bResourceClosing = false;
    TOptional<FCIInventorySlotPicker> SlotPicker;
    int32 SlotChoice = 0;
    int32 FirstSlotChoice = 0;
    int32 RenderedSlotChoice = INDEX_NONE;
    float SlotPickerPaintScale = 0.f;
    static constexpr float SlotPickerHeadingHeight = 34.f;
    static constexpr float SlotPickerScrollbarGutter = 8.f;
    float SlotPickerRowGap = 0.f;
    float SlotPickerRowHeight = 76.f;
    TArray<TStrongObjectPtr<UCIInventoryTile>> SlotPickerRows;
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
    double ProjectedGridWidth = 0.0;
    TFunction<double(double)> ProjectedGridX;
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

void UCIInventoryShell::SetResourceActions(FCIInventoryResourceActions Actions)
{
    ResourceActions = MoveTemp(Actions);
    if (Shell)
    {
        Shell->SetResourceActions(ResourceActions);
    }
}

bool UCIInventoryShell::HasResourceInteraction() const
{
    return Shell && Shell->HasResourceInteraction();
}

void UCIInventoryShell::SetSlotPickerSource(FCIInventorySlotPickerSource Source)
{
    SlotPickerSource = MoveTemp(Source);
    if (Shell)
    {
        Shell->SetSlotPickerSource(SlotPickerSource);
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
    RestoredTabSelections = Previous.Shell ? Previous.Shell->GetTabSelections(true) : Previous.RestoredTabSelections;
    if (ActionSection != Previous.ActionSection)
    {
        if (Shell)
        {
            Shell->RestoreNavigation(FIntPoint::ZeroValue, RestoredTabSelections, nullptr);
        }
        return;
    }
    bRestoreResourceRadialsOpen = Previous.Shell ? Previous.Shell->AreResourceRadialsOpen()
        : Previous.bRestoreResourceRadialsOpen;
    RestoredLiftedResourceId = Previous.Shell ? Previous.Shell->GetLiftedResourceLandingId()
        : Previous.RestoredLiftedResourceId;
    RestoredResourceFollowupKey = Previous.Shell ? Previous.Shell->GetResourceFollowupKey()
        : Previous.RestoredResourceFollowupKey;
    RestoredResourceScrollOffset = Previous.Shell ? Previous.Shell->GetResourceScrollOffset()
        : Previous.RestoredResourceScrollOffset;
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
        Shell->RestoreNavigation(RestoredGridFocus, RestoredTabSelections, RestoredNavigationInput,
            RestoredLiftedResourceId, RestoredResourceFollowupKey);
        Shell->RestoreResourceScrollOffset(RestoredResourceScrollOffset);
        Shell->SetResourceRadialsOpen(bRestoreResourceRadialsOpen);
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

int32 UCIInventoryShell::GetResourceReflowTileCountForTest() const
{
    return Shell ? Shell->GetResourceReflowTileCount() : 0;
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
    Shell->RestoreNavigation(RestoredGridFocus, RestoredTabSelections, RestoredNavigationInput,
        RestoredLiftedResourceId, RestoredResourceFollowupKey);
    Shell->RestoreResourceScrollOffset(RestoredResourceScrollOffset);
    Shell->SetResourceRadialsOpen(bRestoreResourceRadialsOpen);
    Shell->SetSavedSetsState(bRestoreSavedSetsOpen, RestoredSavedSetIndex);
    Shell->RestoreSavedSetsPresentation(RestoredSavedSetsPresentation);
    Shell->SetSlotPickerSource(SlotPickerSource);
    Shell->SetResourceActions(ResourceActions);
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
