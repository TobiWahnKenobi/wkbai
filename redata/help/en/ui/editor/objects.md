---
title: Objects
ids: [ui/editor/objects, ui/editor/objects/place#place, ui/editor/objects/class#class, ui/editor/objects/player#player, ui/editor/objects/facing#facing, ui/editor/objects/filter#filter, ui/editor/objects/types#the-type-list, ui/editor/objects/select#select-move-turn, ui/editor/objects/move#move-g, ui/editor/objects/turn#turning, ui/editor/objects/remove#remove-delete, ui/editor/objects/properties#properties, ui/editor/objects/name#name, ui/editor/objects/owner#owner, ui/editor/objects/items#items]
order: 40
---

# Objects

The **Objects** mode puts things on the map: buildings, units, trees, stone and gold outcrops, props and markers. It has two tools, **Place** and **Select, move, turn**.

![The Objects mode, placing a Manor](shot:editor-objects)

## Place

Choose what to place and who owns it, then click the ground. Each click places one.

### Class

The kind of object:

- **Buildings:** Manors, houses, barracks, walls, towers... They are placed finished, not as building sites.
- **Characters:** peasants, soldiers, ships, animals.
- **Containers (trees, ...):** things that hold goods: trees, stone and gold outcrops.
- **Props:** scenery.
- **Markers:** marks on the map that the game's AI and scripts use.

### Player

The owner of the new object. Every player of the map is listed, the Neutral Player too.

- **Start units and buildings** belong to the player who starts with them.
- **Animals** belong to the Neutral Player on the game's own maps.
- **Trees and outcrops:** to see who owns them on the map you started from, select one with Dev mode on ([below](#select-move-turn)) and look at its **Owner**.

### Facing

The way the new object faces, from -180° to 180°. Use **R** and **Shift+R** after placing to turn it further ([Turning](#turning)).

### Filter

Type part of a name to shorten the list: "manor", "peasant", "tree", "gold", "stone". Upper and lower case don't matter.

### The type list

Every object type of the chosen class in the game set, by its name in the game's data. Click one; it stays chosen until you choose another.

## Select, move, turn

Select objects as in the game: click one, or drag a box. "3 selected" tells you how many. Then use the buttons or keys below.

**What you can select:** as in the game, only your own units and buildings: those of the player you are in the editor. To select anyone's objects, trees and outcrops included, open the debugger (**F2**) and tick **Dev mode** in its *Client Interface Debug* window ([developer tools](../../controls/developer-tools.md#debugger-f2)).

### Move (G)

The selection follows the mouse, each object keeping its place in the group. A left click puts it down there; a right click puts it back where it was.

### Turning

- **Turn +15 (R)** and **Turn -15 (Shift+R)** turn each selected object by 15° about its own centre.
- **Turn +90** and **Turn -90** turn it by a quarter.

A building's footprint turns with it.

### Remove (Delete)

Removes the selected objects from the map. **Delete** does the same in any mode. Undo puts them back with their type, owner, place and facing; their items and orders are not kept.

## Properties

With exactly one object selected, its properties show below the buttons: its number and type, then:

### Name

The object's own name. Most objects have none.

### Owner

The player it belongs to. Choose another to give it away: a building handed to another player is theirs from the start.

### Items

The object's values, such as the wood in a tree or the gold in an outcrop. Each has a box with its value; **x** removes it.

- **Item filter** shortens the **New item** list.
- **New item** and **Add** give the object an item from the game set's list, set to 1. Change the value afterwards.

Every change applies at once. A field's change becomes one undo step when you leave the field.

## Tips

- **A start position** is a player's Manor and a few peasants. See [start positions](make-a-map.md#7-set-up-the-start-positions).
- **Resources:** put trees near every start for wood, and stone and gold outcrops within reach. Note that the lobby's [Randomise Resource](../frontend/skirmish/settings.md#randomise-resource) setting (Medium by default) takes the map's own stone and gold outcrops away and scatters new ones. Only Off keeps yours. Trees are never changed. See [resources](../../mechanics/resources.md).
- **Fair maps:** give every start the same resources at the same distances.
