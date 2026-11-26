#pragma once

#include "Blueprint/UserWidget.h"
#include "Presentation/CFMagritteBinding.h"

#include "CIInventoryView.generated.h"

class UCIInventoryTile;
class UCFMenuItem;
class UWidgetSwitcher;
struct FCFViewState;
class UCIInventoryTileTheme;
class UTexture;
class UCFMenuTheme;

/** Widget Blueprint base for a signed-in player's game profile Inventory. */
UCLASS(Blueprintable, BlueprintType)
class CATALYSTINVENTORY_API UCIInventoryView : public UUserWidget
{
    GENERATED_BODY()

public:
    // Field IDs come from the installed Users and Inventory description contract.
    void BindToPlayer(TSharedPtr<ICFPlayerMagritteSource> InSource,
        std::string InProfileFieldId, std::string InInventoryFieldId);

    const TOptional<FCFMagritteBinding>& GetInventoryBinding() const { return Inventory; }

    UFUNCTION(BlueprintPure, Category = "Catalyst|Inventory")
    bool HasInventory() const { return Inventory.IsSet(); }

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Catalyst|Inventory")
    TObjectPtr<UCIInventoryTileTheme> TileTheme;

    UPROPERTY(EditDefaultsOnly, Category = "Catalyst|Inventory")
    TMap<FString, TObjectPtr<UTexture>> ItemIcons;

    UPROPERTY(EditDefaultsOnly, Category = "Catalyst|Inventory|Layout")
    TObjectPtr<UCFMenuTheme> MenuTheme;

    UPROPERTY(EditDefaultsOnly, Category = "Catalyst|Inventory|Layout")
    FString DefaultSectionId = TEXT("gear");

    UFUNCTION(BlueprintImplementableEvent, Category = "Catalyst|Inventory")
    void OnInventoryBindingChanged();

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    friend class FCIInventoryBuilder;

    UFUNCTION()
    void PresentSection(int32 Index, UCFMenuItem* Item);

    void AttachSource();
    void DetachSource();
    void RefreshInventory();

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UCIInventoryTile> InventoryTile;

    TSharedPtr<ICFPlayerMagritteSource> Source;
    FDelegateHandle SourceChanged;
    TOptional<FCFMagritteBinding> Inventory;
    TSharedPtr<FCFViewState> ViewState;
    TArray<const magritte::Description*> SectionDescriptions;

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> MainPanel;
    std::string ProfileFieldId;
    std::string InventoryFieldId;
    bool bConstructed = false;
};
