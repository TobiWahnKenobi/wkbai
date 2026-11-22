---
title: Farm
ids: [command/Farm]
order: 10
---

# Farm

<!-- gen:icon -->
![Farm](game:interface/icons/sic_farm.tga)
<!-- /gen:icon -->

## Role

Sends a [peasant](../units/peasant.md) to work a field for food. Right-click a farm; the cursor shows the food cursor. A farmer also raises your food support, which decides how big an army you can feed.

## How it works

- **Target:** a farm of yours or an ally's that nobody is working. One farmer per farm: if it is taken, he looks for the nearest free farm of yours he can reach, and gives up if there is none.
- **Yield:** each turn of the hoe adds food by the farm's ground: 1.5 on a Fertile Farm, 1 on a [Standard Farm](../buildings/standard-farm.md), 0.5 on a Barren Farm. His load is full at 10.
- **Windmill:** if a [Windmill](../buildings/windmill.md) of yours or an ally's stands within 40 m on the way home, he stops there for 6 seconds and his load counts double (×2.5 after [Advanced Windmill](../upgrades/advanced-windmill.md)).
- **Maypole:** on his day of rest (every seventh trip) a farmer who dances at a [Maypole](../buildings/maypole.md) within 50 m farms 1.5 times as fast from then on (1.8 after [Festival of Beltane](../upgrades/festival-of-beltane.md), 2.2 after [Festival of Samhain](../upgrades/festival-of-samhain.md), 2.5 after [Sacrifice for Fertility](../upgrades/sacrifice-for-fertility.md)). Praying at a church instead resets this ([church and maypole](../mechanics/peasants-and-work.md#church-and-maypole)).
- **Food support:** while he farms, each farmer supports 4 food of army upkeep (6 after [Heavy Plough](../upgrades/heavy-plough.md), 10 after [Horse Harness](../upgrades/horse-harness.md), 15 after [Crop Rotation](../upgrades/crop-rotation.md)). See [food, population and upkeep](../mechanics/population-and-food.md).
- **Upkeep on delivery:** part of each load goes to feed your army, the share your army's upkeep takes of your food support ([the upkeep penalty](../mechanics/population-and-food.md#the-upkeep-penalty)).
- **Limits:** a unit under [Possess](possess.md) can't take the order.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Peasant](../units/peasant.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- Nothing: it is there whenever the unit or building is.
<!-- /gen:needs -->

## Tips

- When a farm is finished, one of its builders starts farming it by himself.
- Farmers matter more for the support they give than for the food itself: keep enough of them working before you build up an army.
- Fertile ground gives three times the food of barren ground; place farms where the ground is good ([farms and the ground](../mechanics/resources.md#farms-and-the-ground)).
