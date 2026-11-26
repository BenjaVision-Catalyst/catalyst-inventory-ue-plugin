#pragma once

#include "Components/PanelWidget.h"
#include "Presentation/CFControllerHold.h"
#include "CIInventoryShell.generated.h"

class UCFMenu;
class UCFMenuItem;
class UCIInventoryTileTheme;
class UWidgetSwitcher;
struct FCIInventorySavedSetsPresentation;
struct FCIInventoryNavigationInput;

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
    void SetOverwriteSavedSetAction(TFunction<void(FString)> Action);
    void SetDeleteSavedSetAction(TFunction<void(FString)> Action);
    void SetRenameSavedSetAction(TFunction<bool(FString, FString)> Action);
    void CloseSavedSetOptions();
    void SetCloseAction(FSimpleDelegate Action);
    void RestoreSavedSetsState(const UCIInventoryShell& Previous);
#if WITH_DEV_AUTOMATION_TESTS
    FIntPoint GetFocusedGridCellForTest() const;
    FText GetInspectorLabelForTest() const;
#endif
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
    TFunction<void(FString)> OverwriteSavedSet;
    TFunction<void(FString)> DeleteSavedSet;
    TFunction<bool(FString, FString)> RenameSavedSet;
    FSimpleDelegate SavedSetConfirm;
    FSimpleDelegate CloseAction;
    bool bConfirmEquips = false;
    bool bRestoreSavedSetsOpen = false;
    int32 RestoredSavedSetIndex = 0;
    FIntPoint RestoredGridFocus = FIntPoint::ZeroValue;
    TMap<FName, TPair<FIntPoint, FString>> RestoredTabSelections;
    TSharedPtr<const FCIInventoryNavigationInput> RestoredNavigationInput;
    TSharedPtr<const FCIInventorySavedSetsPresentation> RestoredSavedSetsPresentation;
    TOptional<FCFControllerHold> RestoredOverwriteConfirmation;
    TOptional<FCFControllerHold> RestoredDeleteConfirmation;

    TSharedPtr<class SCIInventoryShell> Shell;
};
