# Catalyst Inventory Unreal plugin

`CatalystInventory` is the runtime module for the Unreal companion to
[Catalyst Inventory](https://github.com/BenjaVision-Catalyst/catalyst-inventory).
Create a Widget Blueprint derived from `CIInventoryView` in the UMG Designer
to author an Inventory composition. The view builds a single Inventory tile
when it has no authored root. An authored composition can name a
`CIInventoryTile` widget `InventoryTile` to bind that widget to the first item.
The view can be opened independently in the Designer without a connection.

At runtime, call `BindToPlayer` with the game session's
`ICFPlayerMagritteSource` and the installed description IDs for the profile
and inventory relations. The source supplies the authorized User graph for the
widget's owning local player, with the current game profile selected. Publish
graph replacements and sign-out through `OnChanged` on the game thread. The
widget follows both relations and broadcasts `OnInventoryBindingChanged`.
`HasInventory` is false while any part of the graph is unavailable.

The tile reads `tierId`, `displayName` and optional `count` from the item
description. Assign icons in `ItemIcons` by Catalyst definition identifier.
Create a `CIInventoryTileTheme` Data Asset and assign it to the view's
`TileTheme` or a standalone tile's `Theme`. It controls the frame, focus
fill, backdrop blur, tier palette and band width, text fonts, tints, minimum
size and spacing. One spacing value controls the outer inset and the gaps
between the icon and visible labels. The icon preserves its aspect ratio
while filling the available area.
