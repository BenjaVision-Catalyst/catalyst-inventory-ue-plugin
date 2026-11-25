#include "CIInventoryView.h"

#include "Engine/LocalPlayer.h"

void UCIInventoryView::BindToPlayer(TSharedPtr<ICFPlayerMagritteSource> InSource,
    std::string InProfileFieldId, std::string InInventoryFieldId)
{
    DetachSource();
    Source = MoveTemp(InSource);
    ProfileFieldId = std::move(InProfileFieldId);
    InventoryFieldId = std::move(InInventoryFieldId);
    if (bConstructed)
    {
        AttachSource();
    }
}

void UCIInventoryView::NativeConstruct()
{
    Super::NativeConstruct();
    bConstructed = true;
    AttachSource();
}

void UCIInventoryView::NativeDestruct()
{
    bConstructed = false;
    DetachSource();
    Inventory.Reset();
    Super::NativeDestruct();
}

void UCIInventoryView::AttachSource()
{
    if (Source)
    {
        SourceChanged = Source->OnChanged().AddUObject(this, &UCIInventoryView::RefreshInventory);
    }
    RefreshInventory();
}

void UCIInventoryView::DetachSource()
{
    if (Source && SourceChanged.IsValid())
    {
        Source->OnChanged().Remove(SourceChanged);
        SourceChanged.Reset();
    }
}

void UCIInventoryView::RefreshInventory()
{
    const ULocalPlayer* Player = GetOwningLocalPlayer();
    Inventory.Reset();
    if (Player && Source)
    {
        const auto User = Source->SignedInUser(*Player);
        if (User)
        {
            const auto Profile = User->Follow(ProfileFieldId);
            if (Profile)
            {
                Inventory = Profile->Follow(InventoryFieldId);
            }
        }
    }
    OnInventoryBindingChanged();
}
