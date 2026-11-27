#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "CIInventoryModel.h"
#include "../CIInventorySavedSetRows.h"
#include "CIInventoryShell.h"
#include "CIInventoryProfile.h"
#include "CIInventoryTile.h"
#include "CIInventoryView.h"
#include "Components/NamedSlot.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Input/Events.h"
#include "Misc/FileHelper.h"
#include "Presentation/CFMenu.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventoryPresentationTest,
    "Catalyst.Inventory.MagrittePresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventorySidebarRevealTest,
    "Catalyst.Inventory.SavedSetSidebarReveal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCIInventorySidebarRevealTest::RunTest(const FString&)
{
    FCIInventorySidebarReveal Reveal;
    Reveal.Update(true, 0.0);
    TestEqual(TEXT("Opening starts at the grid edge"), Reveal.GetProgress(), 0.0);
    Reveal.Update(true, .05);
    TestTrue(TEXT("The first quarter uses the original quartic easing"),
        FMath::IsNearlyEqual(Reveal.GetProgress(), .03125));
    Reveal.Update(true, .10);
    TestEqual(TEXT("The slide is halfway at 100 ms"), Reveal.GetProgress(), .5);

    auto Refreshed = Reveal;
    Refreshed.Update(true, .15);
    TestTrue(TEXT("A replacement view continues the same opening"), FMath::IsNearlyEqual(Refreshed.GetProgress(), .96875));
    Refreshed.Update(true, .20);
    TestEqual(TEXT("Opening is complete at 200 ms"), Refreshed.GetProgress(), 1.0);
    Reveal.Update(false, .10);
    TestEqual(TEXT("Reversing starts at the current position"), Reveal.GetProgress(), .5);
    Reveal.Update(false, .20);
    TestEqual(TEXT("Closing uses the same curve from the current position"), Reveal.GetProgress(), .25);
    Reveal.Update(false, .31);
    TestEqual(TEXT("Closing finishes without a snap"), Reveal.GetProgress(), 0.0);

    const FCIInventorySidebarGeometry Midpoint(600.0, 1000.0, 8.0, .5);
    TestTrue(TEXT("Sidebar width follows the original viewport cap"), FMath::IsNearlyEqual(Midpoint.Width, 200.0 * 2.0 / 3.0));
    TArray<FVector> Strips;
    Midpoint.PaintVisibleStrips(Midpoint.Width, [&](double Start, double End, double Alpha) {
        Strips.Emplace(Start * Midpoint.Width, End * Midpoint.Width, Alpha);
    });
    if (TestEqual(TEXT("One solid region and four feather strips"), Strips.Num(), 5))
    {
        TestTrue(TEXT("The painted edge stays at the grid boundary"),
            FMath::IsNearlyEqual(Strips.Last().Y, Midpoint.GetTravel()));
        TestTrue(TEXT("Feather spans half a grid gap at midpoint"),
            FMath::IsNearlyEqual(Strips.Last().Y - Strips[0].Y, 4.0));
        TestEqual(TEXT("The solid region fades with the slide"), Strips[0].Z, .5);
        TestTrue(TEXT("The feather fades toward the fixed edge"), Strips.Last().Z < Strips[1].Z);
    }

    const FCIInventorySidebarGeometry Open(600.0, 1000.0, 8.0, 1.0);
    TestTrue(TEXT("The settled sidebar is two tile gaps from the grid"),
        FMath::IsNearlyEqual(Open.GetTravel() - Open.Width, 16.0));
    Strips.Reset();
    Open.PaintVisibleStrips(Open.Width, [&](double Start, double End, double Alpha) { Strips.Emplace(Start, End, Alpha); });
    TestTrue(TEXT("The settled sidebar has its full width and opacity"),
        Strips.Num() == 1 && Strips[0] == FVector(0.0, 1.0, 1.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventorySavedSetRowsTest,
    "Catalyst.Inventory.SavedSetRowAnimation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCIInventorySavedSetRowsTest::RunTest(const FString&)
{
    FCIInventorySavedSetRows Rows;
    TArray<FCIInventorySavedSet> Sets{{TEXT("One"), TEXT("1")}, {TEXT("Two"), TEXT("2")}};
    const auto Saved = [&](const TCHAR* Id, double Time) {
        const auto* Row = Rows.GetRows().FindByPredicate([&](const auto& Entry) { return Entry.Set.Identifier == Id; });
        check(Row);
        return Rows.Sample(*Row, Time);
    };
    const auto Create = [&](double Time) {
        const auto* Row = Rows.GetRows().FindByPredicate([](const auto& Entry) { return Entry.bCreate; });
        check(Row);
        return Rows.Sample(*Row, Time);
    };

    Rows.Update(Sets, 2, 1.0);
    Sets.Add({TEXT("Three"), TEXT("3")});
    Rows.Update(Sets, 2, 2.0);
    TestEqual(TEXT("Create starts at its previous slot"), Create(2.0).Position, 2.0);
    TestEqual(TEXT("The new row starts invisible at its destination"), Saved(TEXT("3"), 2.0).Opacity, 0.0);
    TestTrue(TEXT("Create slides toward the new slot"), FMath::IsNearlyEqual(Create(2.10).Position, 2.5));
    TestEqual(TEXT("The new row stays in its slot as it fades"), Saved(TEXT("3"), 2.18).Position, 2.0);
    const auto FadeIn = Saved(TEXT("3"), 2.18).Opacity;
    TestTrue(TEXT("The new row fades in while Create moves away"), FadeIn > 0.0 && FadeIn < 1.0);
    Rows = FCIInventorySavedSetRows(Rows);
    TestFalse(TEXT("A refresh with the same rows keeps the animation in progress"), Rows.Update(Sets, 2, 2.18));
    TestEqual(TEXT("A refresh preserves the sampled opacity"), Saved(TEXT("3"), 2.18).Opacity, FadeIn);
    TestEqual(TEXT("Create reaches its new slot"), Create(2.30).Position, 3.0);
    TestEqual(TEXT("The new row reaches full opacity"), Saved(TEXT("3"), 2.30).Opacity, 1.0);

    Rows.Update(Sets, 1, 2.9);
    Sets.RemoveAt(1);
    Rows.Update(Sets, 1, 3.0);
    const auto FadeOut = Saved(TEXT("2"), 3.06);
    TestTrue(TEXT("The deleted row fades in its old slot"),
        FadeOut.Position == 1.0 && FadeOut.Opacity > 0.0 && FadeOut.Opacity < 1.0);
    TestEqual(TEXT("The row below waits for the deleted row to fade"), Saved(TEXT("3"), 3.06).Position, 2.0);
    TestEqual(TEXT("The deleted row is gone during the slide"), Saved(TEXT("2"), 3.22).Opacity, 0.0);
    TestTrue(TEXT("The row below slides up"), FMath::IsNearlyEqual(Saved(TEXT("3"), 3.22).Position, 1.5));
    TestTrue(TEXT("Create also slides up"), FMath::IsNearlyEqual(Create(3.22).Position, 2.5));
    TestEqual(TEXT("The row above does not move"), Saved(TEXT("1"), 3.25).Position, 0.0);
    TestEqual(TEXT("The gap is closed"), Saved(TEXT("3"), 3.35).Position, 1.0);

    Rows = {};
    for (int32 Index = Sets.Num(); Index < FCIInventorySavedSetRows::VisibleCount - 1; ++Index)
    {
        const auto Name = LexToString(Index);
        Sets.Add({Name, TEXT("extra-") + Name});
    }
    Rows.Update(Sets, Sets.Num(), 4.0);
    Sets.Add({TEXT("Newest"), TEXT("newest")});
    Rows.Update(Sets, Sets.Num() - 1, 5.0);
    TestEqual(TEXT("At capacity Create remains visible below the new row"),
        Create(5.3).Position, double(FCIInventorySavedSetRows::VisibleCount - 1));
    TestEqual(TEXT("At capacity the new row is directly above Create"),
        Saved(TEXT("newest"), 5.3).Position, Create(5.3).Position - 1.0);
    Sets.Pop();
    Rows.Update(Sets, Sets.Num(), 6.0);
    TestEqual(TEXT("Deleting at capacity keeps the top of the list anchored"), Saved(TEXT("3"), 6.4).Position, 0.0);
    TestEqual(TEXT("Create still moves up after a deletion at capacity"),
        Create(6.4).Position, double(FCIInventorySavedSetRows::VisibleCount - 2));
    return true;
}

bool FCIInventoryPresentationTest::RunTest(const FString&)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystInventory"));
    FString Json;
    if (!TestTrue(TEXT("Load the GT-authored Inventory graph"), Plugin &&
        FFileHelper::LoadFileToString(Json, *(Plugin->GetBaseDir() / TEXT("Tests/Fixtures/InventoryPresentation.json")))))
    {
        return false;
    }

    auto Graph = std::make_shared<magritte::Graph>(magritte::Graph::decode(TCHAR_TO_UTF8(*Json)));
    const FCFViewContext Context{*GetTransientPackage(), MakeShared<FCFViewState>()};
    const auto Open = [&]() {
        return Cast<UCIInventoryView>(FCIInventoryModel(
            FCFMagritteBinding::Find(Graph, "inventoryForProfile").GetValue()).AsView(Context));
    };

    TStrongObjectPtr<UCIInventoryView> View(Open());
    if (!TestNotNull(TEXT("A model answers its Inventory view"), View.Get()))
    {
        return false;
    }
    const auto Slate = View->TakeWidget();
    auto* Shell = CastChecked<UCIInventoryShell>(View->WidgetTree->RootWidget);
    auto* Menu = Shell->GetMenu();
    TestEqual(TEXT("The container yields four menu items"), Menu->GetChildrenCount(), 4);
    TestEqual(TEXT("The initial Gear tab follows the first description"), Menu->GetSelectedIndex(), 1);
    const auto ShellSlate = Shell->TakeWidget();
    const auto Press = [&](const FKey& Key) {
        return ShellSlate->OnKeyDown(ShellSlate->GetCachedGeometry(),
            FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled();
    };
    TestTrue(TEXT("Q is routed to the previous inventory tab"), Press(EKeys::Q));
    TestEqual(TEXT("Q opens Apparel"), Menu->GetSelectedIndex(), 0);
    TestTrue(TEXT("E is routed to the next inventory tab"), Press(EKeys::E));
    TestEqual(TEXT("E returns to Gear"), Menu->GetSelectedIndex(), 1);
    bool bClosed = false;
    Shell->SetCloseAction(FSimpleDelegate::CreateLambda([&] { bClosed = true; }));
    TestTrue(TEXT("Tab activates the bound close action"), Press(EKeys::Tab) && bClosed);
    Shell->SetCloseAction({});
    if (FApp::CanEverRender())
    {
        auto* Theme = NewObject<UCIInventoryTileTheme>(Shell);
        Theme->Style.Background = FLinearColor::Green;
        Theme->Style.FocusedBackground = FLinearColor::Red;
        Theme->Scrollbar.Width = 6.f;
        Theme->Scrollbar.TrackTint = FLinearColor::Blue;
        Theme->Scrollbar.ThumbTint = FLinearColor::Yellow;
        Shell->SetLayoutTheme(Theme);
        auto* Renderer = new FWidgetRenderer(false);
        const FVector2D Size(1280, 720);
        const auto Geometry = FGeometry::MakeRoot(Size, FSlateLayoutTransform());
        const auto Capture = [&] {
            TArray<FColor> Pixels;
            if (auto* Target = Renderer->DrawWidget(ShellSlate, Size))
            {
                Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
            }
            return Pixels;
        };
        const auto ReadSidebarRows = [&] {
            const auto Pixels = Capture();
            TArray<FColor> Rows;
            if (!Pixels.IsEmpty())
            {
                FColor Previous = FColor::Black;
                // This column crosses the sidebar fill clear of the row labels and tile borders.
                constexpr int32 Column = 292;
                for (int32 Y = 0; Y < Size.Y; ++Y)
                {
                    const FColor Pixel = Pixels[Y * int32(Size.X) + Column];
                    const FColor Fill = Pixel.R > 180 && Pixel.G < 60 ? FColor::Red
                        : Pixel.G > 180 && Pixel.R < 60 ? FColor::Green : FColor::Black;
                    if (Fill != Previous && Fill != FColor::Black)
                    {
                        Rows.Add(Fill);
                    }
                    Previous = Fill;
                }
            }
            return Rows;
        };
        const auto ReadScrollbar = [&] {
            const auto Pixels = Capture();
            TPair<FBox2D, FBox2D> Bounds{FBox2D(ForceInit), FBox2D(ForceInit)};
            for (int32 Index = 0; Index < Pixels.Num(); ++Index)
            {
                const FColor Color = Pixels[Index];
                const FVector2D Point(Index % int32(Size.X), Index / int32(Size.X));
                if (Color.B > 180 && Color.R < 60 && Color.G < 60)
                {
                    Bounds.Key += Point;
                }
                if (Color.R > 180 && Color.G > 180 && Color.B < 60)
                {
                    Bounds.Value += Point;
                }
            }
            return Bounds;
        };
        for (const auto& Section : {TPair<int32, FName>{1, TEXT("gear")}, TPair<int32, FName>{0, TEXT("apparel")}})
        {
            Menu->SelectItem(Section.Key);
            Shell->SetActionSection(Section.Value);
            Shell->SetSavedSets({{TEXT("One"), TEXT("one")}, {TEXT("Two"), TEXT("two")}});
            TestTrue(TEXT("Open the saved-set sidebar"), Press(EKeys::G));
            ShellSlate->Tick(Geometry, 0.0, 0.f);
            FPlatformProcess::Sleep(.21f);
            const TArray<FColor> First{FColor::Red, FColor::Green, FColor::Green};
            TestTrue(TEXT("The first selection shows both saved rows and Create"), ReadSidebarRows() == First);
            TestTrue(TEXT("Move the saved-set selection"), Press(EKeys::Down));
            const TArray<FColor> Second{FColor::Green, FColor::Red, FColor::Green};
            TestTrue(TEXT("Moving selection changes only the highlighted fill"), ReadSidebarRows() == Second);

            TArray<FCIInventorySavedSet> Sets;
            for (int32 Index = 0; Index < FCIInventorySavedSetRows::VisibleCount - 1; ++Index)
            {
                const FString Name = FString::FromInt(Index);
                Sets.Add({Name, Name});
            }
            Shell->SetSavedSets(Sets);
            const auto Fits = ReadScrollbar();
            TestTrue(TEXT("Six rows including Create show no scrollbar"), !Fits.Key.bIsValid && !Fits.Value.bIsValid);
            Sets.Add({TEXT("Sixth saved entry"), TEXT("sixth")});
            Shell->SetSavedSets(Sets);
            const auto AtTop = ReadScrollbar();
            if (TestTrue(TEXT("An overflowing sidebar renders the themed track and thumb"),
                AtTop.Key.bIsValid && AtTop.Value.bIsValid))
            {
                TestTrue(TEXT("The themed thumb is centered in the gap"),
                    AtTop.Value.GetCenter().X > 324.0 && AtTop.Value.GetCenter().X < 329.0);
                TestTrue(TEXT("The thumb starts at the top of the track"), AtTop.Value.Min.Y < AtTop.Key.Min.Y);
                for (int32 Index = 0; Index < Sets.Num(); ++Index)
                {
                    Press(EKeys::Down);
                }
                const auto AtBottom = ReadScrollbar();
                TestTrue(TEXT("Scrolling moves the thumb to the bottom of the same track"),
                    AtBottom.Key.bIsValid && AtBottom.Value.bIsValid && AtBottom.Key.Max.Y < AtBottom.Value.Max.Y
                    && AtBottom.Value.Min.Y > AtTop.Value.Min.Y);
            }
            Press(EKeys::G);
        }
        BeginCleanup(Renderer);
    }
    TestTrue(TEXT("Select Resources"), Menu->SelectItem(2));

    TStrongObjectPtr<UCIInventoryView> Reopened(Open());
    if (!TestNotNull(TEXT("The model can be rendered again"), Reopened.Get()))
    {
        return false;
    }
    const auto ReopenedSlate = Reopened->TakeWidget();
    auto* ReopenedShell = CastChecked<UCIInventoryShell>(Reopened->WidgetTree->RootWidget);
    TestEqual(TEXT("The last selected description is restored"), ReopenedShell->GetMenu()->GetSelectedIndex(), 2);
    auto* ActiveSlot = Cast<UNamedSlot>(ReopenedShell->GetMainPanel()->GetActiveWidget());
    auto* Content = ActiveSlot ? Cast<UTextBlock>(ActiveSlot->GetContent()) : nullptr;
    if (TestNotNull(TEXT("The selected relation creates its own view"), Content))
    {
        TestEqual(TEXT("The panel displays the related object's value"), Content->GetText().ToString(),
            FString(TEXT("Owned resources")));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventoryCatalogProfileTest, "Catalyst.Inventory.CatalogProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCIInventoryCatalogProfileTest::RunTest(const FString&)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystInventory"));
    FString Json;
    if (!TestTrue(TEXT("Load the server catalog profile"), Plugin &&
        FFileHelper::LoadFileToString(Json, *(Plugin->GetBaseDir() / TEXT("Tests/Fixtures/CatalogGadgetProfile.json")))))
    {
        return false;
    }

    TestFalse(TEXT("Reject malformed graphs"), CIInventoryProfile::DecodeUser(TEXT("{"), TEXT("inventory-fixture")).IsSet());
    TestFalse(TEXT("Reject another user"), CIInventoryProfile::DecodeUser(Json, TEXT("another-user")).IsSet());
    const auto User = CIInventoryProfile::DecodeUser(Json, TEXT("inventory-fixture"));
    if (!TestTrue(TEXT("Decode the authorized inventory"), User.IsSet()))
    {
        return false;
    }
    const auto Gear = User->Follow("profile")->Follow("inventory")->Follow("gear");
    const auto Gadgets = Gear ? Gear->Related("gadgets", 2) : TArray<FCFMagritteBinding>();
    if (!TestEqual(TEXT("Both equipped gadgets are described"), Gadgets.Num(), 2))
    {
        return false;
    }
    for (int32 Index = 0; Index < Gadgets.Num(); ++Index)
    {
        const auto Inspector = Gadgets[Index].Follow("inspector");
        if (!TestTrue(TEXT("The selected gadget provides an inspector"), Inspector.IsSet()))
        {
            continue;
        }
        const auto Controls = Inspector->Follow(Inspector->ObjectId() + ":controls:section");
        if (TestTrue(TEXT("The inspector provides controller bindings"), Controls.IsSet()))
        {
            const auto Fields = Controls->Describe().orderedChildren();
            if (TestEqual(TEXT("There is one activation binding"), Fields.size(), std::size_t(1)))
            {
                const auto Key = Controls->Read(*Fields.front());
                const auto* Symbol = std::get_if<magritte::Value::Symbol>(&Key.data);
                TestTrue(TEXT("The binding uses the equipped slot's controller key"),
                    Symbol && Symbol->utf8 == (Index == 0 ? "Gamepad_RightShoulder" : "Gamepad_RightTrigger"));
            }
        }
    }
    return true;
}

#endif
