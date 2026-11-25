# Catalyst Inventory Unreal plugin

`CatalystInventory` is the runtime module for the Unreal companion to
[Catalyst Inventory](https://github.com/BenjaVision-Catalyst/catalyst-inventory).
Create a Widget Blueprint derived from `CIInventoryView` in the UMG Designer
to author an Inventory composition. The base does not choose a layout, start
a connection or require a player or ACF character, so the composition can be
opened independently in the Designer. The running game will supply the live
Magritte presentation context when that binding is implemented.
