// wkbai - leafless trees made in code (wkbai's seasons: winter, wildfires)
// Licensed under the GNU General Public License 3

#pragma once

struct Model;
struct ModelCache;

// A broadleaf tree without leaves, of the game set's bark texture ("Tree Bark"): trunk, limbs,
// branches and twigs (winter), or burnt: the trunk and a few main limbs broken off (drawn
// charred by SEFLAG_CHARRED). 1 high (burnt 0.7), standing on its origin; four variants each (variant & 3).
Model* LeaflessTreeModel(ModelCache& cache, int variant, bool burnt);
