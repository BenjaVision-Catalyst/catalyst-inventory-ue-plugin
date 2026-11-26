#pragma once

#include "Components/PanelWidget.h"
#include "CIInventoryShell.generated.h"

class UCFMenu;
class UCFMenuItem;
class UWidgetSwitcher;

/** Projects the Inventory menu, tab panels and inspector outline into viewport space. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIInventoryShell : public UPanelWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Layout")
    FLinearColor PanelOutline = FLinearColor(.45f, .78f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Layout", meta = (ClampMin = "0.0"))
    float PanelOutlineThickness = 4.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Layout")
    FLinearColor MainPanelOutline = FLinearColor(1.f, .72f, .33f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Layout", meta = (ClampMin = "0.0"))
    float MainPanelOutlineThickness = 3.f;

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

    TWeakObjectPtr<UCFMenu> BoundMenu;

    TSharedPtr<class SCIInventoryShell> Shell;
};
