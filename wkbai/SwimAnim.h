// wkbai - swim animations made from a unit's walk
// Licensed under the GNU General Public License 3

#pragma once

#include <string>

struct Anim;

// The game set has a "Swim" animation tag, but only the Scout has a SWIM animation. A unit with
// none gets one made from its walk ("Move"), in memory, when it is first needed (no file written,
// none shipped). The original's are vertex animations of one mesh, with no skeleton, so the swim
// is the walk's own vertices moved:
//  - upright (soldiers, peasants): the body tipped forward to lie in the water from the hips, the
//    head held up, the walk's leg swing (fore and aft) a kick (up and down), its arm swing a
//    stroke; the hips a little under the water line, a slow bob;
//  - robed (priests, monks, women in long skirts): less tipped, so the robe doesn't fold;
//  - mounted (a horse and its rider are one mesh): level, the horse's legs paddling, down to its
//    back in the water, the rider over it;
//  - swarm (the Gibbering Horde, many small ones in a mesh): level, their heads at the water line.
// The model's origin is at the water line (as the Scout's own SWIM1): the client draws a swimmer
// with its origin at the water's surface (ClientInterface). A real SWIM*.ANIM3 (the Scout's, a
// mod's, or one an HD model brings later) always takes the place of the made one
// (GameObjBlueprint::loadAnimations).
enum class SwimStyle { Upright, Robed, Mounted, Swarm };

// whether a character (by its blueprint's name) swims in the river currents, as
// ServerEnvironment::kindOf and waterHardiness have it: not the war machines, carts, wagons and
// boats, the huge creatures (they drift) or the otherworldly (they walk the bed)
bool SwimsByName(const std::string& blueprintName);

// the style for a character's folder (its model path in the game set)
SwimStyle SwimStyleForFolder(const std::string& folder);

// the swim made from a walk: the same mesh and vertices, its own key frames (the walk's, 1.5
// times as long), attachment points and normals; false (out untouched) if the walk has none
bool MakeSwimAnim(const Anim& walk, SwimStyle style, Anim& out);
