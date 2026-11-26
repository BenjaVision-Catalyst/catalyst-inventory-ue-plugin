#pragma once

#include "Components/PanelWidget.h"
#include "CIInventoryShell.generated.h"

class UCFMenu;
class UCFMenuItem;
class UCIInventoryTileTheme;
class UWidgetSwitcher;

struct FCIInventorySavedSet
{
    FString Name;
    FString Identifier;
};

/** Projects the Inventory menu and active tab panel into viewport space. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIInventoryShell : public UPanelWidget
{
    GENERATED_BODY()

public:
    void SetLayoutTheme(UCIInventoryTileTheme* InTheme);
    void SetActionSection(FName Section);
    void SetSavedSets(TArray<FCIInventorySavedSet> Sets);
    void SetCreateSavedSetAction(TFunction<void(bool)> Action);
    void SetEquipSavedSetAction(TFunction<void(FString)> Action);
    void RestoreSavedSetsState(const UCIInventoryShell& Previous);
    /** Bind Create or Equip for the selected row. An unbound action is disabled. */
    void SetSavedSetConfirmAction(bool bEquip, FSimpleDelegate Action);

    UFUNCTION(BlueprintPure, Category = "Inventory|Layout")
    UCFMenu* GetMenu() const;

    UFUNCTION(BlueprintPure, Category = "Inventory|Layout")
    UWidgetSwitcher* GetMainPanel() const;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void OnSlotAdded(UPanelSlot* Slot) override;
    virtual void OnSlotRemoved(UPanelSlot* Slot) override;

private:
    void UpdateContent();
    void UnbindMenu();

    UFUNCTION()
    void OnMenuSelectionChanged(int32 Index, UCFMenuItem* Item);

    UPROPERTY(Transient)
    TObjectPtr<UCIInventoryTileTheme> LayoutTheme;

    TWeakObjectPtr<UCFMenu> BoundMenu;
    FName ActionSection;
    TArray<FCIInventorySavedSet> SavedSets;
    TFunction<void(bool)> CreateSavedSet;
    TFunction<void(FString)> EquipSavedSet;
    FSimpleDelegate SavedSetConfirm;
    bool bConfirmEquips = false;
    bool bRestoreSavedSetsOpen = false;
    int32 RestoredSavedSetIndex = 0;

    TSharedPtr<class SCIInventoryShell> Shell;
};
