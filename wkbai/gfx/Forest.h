// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "../util/vecmat.h"
#include <cstdint>
#include <vector>

struct Model;

// The forest: the still trees and bushes of the level, kept between frames. The client interface
// (ClientInterface::drawObject) works a tree's look out once and then only checks each frame that
// it hasn't changed; the enhanced scene renderer keeps the trees in GPU buffers, culls them per
// pass on the GPU and draws them with indirect draws, and the ray-traced shadows keep them in a
// structure of their own that is only updated when one changes. With 30,000 trees all of this
// was done per tree and per pass every frame on the CPU (~150 ms a frame).
struct ForestTree {
	Model* model = nullptr;    // a static model (an animated tree, being worked at, isn't in the forest)
	Matrix transform;          // world, row vectors (SceneEntity::transform)
	Vector3 centre;            // the bounding sphere in world space
	float radius = 0.0f;
	uint32_t flags = 0;        // SceneEntity flags
	int color = 0;             // the player's colour (the UV set)
	bool hidden = true;        // not drawn: an empty slot, gone, or lifted out this frame (hovered)
	bool lifted = false;       // hidden for this frame only: drawn in the scene (its shadow stays)
	uint16_t minimap = 0;      // the minimap's: 0x8000 shown on it (DISPLAY_ON_MINIMAP), its categories
};

struct Forest {
	// the slots; a slot keeps its index while the forest lasts (free ones hidden, used again)
	std::vector<ForestTree> trees;
	// slots changed since the renderer last took them (its GPU copy and the ray-traced
	// structure are updated for those alone); all of them after a rebuild
	std::vector<uint32_t> changed;
	bool rebuilt = true;
	// counts every change: the renderers' copies compare it
	uint64_t version = 1;

	void markChanged(uint32_t slot) {
		// (many at once: the renderer takes all of them)
		if (!rebuilt) {
			changed.push_back(slot);
			if (changed.size() > trees.size() / 4 + 64) {
				changed.clear();
				rebuilt = true;
			}
		}
		version++;
	}
	void clear() {
		trees.clear();
		changed.clear();
		rebuilt = true;
		version++;
	}
};
