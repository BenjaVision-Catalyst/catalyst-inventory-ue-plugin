#pragma once

#include "Components/ContentWidget.h"
#include "CIInventoryShell.generated.h"

/** Projects the Inventory panels and their menu into viewport space. */
UCLASS(BlueprintType, Blueprintable)
class CATALYSTINVENTORY_API UCIInventoryShell : public UContentWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Layout")
    FLinearColor PanelOutline = FLinearColor(.45f, .78f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Layout", meta = (ClampMin = "0.0"))
    float PanelOutlineThickness = 4.f;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void OnSlotAdded(UPanelSlot* Slot) override;
    virtual void OnSlotRemoved(UPanelSlot* Slot) override;

private:
    void UpdateMenu();

    TSharedPtr<class SCIInventoryShell> Shell;
};
