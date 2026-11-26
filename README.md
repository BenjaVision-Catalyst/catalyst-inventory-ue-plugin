# Catalyst Inventory Unreal plugin

`CatalystInventory` is the runtime module for the Unreal companion to
[Catalyst Inventory](https://github.com/BenjaVision-Catalyst/catalyst-inventory).
Construct an `FCIInventoryModel` from the authorized Inventory binding and
call its `AsView` with the local player's presentation context. The
description's local builder returns a `UCIInventoryView`. The scaffold
presents the immediate, visible to-one descriptions as tabs in Magritte
priority order. Each tab uses the description label and resolves its icon
through the `UCFMenuTheme` icon table. Reuse the presentation context's state
when reopening the view to restore the selected description for that Inventory.
The selected description reads the section object whose view fills the shared
panel.

For signed-in play, mount a CIInventorySessionView and bind the current local
player's Users presentation source. The session view follows User, game
profile, and Inventory, opens the Inventory through AsView, and clears it when
the source has no authorized profile. The three item sections share Foundation's
CFSpanGrid. Apparel and Gear start at six columns and Resources at four.
SetHalfSize doubles the column count while keeping each item's span.

An independently authored Widget Blueprint can still use the named
`InventoryTile` binding and the game session's `ICFPlayerMagritteSource`
through `BindToPlayer`. Publish graph replacements and sign-out through
`OnChanged` on the game thread.

The tile reads `displayName` and either an individual item's `tierId` or a
stack's `count`. Assign icons in `ItemIcons` by Catalyst definition identifier.
Create a `CIInventoryTileTheme` Data Asset and assign it to the view's
`TileTheme` or a standalone tile's `Theme`. It controls the frame, focus
fill, backdrop blur, tier palette, text fonts, tints, minimum size and spacing. One spacing value controls the outer inset and the gaps
between the icon and visible labels. The icon preserves its aspect ratio
inside the theme's `IconSize` box.

Compact tiles and rows use the same 4-unit tier band by default. To change the
width for a presentation, assign the same `CIInventoryTierStyle` Data Asset to
each theme's `TierStyle` and edit `BandWidth` on that shared asset. The band gets
its own layout space before the tile content. Tiles with no tier, including
stacks and gadgets, use the full width for content.

Use `CIEquipmentTile` for individual tools, weapons, gear and gadgets. The
equipment row supplies `presentationKind`, an Inventory-wide
`inventoryTypeCount`, ordered `modSlots`, and offensive, defensive and utility
attribute ID lists. Each slot relates to a row with `slotIdentifier` and
optional `equippedModIdentifier`. Brand, talent and expertise are optional.
Equipment theme assets map brand, talent and attribute IDs to local art.

Use `CIWeaponToolRow` in a tool or weapon list. Bind the item with
`BindItem` and pass the list's selected item to `SetComparisonItem`.
Update the comparison item when selection changes and clear it when the
list has no selection. The widget matches numeric stats by Catalyst
identifier. Higher and lower values have distinct tints and arrows.
Equal or unavailable comparisons keep the normal value style.

The row accepts the compact equipment fields plus `consumableTypeIdentifier`,
`requiredPlayerLevel`, `isProficient`, `maximumAttributes`,
`talentIdentifiers` and ordered `comparisonStats`. Each comparison stat
has a `statIdentifier`, `displayName` and numeric `value`. The server
supplies the identifiers of attributes at maximum. Create a
`CIWeaponToolRowTheme` to tune the layout and resolve the consumable,
proficiency and talent art locally.

Use `CIGearRow` for an individual gear item. It uses the same Equipment Row
layout and Inventory tier band as tool and weapon rows. `CIGearRowTheme`
resolves local brand, gear-slot, talent and junk art. The gear summary supplies
the brand name, optional primary attribute, required player level and item
state. The primary attribute specifies its identifier, display name, type,
numeric value, format and maximum state. Use a fractional value for
`signedPercent` (for example, 0.15 displays as +15%). Secondary attribute and
talent indicators use the shared Equipment Row rendering.

`isJunk` is a read-only state for gear presentation. The row displays that
state when supplied by the model.

The apparel grid uses `CIInventoryTile` with the category identifier, owned
count, tier and optional equipped definition identifier. Positive owned counts
appear in the top left. The view resolves equipped art by definition identifier
through `ItemIcons`. A standalone presenter can assign `Icon` after binding,
including a render target for a model preview. Empty slots use the theme's
`SlotIcons` or its default category glyph.

Use `CIResourceTile` for a stack. Bind the resource definition identifier,
optional display name and stack count. The same icon slot accepts a model
preview or resource icon. The resource theme can hide the label and supply a
fallback icon when the presenter has no definition-specific art.

Use `CIApparelRow` for an individual apparel item in a list. Bind the item
summary's definition identifier, display name, tier and optional brand name.
The apparel row theme uses the equipment row's visual defaults with a shorter
layout. The view resolves preview art by definition identifier.
