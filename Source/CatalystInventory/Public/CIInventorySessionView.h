#pragma once

#include "Blueprint/UserWidget.h"
#include "CIInventorySessionView.generated.h"

class ICFPlayerMagritteSource;
class UCIInventoryView;
class USizeBox;
struct FCFViewState;

/** Hosts the current local player's Inventory presentation as their profile changes. */
UCLASS(BlueprintType)
class CATALYSTINVENTORY_API UCIInventorySessionView : public UUserWidget
{
    GENERATED_BODY()

public:
    void BindToPlayer(TSharedPtr<ICFPlayerMagritteSource> InSource,
        TSubclassOf<UCIInventoryView> InViewClass = {});

    UFUNCTION(BlueprintPure, Category = "Catalyst|Inventory")
    UCIInventoryView* GetInventoryView() const;

protected:
    TSharedRef<SWidget> RebuildWidget() override;
    void NativeConstruct() override;
    void NativeDestruct() override;

private:
    void Refresh();
    void Unsubscribe();

    UPROPERTY(Transient)
    TObjectPtr<USizeBox> Panel;

    UPROPERTY(Transient)
    TSubclassOf<UCIInventoryView> ViewClass;

    TSharedPtr<ICFPlayerMagritteSource> Source;
    TSharedPtr<FCFViewState> ViewState;
    FDelegateHandle Changed;
};
