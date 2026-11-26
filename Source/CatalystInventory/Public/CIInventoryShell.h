#pragma once

#include "Components/PanelWidget.h"
#include "CIInventoryShell.generated.h"

class UCFMenu;
class UCFMenuItem;
class UCIInventoryTileTheme;
class UWidgetSwitcher;

/** Projects the Inventory menu and active tab panel into viewport space. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIInventoryShell : public UPanelWidget
{
    GENERATED_BODY()

public:
    void SetLayoutTheme(UCIInventoryTileTheme* InTheme);

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

    TSharedPtr<class SCIInventoryShell> Shell;
};
