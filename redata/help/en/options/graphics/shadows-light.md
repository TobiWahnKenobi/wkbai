---
title: Shadows and light
ids: [ui/frontend/options/lighting, option/buildingShadows#building-shadows, option/staticShadows#building-shadows, option/staticShadowLight#light-in-building-shadows, option/wallShadows#wall-shadows, option/shadowResolution#building-shadow-detail, option/characterShadows#character-shadows, option/realCharacterShadows#real-character-shadows, option/rtShadows#ray-traced-shadows, option/contactShadows#contact-shadows, option/fireLights#light-from-fires, option/skyGI#light-from-the-sky, option/dynamicGI#bounced-light-dynamic-gi, option/dynamicGIStrength#bounced-light-strength, option/emissiveLightStrength#window-light-strength-ultra, option/ssao#ambient-occlusion, option/fireQuality#fire]
order: 30
---

# Shadows and light

The sun's shadows, and the other light that falls on the land: from fires, from the sky, and bounced off the ground and the walls.

![Graphics > Shadows and light](shot:options-lighting)

## Building shadows

On (the default): buildings and trees cast shadows from the sun. Off: none, and the shadow settings that depend on them are greyed out.

## Light in building shadows

With Building shadows: how much light is left in the shadows, 0 (black) to 1 (no shadow at all). Default 0.71, the original's.

## Wall shadows

With Building shadows; on by default. Walls and palisades cast shadows too. The original gives them none.

## Building shadow detail

With Building shadows: how sharp the shadows are.

- **Original** (the default): as the original's.
- **High:** twice as fine.
- **Very high:** four times as fine.

The finer shadow map takes effect from the next map. **Cost:** more graphics memory and a little more time to draw the shadows.

## Character shadows

On (the default): units cast shadows.

## Real character shadows

With Character shadows and Building shadows; on by default. Units cast shadows of their real shape, moving with them, instead of the original's round dark spot under each one.

## Ray-traced shadows

With Building shadows; off by default. The sun's shadows of buildings, trees and units are traced ray by ray instead of drawn from a shadow map, so they are exact at any distance and never blocky. Needs a graphics card with ray tracing; see [Graphics](index.md#ray-tracing). **Cost:** less work for the processor than before, but more for the graphics card: in wkbai's performance test, about 5 to 6 ms more graphics-card time a frame than the shadow map they replace (10 ms in all instead of 4.4, Radeon RX 6900 XT). Where the processor holds the game back, as on that test machine, the game still runs faster; on a weaker graphics card it may run slower.

## Contact shadows

On (the default): the small shadows where units' feet, fences and walls meet the ground, which the ordinary shadows are too coarse to show. **Cost:** small.

## Light from fires

On (the default): fires light the ground and the models around them: burning buildings, flaming arrows, fire spells, up to 16 at once. With [Fire](#fire) on Low or High the light is more orange and flickers and sways with the flames; on High it is also stronger and reaches further the more a building is damaged.

## Fire

How fires look.

- **Original:** the original game's flame pictures.
- **Low:** drawn flames that rise and flicker, sunk into a burning building rather than standing in front of it, with soot that thins out and fades as it rises, and a column of smoke above a burning building.
- **High** (the default): Low's flames, finer, with a glow around each fire, a thicker column of smoke and the hot air shimmering above it.

With Low and High, the [light from fires](#light-from-fires) is more orange and flickers and sways with the flames. Low and High need the enhanced renderers; without them fires look as with Original. Fire is only looks: it changes nothing in the game, and replays stay the same.

## Light from the sky

On (the default): the land and the models are lit by the sky's colours as much as they can see of the sky, so that valleys, the spaces between houses and the shaded sides are darker and coloured by the sky; where the sky is hidden, the sun's light bounced off the ground fills in. Off: the original's simple rule, which keeps every slope at least a little lit. **Cost:** about 0.3 ms.

## Bounced light (dynamic GI)

With Light from the sky. The light of the sun, the moon, the sky and fires bounces off the ground and the buildings onto what is around them, in the colour of what it bounced off.

- **Off** (the default): only the plain bounce of Light from the sky.
- **Low:** off the ground and the buildings, once.
- **High:** off the units too, twice, gathered from further away.
- **Ultra:** High, and the lit windows, fires and lamps of buildings at night give real light from where they are, in their colour.

**Cost:** Low about 0.1 ms, High about 0.2 to 0.3 ms; Ultra's window lights add a few hundredths more.

## Bounced light strength

With Bounced light on: how strong the bounce is, 0 to 3, default 1.

## Window light strength (Ultra)

With Bounced light at Ultra: how strong the light from windows, fires and lamps is, 0 to 3, default 1, on top of the bounce.

## Ambient occlusion

On (the default): corners, the ground under buildings and the spaces between things are a little darker, as they are in real light. **Cost:** small.

> wkbai: the original has the building shadows (at Original detail, without walls), the round unit shadows and its own simple lighting. The rest of this page is wkbai's own.
