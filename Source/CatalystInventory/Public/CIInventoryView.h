#pragma once

#include "Blueprint/UserWidget.h"
#include "Presentation/CFMagritteBinding.h"
#include "Styling/SlateBrush.h"

#include "CIInventoryView.generated.h"

class UCIInventoryTile;
class UCIInventoryTileTheme;
class UTexture;
class UCFMenuTheme;

/** Widget Blueprint base for a signed-in player's game profile Inventory. */
UCLASS(Abstract, Blueprintable, BlueprintType)
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
    TMap<FName, FSlateBrush> MenuIcons;

    UFUNCTION(BlueprintImplementableEvent, Category = "Catalyst|Inventory")
    void OnInventoryBindingChanged();

    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    void AttachSource();
    void DetachSource();
    void RefreshInventory();

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UCIInventoryTile> InventoryTile;

    TSharedPtr<ICFPlayerMagritteSource> Source;
    FDelegateHandle SourceChanged;
    TOptional<FCFMagritteBinding> Inventory;
    std::string ProfileFieldId;
    std::string InventoryFieldId;
    bool bConstructed = false;
};
