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
    TestTrue(TEXT("Decode the uncompressed preview JSON"), CIInventoryProfile::DecodeUser(Json, TEXT("inventory-fixture")).IsSet());
    TArray<uint8> Body;
    if (!TestTrue(TEXT("Load the gateway's compressed profile"),
        FFileHelper::LoadFileToArray(Body, *(Plugin->GetBaseDir() / TEXT("Tests/Fixtures/CatalogGadgetProfile.reply")))))
    {
        return false;
    }
    TestFalse(TEXT("Reject a different session identity"), CIInventoryProfile::DecodeReply(Body, TEXT("another-user")).IsSet());
    TestFalse(TEXT("Reject a truncated ZLIB envelope"), CIInventoryProfile::DecodeReply(MakeArrayView(Body.GetData(), 7),
        TEXT("inventory-fixture")).IsSet());
    auto Oversized = Body;
    Oversized[7] = 1;
    TestFalse(TEXT("Reject an oversized graph before decompression"),
        CIInventoryProfile::DecodeReply(Oversized, TEXT("inventory-fixture")).IsSet());
    const auto User = CIInventoryProfile::DecodeReply(Body, TEXT("inventory-fixture"));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventoryCatalogGearTest, "Catalyst.Inventory.CatalogGear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCIInventoryCatalogGearTest::RunTest(const FString&)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystInventory"));
    FString Json;
    if (!TestTrue(TEXT("Load a profile with catalog gear and gadgets"), Plugin &&
        FFileHelper::LoadFileToString(Json, *(Plugin->GetBaseDir() / TEXT("Tests/Fixtures/CatalogGearProfile.json")))))
    {
        return false;
    }
    const auto User = CIInventoryProfile::DecodeUser(Json, TEXT("inventory-fixture"));
    if (!TestTrue(TEXT("Decode the server-authored profile"), User.IsSet()))
    {
        return false;
    }
    const auto Gear = User->Follow("profile")->Follow("inventory")->Follow("gear");
    if (!TestTrue(TEXT("The profile includes Equipment"), Gear.IsSet()))
    {
        return false;
    }
    const auto Equipped = Gear->Related("equippedGear", 16);
    struct FExpectedItem
    {
        int32 Index;
        int32 Attributes;
        int32 Sockets;
        const char* Core;
    };
    const FExpectedItem Expected[] = {{1, 2, 2, "+80"}, {2, 2, 1, "+1"}, {4, 1, 0, "+50"}};
    for (const auto& Piece : Expected)
    {
        if (!TestTrue(TEXT("The occupied slot is present"), Equipped.IsValidIndex(Piece.Index)))
        {
            return false;
        }
        const auto Inspector = Equipped[Piece.Index].Follow("inspector");
        if (!TestTrue(TEXT("Gear exposes the generic inspector"), Inspector.IsSet()))
        {
            continue;
        }
        const auto Core = Inspector->Follow(Inspector->ObjectId() + ":primary:section");
        const auto Attributes = Inspector->Follow(Inspector->ObjectId() + ":modifiers:section");
        const auto Mods = Inspector->Follow(Inspector->ObjectId() + ":slots:section");
        if (TestTrue(TEXT("Gear exposes its rolled core"), Core.IsSet()))
        {
            const auto Fields = Core->Describe().orderedChildren();
            if (TestEqual(TEXT("There is one core attribute"), int32(Fields.size()), 1))
            {
                const auto Value = Core->Read(*Fields.front());
                const auto* Text = std::get_if<std::string>(&Value.data);
                TestTrue(TEXT("The core value comes from the owned gear"), Text && *Text == Piece.Core);
            }
        }
        TestEqual(TEXT("Show each rolled secondary attribute"),
            Attributes ? int32(Attributes->Describe().children.size()) : 0, Piece.Attributes);
        TestEqual(TEXT("Show only the supported mod sockets"),
            Mods ? int32(Mods->Describe().children.size()) : 0, Piece.Sockets);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventoryCatalogWeaponsTest, "Catalyst.Inventory.CatalogWeapons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCIInventoryCatalogWeaponsTest::RunTest(const FString&)
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystInventory"));
    FString Json;
    if (!TestTrue(TEXT("Load the catalog weapon profile"), Plugin &&
        FFileHelper::LoadFileToString(Json, *(Plugin->GetBaseDir() / TEXT("Tests/Fixtures/CatalogWeaponProfile.json")))))
    {
        return false;
    }
    const auto User = CIInventoryProfile::DecodeUser(Json, TEXT("inventory-fixture"));
    if (!TestTrue(TEXT("Decode the weapon profile"), User.IsSet()))
    {
        return false;
    }
    const auto Inventory = User->Follow("profile")->Follow("inventory");
    const auto Gear = Inventory->Follow("gear");
    if (!TestTrue(TEXT("The combined preview keeps the gadget and gear slots"), Gear
        && Gear->Related("gadgets", 2).Num() == 2 && Gear->Related("equippedGear", 6).Num() == 6))
    {
        return false;
    }
    const auto Weapons = Gear ? Gear->Related("toolsWeapons", 4) : TArray<FCFMagritteBinding>();
    if (!TestEqual(TEXT("Populate the melee, off-hand and ranged slots"), Weapons.Num(), 3))
    {
        return false;
    }

    const auto Resources = Inventory->Follow("resources");
    if (TestTrue(TEXT("The catalog includes resource stacks"), Resources.IsSet()))
    {
        const auto CheckMultipleStacks = [&](const char* Group, const char* Definition)
        {
            TSet<FString> StackIds;
            for (const auto& Item : Resources->Related(Group, 16))
            {
                for (const auto& Field : Item.Describe().orderedChildren())
                {
                    if (Field->id == "definitionIdentifier")
                    {
                        const auto Value = Item.Read(*Field);
                        if (const auto* Id = std::get_if<std::string>(&Value.data); Id && *Id == Definition)
                        {
                            StackIds.Add(UTF8_TO_TCHAR(Item.ObjectId().c_str()));
                        }
                    }
                }
            }
            TestEqual(TEXT("The resource definition has two distinct stacks"), StackIds.Num(), 2);
        };
        CheckMultipleStacks("rock", "Shale");
        CheckMultipleStacks("wood", "Pine");
        TestEqual(TEXT("Garden starts with Seeds only"), Resources->Related("garden", 16).Num(), 1);
        TestTrue(TEXT("Building resources are deferred"), Resources->Related("building", 16).IsEmpty());
    }

    const char* Damage[] = {"52.8", "34.0", "28.0"};
    const int32 AttachmentCount[] = {0, 2, 3};
    const int64 Accuracy[] = {0, 72, 82};
    const int64 DamageRollFill[] = {666667, 416667, 800000};
    const auto ReadMeterFill = [](const magritte::Description& Field)
    {
        const auto Value = Field.property("metadata:ciMeterMillionths");
        const auto* Millionths = std::get_if<std::int64_t>(&Value.data);
        return Millionths ? *Millionths : std::int64_t(-1);
    };
    for (int32 Index = 0; Index < Weapons.Num(); ++Index)
    {
        const auto Inspector = Weapons[Index].Follow("inspector");
        if (!TestTrue(TEXT("An occupied weapon slot has an inspector"), Inspector.IsSet()))
        {
            continue;
        }
        const auto Stats = Inspector->Follow(Inspector->ObjectId() + ":stats:section");
        const auto Core = Inspector->Follow(Inspector->ObjectId() + ":primary:section");
        const auto Handling = Inspector->Follow(Inspector->ObjectId() + ":meters:section");
        const auto Falloff = Inspector->Follow(Inspector->ObjectId() + ":falloff:section");
        TestEqual(TEXT("Only ranged weapons have a falloff curve"),
            Falloff ? int32(Falloff->Describe().children.size()) : 0, Index == 0 ? 0 : 4);
        if (Falloff && Falloff->Describe().children.size() == 4)
        {
            const auto& Curve = Falloff->Describe().children;
            const auto ReadDistance = [](const magritte::Description& Field)
            {
                const auto Value = Field.property("metadata:ciDistanceMetres");
                const auto* Metres = std::get_if<std::int64_t>(&Value.data);
                return Metres ? *Metres : std::int64_t(-1);
            };
            TestEqual(TEXT("Full damage extends to this weapon's optimal range"),
                ReadDistance(*Curve[1]), std::int64_t(Index == 1 ? 18 : 32));
            TestEqual(TEXT("The curve ends at the weapon's longest plotted range"),
                ReadDistance(*Curve.back()), std::int64_t(Index == 1 ? 60 : 100));
            const auto Value = Falloff->Read(*Curve.back());
            const auto* Retained = std::get_if<std::int64_t>(&Value.data);
            TestTrue(TEXT("The curve shows the catalog's retained damage"), Retained && *Retained == (Index == 1 ? 55 : 65));
        }
        const auto Attachments = Inspector->Follow(Inspector->ObjectId() + ":slots:section");
        if (TestTrue(TEXT("The owned weapon exposes its core roll"), Core && !Core->Describe().children.empty()))
        {
            TestEqual(TEXT("The weapon damage meter uses the catalog roll maximum"),
                ReadMeterFill(*Core->Describe().children.front()), DamageRollFill[Index]);
        }
        TestEqual(TEXT("The attachment card uses the type's sockets"),
            Attachments ? int32(Attachments->Describe().children.size()) : 0, AttachmentCount[Index]);
        TestEqual(TEXT("Only ranged weapons have accuracy and stability"),
            Handling ? int32(Handling->Describe().children.size()) : 0, Index == 0 ? 0 : 2);
        if (Stats && !Stats->Describe().children.empty())
        {
            const auto Value = Stats->Read(*Stats->Describe().children.front());
            const auto* Text = std::get_if<std::string>(&Value.data);
            TestTrue(TEXT("Effective damage includes the weapon's own roll"), Text && *Text == Damage[Index]);
        }
        else
        {
            AddError(TEXT("Weapon stats are missing."));
        }
        if (Handling && !Handling->Describe().children.empty())
        {
            const auto Value = Handling->Read(*Handling->Describe().children.front());
            const auto* Amount = std::get_if<std::int64_t>(&Value.data);
            TestTrue(TEXT("Handling is a numeric value suitable for a meter"), Amount && *Amount == Accuracy[Index]);
        }
    }
    for (const auto& Card : Gear->Related("equippedGear", 6))
    {
        const auto Inspector = Card.Follow("inspector");
        if (Inspector && Inspector->ObjectId() == "item:pack-fixture:inspector")
        {
            const auto Core = Inspector->Follow(Inspector->ObjectId() + ":primary:section");
            const auto Rolls = Inspector->Follow(Inspector->ObjectId() + ":modifiers:section");
            if (TestTrue(TEXT("The pack has core and rolled attributes"), Core && Rolls
                && Core->Describe().children.size() == 1 && Rolls->Describe().children.size() == 2))
            {
                TestEqual(TEXT("80 armor fills 80% of the 100 armor track"),
                    ReadMeterFill(*Core->Describe().children.front()), int64(800000));
                TestEqual(TEXT("8% hazard resistance fills two thirds of its 12% track"),
                    ReadMeterFill(*Rolls->Describe().children.front()), int64(666667));
            }
        }
    }
    if (FApp::CanEverRender())
    {
        const FCFViewContext Context{*GetTransientPackage(), MakeShared<FCFViewState>()};
        TStrongObjectPtr<UCIInventoryView> View(Cast<UCIInventoryView>(FCIInventoryModel(*Inventory).AsView(Context)));
        if (!TestNotNull(TEXT("Build a preview with the weapon inspector"), View.Get()))
        {
            return false;
        }
        const auto ViewWidget = View->TakeWidget();
        auto* Shell = View->GetInventoryShell();
        const auto Widget = Shell->TakeWidget();
        const auto Geometry = FGeometry::MakeRoot({1280.0, 720.0}, FSlateLayoutTransform());
        Widget->Tick(Geometry, 0.0, .2f);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            Widget->OnKeyDown(Geometry, FKeyEvent(EKeys::Right, FModifierKeysState(), 0, false, 0, 0));
        }
        Widget->Tick(Geometry, .2, .2f);
        TestEqual(TEXT("The carbine is focused"), Shell->GetInspectorLabelForTest().ToString(),
            FString(TEXT("Pathfinder Carbine")));
        TestEqual(TEXT("Draw three attribute and handling meters, two plot rules, and one curve"),
            Shell->GetInspectorStrokeCountForTest(), 9);

        FString EquippedIdentifier;
        Shell->SetSlotPickerSource([&](const FCFMagritteBinding&)
        {
            FCIInventorySlotPicker Picker;
            Picker.Choices = {{TEXT("equipped"), FText::FromString(TEXT("Pathfinder Carbine")), true, Weapons[2]},
                {TEXT("reserve"), FText::FromString(TEXT("Relay Sidearm")), false, Weapons[1]}};
            Picker.Equip = [&](const FString& Identifier) { EquippedIdentifier = Identifier; return true; };
            return TOptional<FCIInventorySlotPicker>(MoveTemp(Picker));
        });
        const auto Press = [&](const FKey& Key)
        {
            return Widget->OnKeyDown(Geometry, FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled();
        };
        TestTrue(TEXT("Confirm opens a slot picker for the focused tile"), Press(EKeys::Enter));
        Widget->Tick(Geometry, .4, .2f);
        TestTrue(TEXT("Back cancels item picking"), Press(EKeys::BackSpace));
        TestTrue(TEXT("Cancelling leaves the equipped item alone"), EquippedIdentifier.IsEmpty());
        Press(EKeys::Enter);
        Press(EKeys::Down);
        Press(EKeys::Enter);
        TestEqual(TEXT("The picker equips the selected compatible item"), EquippedIdentifier, FString(TEXT("reserve")));

        Shell->SetSlotPickerSource([&](const FCFMagritteBinding&)
        {
            FCIInventorySlotPicker Picker;
            for (int32 Index = 0; Index < 8; ++Index)
            {
                Picker.Choices.Add({FString::FromInt(Index), FText::AsNumber(Index), Index == 0, Weapons[2]});
            }
            Picker.Equip = [&](const FString& Identifier) { EquippedIdentifier = Identifier; return true; };
            return TOptional<FCIInventorySlotPicker>(MoveTemp(Picker));
        });
        TestTrue(TEXT("Open the overflowing picker"), Press(EKeys::Enter));
        for (int32 Index = 0; Index < 7; ++Index)
        {
            Press(EKeys::Down);
            Widget->Tick(Geometry, .6 + Index * .2, .2f);
        }
        Press(EKeys::Enter);
        TestEqual(TEXT("Navigation reaches choices below the visible rows"), EquippedIdentifier, FString(TEXT("7")));
        Shell->SetSlotPickerSource({});
        Press(EKeys::E);
        Widget->Tick(Geometry, 2.1, .2f);
        TestEqual(TEXT("Resources opens the focused stack inspector"), Shell->GetInspectorLabelForTest().ToString(),
            FString(TEXT("Credits")));
        const auto Release = [&](const FKey& Key)
        {
            Widget->OnKeyUp(Geometry, FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
        };
        TestTrue(TEXT("R1 opens resource categories"), Press(EKeys::Gamepad_RightShoulder));
        Release(EKeys::Gamepad_RightShoulder);
        Widget->Tick(Geometry, 2.2, .2f);
        TestEqual(TEXT("Opening categories keeps the focused stack"), Shell->GetInspectorLabelForTest().ToString(),
            FString(TEXT("Credits")));
        Press(EKeys::Down);
        Widget->Tick(Geometry, 2.3, .2f);
        TestTrue(TEXT("R1 closes resource categories"), Press(EKeys::Gamepad_RightShoulder));
        Release(EKeys::Gamepad_RightShoulder);
        TestEqual(TEXT("Selecting Keys focuses the first key"), Shell->GetInspectorLabelForTest().ToString(),
            FString(TEXT("Depot Key")));
        Press(EKeys::Q);
        Widget->Tick(Geometry, 2.5, .2f);
        Press(EKeys::E);
        Widget->Tick(Geometry, 2.7, .2f);
        TestEqual(TEXT("Resources restores the selected stack after changing tabs"),
            Shell->GetInspectorLabelForTest().ToString(), FString(TEXT("Depot Key")));
    }
    return true;
}

#endif
