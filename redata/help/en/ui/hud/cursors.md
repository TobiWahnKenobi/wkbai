---
title: Cursors
ids: [ui/hud/cursors, ui/hud/cursors/attack#attack-cursors, ui/hud/cursors/missile#missiles-and-siege-engines, ui/hud/cursors/patrol#patrol-cursors, ui/hud/cursors/next#more-than-one-order]
order: 110
---

# Cursors

With something selected, the mouse pointer shows **what a right-click would do** at that spot. Over a tree a peasant's pointer becomes an axe, over an enemy a soldier's becomes a sword. With nothing selected, or nothing to do there, it is the plain arrow.

The cursor and the right-click always agree: what the cursor shows is the order the right-click gives.

## The common cursors

The pictures are the game's own; the files are in `Interface\` of your install.

| Cursor | File | Order | Shows when |
|---|---|---|---|
| ![](game:Interface/C_DEFAULT.TGA) | C_Default | – | Nothing to do here |
| ![](game:Interface/C_Move.tga) | C_Move | Move | Over open ground |
| ![](game:Interface/c_attack_normal.tga) | c_attack_… | Attack | Over an enemy: see [below](#attack-cursors) |
| ![](game:Interface/c_chop_wood.tga) | c_chop_wood | Chop tree | A peasant over a tree with wood, fewer than 5 choppers there |
| ![](game:Interface/c_minerock.tga) | c_minerock | Mine (stone or gold) | A peasant over a rock or gold deposit |
| ![](game:Interface/C_Food.tga) | C_Food | Farm | A peasant over your farm that nobody works |
| ![](game:Interface/C_Deliver.tga) | C_Deliver | Deliver | Someone carrying a load, over a building that takes it |
| ![](game:Interface/c_repair.tga) | c_repair | Finish building, Repair | A builder over an unfinished or damaged building that isn't burning |
| ![](game:Interface/c_entre.tga) | c_entre | Garrison, Enter | Over a building or cart with room |
| ![](game:Interface/C_Defend.tga) | C_Defend | Guard | Over any of your own or allied units or buildings |
| ![](game:Interface/C_Train.tga) | C_Train | Train | A soldier still learning, over a training dummy with room |
| ![](game:Interface/c_heal.tga) | c_heal | Heal, Resupply | A healer over a wounded unit or formation |
| ![](game:Interface/c_shop.tga) | c_shop | Shop, Convert goods to gold | A shopper or merchant over a shop or market |
| ![](game:Interface/c_board.tga) | c_board | Board | Over a boat with room |
| ![](game:Interface/c_follow.tga) | c_follow | Follow | Over a unit (hold **F**) |
| ![](game:Interface/c_join_formation.tga) | c_join_formation | Join formation | A soldier over a formation |
| ![](game:Interface/c_arson.tga) | c_arson | Arson | Over an enemy building |

**Guard is the fallback over your own things.** Over a farm already worked, a training dummy with five soldiers on it, or with a soldier already half trained, the cursor is Guard: that is right, the other orders don't fit there.

Every command in the game set can bring its own cursor: spells, acts of God, capturing buildings, blowing gates open, priests' and druids' work. A mod's commands bring theirs.

## Attack cursors

Over an enemy unit, a fighter shows one of three swords:

- **green:** this match-up favours your unit (by the game set's values for which unit types do well against which);
- **red:** it favours the enemy;
- **plain:** neither by much, or the target is a building.

## Missiles and siege engines

Archers, gunners and siege engines show:

- **an arrow** where they can shoot;
- **a flaming arrow** against a building when they shoot fire;
- **"too close"** inside a siege engine's least range;
- **"out of range"** beyond its reach.

## More than one order

Often several orders fit the same spot: a soldier over an allied building could guard it or garrison it. The cursor shows the first that fits, in the order the game set lists them, with Move always last.

- **E** steps to the next order that fits there ([Next command at the cursor](../../controls/keys.md#selection-and-orders)); after the last it goes back to the first. The choice holds until your next left-click.
- **F** held: the Follow order where it fits; **G** held: Guard.

## Patrol cursors

With **R** held (and something selected), the cursor turns into the patrol cursor; with **Shift+R**, the waypoint cursor. A right-click then patrols. See [Orders](../../controls/orders.md#patrol).

> wkbai: the cursor is chosen as in the original: the commands with a cursor, spells and acts of God left out, Move last. A formation offers only what its members offer, plus its own formation attacks.
