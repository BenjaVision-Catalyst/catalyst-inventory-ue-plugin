#include "CIInventoryShell.h"

#include "CIInventoryTile.h"

#include "Algo/BinarySearch.h"
#include "Components/Border.h"
#include "Components/NamedSlot.h"
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
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SWindow.h"

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
        SavedSetsGrid.Reset();
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

    bool IsSavedSetsOpen() const { return bSavedSetsOpen; }
    int32 GetSavedSetIndex() const { return SavedSetIndex; }
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
        if (HasSavedSets(ActionSection.Get()) && (Key == EKeys::G || Key == EKeys::Gamepad_LeftTrigger))
        {
            if (!Event.IsRepeat())
            {
                ToggleSavedSets();
            }
            return FReply::Handled();
        }
        if (bSavedSetsOpen && HasSavedSets(ActionSection.Get())
            && (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down))
        {
            const bool bUp = Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up;
            SelectSavedSet(SavedSetIndex + (bUp ? -1 : 1));
            return FReply::Handled();
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
        return MenuWidget ? MenuWidget->OnKeyDown(Geometry, Event) : FReply::Unhandled();
    }

    FReply OnKeyUp(const FGeometry&, const FKeyEvent& Event) override
    {
        return ReleaseHoldInput(Event.GetKey()) ? FReply::Handled() : FReply::Unhandled();
    }

    void OnFocusLost(const FFocusEvent&) override
    {
        for (const auto Action : SavedSetHoldActions)
        {
            Hold(Action).Inputs.Reset();
        }
        CancelSavedSetHolds();
    }

    FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (OptionsWindow.IsValid())
        {
            return FReply::Handled();
        }
        if (Event.GetEffectingButton() == EKeys::LeftMouseButton && FooterWidget && FooterTexture.IsValid()
            && HasSavedSets(ActionSection.Get()))
        {
            const auto Layout = GetLayout(Geometry.GetLocalSize());
            const FVector2D PlanePoint = Layout.Left.Unproject(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
            const FVector2D FooterPoint = (PlanePoint - Layout.FooterOrigin) / Layout.Scale;
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
            if (bSavedSetsOpen && SavedSetsTexture.IsValid() && SavedSetsReveal == 1.f)
            {
                const double Width = SavedSetsTexture->SizeX / ProjectionRasterScale;
                const auto Point = (PlanePoint - Layout.MainOrigin + FVector2D(Width + 16.0 * Layout.Scale, 0))
                    / Layout.Scale;
                const auto& Theme = *InventoryTheme.Get();
                const double Heading = Theme.GroupLabelHeight + Theme.GroupLabelPadding.GetTotalSpaceAlong<Orient_Vertical>();
                if (Point.X >= 0 && Point.X < Width / Layout.Scale && Point.Y >= Heading && Point.Y < SavedSetsHeight)
                {
                    const int32 Row = FMath::FloorToInt((Point.Y - Heading) / (SavedSetsRowHeight + Theme.RowGap));
                    const int32 Index = FirstSavedSetIndex + Row;
                    if (Row < VisibleSavedSetRows && Index <= SavedSets.Num())
                    {
                        SelectSavedSet(Index);
                        return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
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
                    || SavedSetsTheme.Get() != Theme || SavedSetsHeight != Height || RenderedSavedSetIndex != SavedSetIndex)
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

    void BuildSavedSetsGrid(FName Section, UCIInventoryTileTheme& Theme, double PanelExtent)
    {
        const double Heading = Theme.GroupLabelHeight + Theme.GroupLabelPadding.GetTotalSpaceAlong<Orient_Vertical>();
        SavedSetsRowHeight = FMath::Max(1.0, (PanelExtent - Heading - (VisibleSavedSetRows - 1) * Theme.RowGap) / VisibleSavedSetRows);
        SavedSetsTexture.Reset();
        SavedSetsGrid.Reset(NewObject<UCFSpanGrid>());
        SavedSetsGrid->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        SavedSetsGrid->Configure(1, SavedSetsRowHeight, 0.f, Theme.RowGap, Theme.GroupGap,
            Theme.GroupLabelHeight, Theme.GroupLabelPadding, Theme.GroupLabelFont, Theme.GroupLabelTint);
        SavedSetsGrid->AddHeading(SavedSetsLabel(Section));

        FirstSavedSetIndex = FMath::Max(0, SavedSetIndex + 1 - VisibleSavedSetRows);
        for (int32 Index = FirstSavedSetIndex; Index < FirstSavedSetIndex + VisibleSavedSetRows && Index <= SavedSets.Num(); ++Index)
        {
            auto* Tile = NewObject<UCFTile>(SavedSetsGrid.Get());
            Tile->Theme = &Theme;
            auto* Label = NewObject<UTextBlock>(Tile);
            Label->SetText(SavedSets.IsValidIndex(Index) ? FText::AsCultureInvariant(SavedSets[Index].Name)
                : NSLOCTEXT("Inventory", "NewGearSet", "Create"));
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
            const float Selection = Index == SavedSetIndex ? 1.f : 0.f;
            Tile->SetVisualState(Selection, Selection);
        }

        SavedSetsSection = Section;
        SavedSetsTheme = &Theme;
        SavedSetsHeight = PanelExtent;
        RenderedSavedSetIndex = SavedSetIndex;
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
    TAttribute<bool> ConfirmEquips;
    TAttribute<bool> ConfirmAvailable;
    FSimpleDelegate OnConfirmSavedSet;
    TAttribute<bool> CanOverwrite;
    TAttribute<bool> CanDelete;
    FSavedSetHoldAction OnSavedSetHold;
    TAttribute<bool> CanRename;
    FRenameSavedSet OnRenameSavedSet;
    TWeakPtr<SWindow> OptionsWindow;
    TStaticArray<FSavedSetHold, 2> Holds;
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
    static constexpr int32 VisibleSavedSetRows = 6;
    float SavedSetsReveal = 0.f;
    bool bSavedSetsOpen = false;
    TArray<FCIInventorySavedSet> SavedSets;
    int32 SavedSetIndex = 0;
    int32 RenderedSavedSetIndex = INDEX_NONE;
    int32 FirstSavedSetIndex = 0;
    float SavedSetsRowHeight = 0.f;
    FWidgetRenderer* Renderer = nullptr;
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
            Shell->CloseSavedSetOptions();
            Shell->CancelSavedSetHolds();
            Shell->SetSavedSetsState(Shell->IsSavedSetsOpen() && HasSavedSets(Section), 0);
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

void UCIInventoryShell::RestoreSavedSetsState(const UCIInventoryShell& Previous)
{
    if (ActionSection != Previous.ActionSection)
    {
        return;
    }
    bRestoreSavedSetsOpen = Previous.Shell ? Previous.Shell->IsSavedSetsOpen() : Previous.bRestoreSavedSetsOpen;
    RestoredSavedSetIndex = Previous.Shell ? Previous.Shell->GetSavedSetIndex() : Previous.RestoredSavedSetIndex;
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
        Shell->SetSavedSetsState(bRestoreSavedSetsOpen, RestoredSavedSetIndex);
        Shell->RestoreHoldConfirmation(ESavedSetHold::Overwrite, RestoredOverwriteConfirmation);
        Shell->RestoreHoldConfirmation(ESavedSetHold::Delete, RestoredDeleteConfirmation);
    }
}

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
    Shell->SetSavedSetsState(bRestoreSavedSetsOpen, RestoredSavedSetIndex);
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
