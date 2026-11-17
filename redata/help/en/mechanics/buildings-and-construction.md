---
title: Buildings and construction
ids: [mechanics/buildings-and-construction, mechanic/construction, mechanic/placement, mechanic/city, mechanic/repair, mechanic/building-damage, mechanic/fire, mechanic/garrison, mechanic/capture, mechanic/destruction]
order: 50
---

# Buildings and construction

## Placing a building

Pick a building from the top bar's Military, Alignment or Economic menu (or a peasant's build buttons) and move it over the ground. Its tiles show green where it may stand and red where it may not; a red box under the cursor says why. `[` and `]` (the two keys right of P) turn it by a quarter before you place it.

- **The cost is paid when you place the foundation.** A building whose button is grey is waiting for resources; a ghost button names the building or research it still needs ([alignment](alignment.md), [training and research](upgrades-and-research.md)).
- **City buildings** (barracks, church, shops and the like) must join your **city**: their outline has to reach into the city's area or onto its walls. The city starts as your Manor's ground and grows with every city building, so a long street of buildings can lead far from the Manor.
- **Walls grow by themselves.** Each new city building pushes the city's palisade out around it; the old wall pieces left inside come down. Building over a gatehouse is refused.
- **A [Village](../buildings/village-wood.md)** starts a settlement of its own: it must stand clear of every city.
- **Farms and other country buildings** stand outside the walls; a city may not grow around one.
- The ground under a new building is levelled.

## Building it

- Peasants on [Build](../commands/build.md) raise a foundation step by step. Each swing adds a fixed share of its strength, so two builders finish twice as fast.
- The foundation's frame rises with the work; when it reaches full strength it turns into the finished building and can do its job.
- Some buildings unlock others only when finished; some already when their foundation is placed ([alignment](alignment.md)).

## Strength, damage and repair

A building's health is its **structural integrity** (the bar over it, and its panel).

- **How it looks:** above 75% whole, above 33% damaged, below that derelict.
- **Who can hurt it:** ordinary soldiers barely scratch a building (0.1 a blow). Siege engines, sappers and fire do the work ([siege](siege.md)).
- **[Repair](../commands/repair.md):** peasants repair one step a second. Repairing a building from nothing to full costs **half its price**; each step costs its share. With the resources lacking, they wait and try again every 3 seconds.
- **Destroyed:** a building at 0 collapses into a ruin that belongs to no one.
- **Taking down your own:** a building you delete gives back half of its repair price, by how much strength it had left; a foundation gives back what was paid for it.

## Fire

- Flaming arrows, Greek fire, rockets, arson by spies and lightning set buildings alight.
- A burning building loses strength steadily, and the fire grows. It can leap to other buildings within 25 m. Walls don't burn.
- A fire may die out by itself; the better the building's fire proofing, the likelier.
- Peasants on [Extinguish](../commands/extinguish.md) put it out. Your minimap marks burning buildings with an orange circle.

## Garrisons

Units can [garrison](../commands/garrison.md) inside some buildings. Missile units shoot out of them.

| Building | Places |
|---|---|
| Manor, Keep, Castle, Palace | 50 |
| Church | 20 |
| Stone fort | 20 |
| Wooden fort | 10 |
| Watch tower, towers | 5 |

- A fort shoots only while someone is inside.
- When a building drops below **10%** of its strength, or is destroyed, everyone inside runs out 10 to 20 m. Nobody inside dies with it.

## Capture

Only the flags of Valhalla mode can be captured in the shipped game: walk a unit next to one (within 5 m) with [Capture](../commands/capture.md) ([victory and defeat](victory-conditions.md#valhalla-mode)). Buildings change hands otherwise only by scripts in a campaign level.

![A city growing: a new building placed at the end of a street, the palisade moving out](shot:mech-city-placement)

> **wkbai.** The original's city rule is followed exactly, a corner inside the city counting twice. wkbai shows the refusal reason in the original's red box. Lightning in wkbai's weather only scratches a building (6% of its strength) but sets it alight.
