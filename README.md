# Catalyst Inventory Unreal plugin

`CatalystInventory` is the runtime module for the Unreal companion to
[Catalyst Inventory](https://github.com/BenjaVision-Catalyst/catalyst-inventory).
Create a Widget Blueprint derived from `CIInventoryView` in the UMG Designer
to author an Inventory composition. The base does not choose a layout, start
a connection or require a player or ACF character, so the composition can be
opened independently in the Designer.

At runtime, call `BindToPlayer` with the game session's
`ICFPlayerMagritteSource` and the installed description IDs for the profile
and inventory relations. The source supplies the authorized User graph for the
widget's owning local player, with the current game profile selected. Publish
graph replacements and sign-out through `OnChanged` on the game thread. The
widget follows both relations and broadcasts `OnInventoryBindingChanged`.
`HasInventory` is false while any part of the graph is unavailable.
