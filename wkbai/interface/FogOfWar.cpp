// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "FogOfWar.h"
#include "../client.h"
#include "../terrain.h"
#include "../tags.h"
#include "../gameset/GameObjBlueprint.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cmath>

bool FogOfWar::isFriendly(Client* client, CommonGameObject* obj) const
{
	CommonGameObject* me = client->clientPlayer.get();
	CommonGameObject* owner = obj->getPlayer();
	if (!me || !owner)
		return false;
	return owner == me || client->getDiplomaticStatus(owner, me) == 0;
}

void FogOfWar::update(Client* client)
{
	const uint64_t now = SDL_GetTicks();
	if (now - lastUpdate < 250 && !tiles.empty())
		return;
	lastUpdate = now;
	if (!client->terrain || !client->getLevel() || !client->clientPlayer)
		return;
	const auto [w, h] = client->terrain->getNumPlayableTiles();
	if (w <= 0 || h <= 0)
		return;
	width = (uint32_t)w;
	height = (uint32_t)h;
	std::vector<uint8_t> next(width * height, 0);

	auto stamp = [&](const Vector3& pos, float sight) {
		// as WKB.exe: whole tiles of 5 m; the last tile fades out
		const float radius = std::floor(sight + 0.5f) * 5.0f;
		if (radius <= 0.0f)
			return;
		const int x0 = std::max(0, (int)((pos.x - radius) / 5.0f)), x1 = std::min(w - 1, (int)((pos.x + radius) / 5.0f));
		const int z0 = std::max(0, (int)((pos.z - radius) / 5.0f)), z1 = std::min(h - 1, (int)((pos.z + radius) / 5.0f));
		for (int z = z0; z <= z1; z++) {
			const float dz = z * 5.0f + 2.5f - pos.z;
			for (int x = x0; x <= x1; x++) {
				const float dx = x * 5.0f + 2.5f - pos.x;
				const float d = std::sqrt(dx * dx + dz * dz);
				const float seen = std::clamp((radius - d) / 5.0f + 0.5f, 0.0f, 1.0f);
				uint8_t& t = next[z * width + x];
				t = std::max(t, (uint8_t)(seen * 255.0f));
			}
		}
	};
	auto walk = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				const int cls = child->blueprint->bpClass;
				if ((cls == Tags::GAMEOBJCLASS_CHARACTER || cls == Tags::GAMEOBJCLASS_BUILDING) && isFriendly(client, child))
					stamp(child->position, child->getItem(Tags::PDITEM_ACTUAL_SIGHT_RANGE));
				self(child, self);
			}
	};
	walk(client->getLevel(), walk);
	if (next != tiles) {
		tiles = std::move(next);
		version++;
	}
}

bool FogOfWar::isSeen(const Vector3& position) const
{
	if (tiles.empty())
		return true;
	const int x = (int)(position.x / 5.0f), z = (int)(position.z / 5.0f);
	if (x < 0 || z < 0 || x >= (int)width || z >= (int)height)
		return false;
	return tiles[z * width + x] >= 128;
}
