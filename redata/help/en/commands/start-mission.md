---
title: Start Mission
ids: [command/Start Mission]
order: 10
---

# Start Mission

<!-- gen:icon -->
![Start Mission](game:interface/icons/sic_Mission.tga)
<!-- /gen:icon -->

## Role

Sends a [Spy](../units/spy.md) to win enemy [peasants](../units/peasant.md) over to your side. They walk to your Monastery and take up arms for you.

## How it works

- **Button:** it appears once you have built a [Monastery](../buildings/monastery.md). While the mission runs, the button is [Stop Mission](stop-mission.md).
- **Who:** at once, and then every 20 seconds, the nearest enemy peasant within 25 m of the spy. Move the spy close to enemy workers first: he stays where he is.
- **What happens to the peasant:** he goes to the spy and prays with him, and his owner can no longer select him. Then he walks to the nearest of your Monasteries or [Holy Chapters](../buildings/holy-chapter.md) and prays again within 10 m of it. If you are below your population limit, he joins you as an [Archer](../units/archer.md), a [Squire](../units/mounted-spearman.md) or a [Spearman](../units/spearman.md), at random. If you are at the limit, he stays his owner's and goes free.
- **Cost:** nothing.
- **Exposure:** after each try, computer players' defences can notice the spy for 8 seconds, disguise or not.
- **Limits:** any other order you give the spy ends the mission.

## Offered by

<!-- gen:trained -->
- **Offered by:** [Spy](../units/spy.md)
<!-- /gen:trained -->

## Needs

<!-- gen:needs -->
- building a [Monastery](../buildings/monastery.md)
<!-- /gen:needs -->

