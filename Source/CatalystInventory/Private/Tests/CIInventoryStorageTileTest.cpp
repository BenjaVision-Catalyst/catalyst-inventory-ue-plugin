#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CIEquipmentTile.h"
#include "CIInventoryProfile.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Presentation/CFMagritteBinding.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCIInventoryStorageTileTest, "Catalyst.Inventory.StorageTile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCIInventoryStorageTileTest::RunTest(const FString&)
{
    const auto Card = std::make_shared<magritte::Description>();
    Card->id = "equipmentCard";
    Card->className = "MAPriorityContainer";
    for (const auto* Id : {"inventoryTypeCount", "storageCapacity", "presentationKind"})
    {
        auto Field = std::make_shared<magritte::Description>();
        Field->id = Id;
        Field->className = std::string_view(Id) == "presentationKind" ? "MAStringDescription" : "MANumberDescription";
        Card->children.push_back(Field);
    }
    const auto Bind = [&](UCIEquipmentTile& Tile, std::int64_t Count, TOptional<std::int64_t> Capacity) {
        auto Graph = std::make_shared<magritte::Graph>();
        auto& Object = Graph->objects["modStorage"];
        Object.id = "modStorage";
        Object.description = Card;
        Object.values = {{"presentationKind", magritte::Value("gear")},
            {"inventoryTypeCount", magritte::Value(Count)}};
        if (Capacity.IsSet())
        {
            Object.values.emplace("storageCapacity", magritte::Value(*Capacity));
        }
        Tile.BindItem(FCFMagritteBinding::Find(Graph, Object.id).GetValue());
    };
    TStrongObjectPtr<UCIEquipmentTile> Tile(NewObject<UCIEquipmentTile>());
    Bind(*Tile, 0, 100);

    const auto FindUsage = [](const auto& Search, const TSharedRef<SWidget>& Widget) -> TSharedPtr<STextBlock> {
        if (Widget->GetTag() == TEXT("InventoryTileTypeCount"))
        {
            return StaticCastSharedRef<STextBlock>(Widget);
        }
        auto* Children = Widget->GetChildren();
        for (int32 Index = 0; Index < Children->Num(); ++Index)
        {
            if (auto Result = Search(Search, Children->GetChildAt(Index)))
            {
                return Result;
            }
        }
        return nullptr;
    };
    const auto Usage = FindUsage(FindUsage, Tile->TakeWidget());
    if (TestTrue(TEXT("The equipment tile renders the model's count field"), Usage.IsValid()))
    {
        TestTrue(TEXT("Empty storage still displays capacity"), Usage->GetVisibility().IsVisible());
        TestEqual(TEXT("Empty Mods displays 0/100"), Usage->GetText().ToString(), FString(TEXT("0/100")));
        Bind(*Tile, 7, 120);
        TestEqual(TEXT("The same widget reads updated usage and capacity"), Usage->GetText().ToString(), FString(TEXT("7/120")));
        Bind(*Tile, 0, 0);
        TestEqual(TEXT("An explicit zero allowance displays 0/0"), Usage->GetText().ToString(), FString(TEXT("0/0")));
        Bind(*Tile, 2, {});
        TestEqual(TEXT("Equipment without a capacity still displays the plain count"), Usage->GetText().ToString(), FString(TEXT("2")));
        Bind(*Tile, 0, {});
        TestTrue(TEXT("An empty ordinary count shows no text"), Usage->GetText().IsEmpty());

        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CatalystInventory"));
        FString Json;
        if (!TestTrue(TEXT("Load the server gear fixture"), Plugin &&
            FFileHelper::LoadFileToString(Json, *(Plugin->GetBaseDir() / TEXT("Tests/Fixtures/CatalogGearProfile.json")))))
        {
            return false;
        }
        const auto User = CIInventoryProfile::DecodeUser(Json, TEXT("inventory-fixture"));
        const auto Profile = User ? User->Follow("profile") : TOptional<FCFMagritteBinding>();
        const auto Inventory = Profile ? Profile->Follow("inventory") : TOptional<FCFMagritteBinding>();
        const auto Gear = Inventory ? Inventory->Follow("gear") : TOptional<FCFMagritteBinding>();
        const auto Equipment = Gear ? Gear->Related("equippedGear", 6) : TArray<FCFMagritteBinding>();
        if (!TestEqual(TEXT("Server equipment includes Mod Storage"), Equipment.Num(), 6))
        {
            return false;
        }
        Tile->BindItem(Equipment.Last());
        TestEqual(TEXT("The server allowance restores the empty Mod Storage label"),
            Usage->GetText().ToString(), FString(TEXT("0/100")));
    }
    return true;
}
#endif
