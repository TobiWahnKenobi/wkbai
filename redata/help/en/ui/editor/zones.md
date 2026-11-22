---
title: Zones
ids: [ui/editor/zones, ui/editor/zones/type#type-and-new-zone, ui/editor/zones/new#type-and-new-zone, ui/editor/zones/list#the-zone-list, ui/editor/zones/name#name, ui/editor/zones/tiles#add-tiles-and-remove-tiles, ui/editor/zones/radius#radius-m, ui/editor/zones/delete#delete-zone]
order: 60
---

# Zones

A zone is a named area of tiles. A mission's scripts use zones: they notice when a unit enters or leaves one, and find the things inside it. A skirmish map doesn't need any.

## Type and New zone

Choose a zone type from **Type** (the game set's types), then click **New zone**. It is named "Zone 1", "Zone 2"... and selected, ready to paint.

## The zone list

The map's zones, each with its name, type and number of tiles. Click one to select it. Its tiles are drawn over the ground in light blue.

## Name

The selected zone's name. Scripts find a zone by its name, so it must be the name the script uses.

## Add tiles and Remove tiles

Choose one, then hold the left mouse button on the ground: the tiles under the brush join the zone, or leave it. A tile belongs to one zone at most, so adding it to this zone takes it out of any other.

## Radius (m)

The brush's size: 2.5 to 100 metres.

## Delete zone

Removes the selected zone and frees its tiles.

Zones are saved with the map. Zone changes are not part of undo.
