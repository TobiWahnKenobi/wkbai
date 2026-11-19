// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "FogOfWar.h"
#include "../client.h"
#include "../terrain.h"
#include "../tags.h"
#include "../gameset/GameObjBlueprint.h"
#include "../gameset/gameset.h"
#include "../gameset/ScriptContext.h"
#include "../Model.h"
#include "../server.h"
#include "../util/LoadingProgress.h"
#include <cstdio>
#include <cstdlib>
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
	const int P = std::max(drawPad, 0);
	const int DW = w + 2 * P, DH = h + 2 * P;
	std::vector<uint8_t> drawNext((size_t)DW * DH, 0);
	// at night (wkbai's day and night) the sight is shorter, but for the lit ground (once a second)
	NightSight& night = client->nightSight;
	if (night.active() && (night.lit.empty() || ++nightRebuild % 4 == 0))
		night.rebuild(client);

	auto stamp = [&](const Vector3& pos, float sight) {
		// as WKB.exe: whole tiles of 5 m; the last tile fades out
		const float radius = std::floor(sight + 0.5f) * 5.0f;
		if (radius <= 0.0f)
			return;
		// (over the drawn tiles, which reach beyond the map by P; the game's are those on the map)
		const int x0 = std::max(-P, (int)std::floor((pos.x - radius) / 5.0f)), x1 = std::min(w - 1 + P, (int)((pos.x + radius) / 5.0f));
		const int z0 = std::max(-P, (int)std::floor((pos.z - radius) / 5.0f)), z1 = std::min(h - 1 + P, (int)((pos.z + radius) / 5.0f));
		for (int z = z0; z <= z1; z++) {
			const float dz = z * 5.0f + 2.5f - pos.z;
			for (int x = x0; x <= x1; x++) {
				const float dx = x * 5.0f + 2.5f - pos.x;
				const float d = std::sqrt(dx * dx + dz * dz);
				const float r = night.active() ? night.radiusFor(radius, Vector3(x * 5.0f + 2.5f, 0.0f, z * 5.0f + 2.5f)) : radius;
				const float seen = std::clamp((r - d) / 5.0f + 0.5f, 0.0f, 1.0f);
				const uint8_t v = (uint8_t)(seen * 255.0f);
				if (x >= 0 && z >= 0 && x < w && z < h) {
					uint8_t& t = next[z * width + x];
					t = std::max(t, v);
				}
				uint8_t& dt = drawNext[(size_t)(z + P) * DW + x + P];
				dt = std::max(dt, v);
			}
		}
	};
	// what sees: the player's own units, and an ally's when that ally shares its sight (the Shared
	// Vision research done, "Reconnaissance Upgrade Status" 2, which Team Reconnaissance gives
	// every player at the start); allies' units are drawn either way (isFriendly)
	static const GameSet* itemsOf = nullptr;
	static int reconItem = -1;
	if (itemsOf != client->gameSet.get()) {
		itemsOf = client->gameSet.get();
		reconItem = itemsOf ? itemsOf->items.names.getIndex("Reconnaissance Upgrade Status") : -1;
	}
	CommonGameObject* me = client->clientPlayer.get();
	auto sees = [&](CommonGameObject* obj) {
		CommonGameObject* owner = obj->getPlayer();
		if (!owner || !me)
			return false;
		if (owner == me)
			return true;
		return client->getDiplomaticStatus(owner, me) == 0 && reconItem >= 0 && owner->getItem(reconItem) >= 2.0f;
	};
	auto walk = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				const int cls = child->blueprint->bpClass;
				if ((cls == Tags::GAMEOBJCLASS_CHARACTER || cls == Tags::GAMEOBJCLASS_BUILDING) && sees(child))
					stamp(child->position, child->getItem(Tags::PDITEM_ACTUAL_SIGHT_RANGE));
				self(child, self);
			}
	};
	walk(client->getLevel(), walk);
	for (const auto& [pos, radius] : testViewers)
		stamp(pos, radius / 5.0f);
	// REVEAL_FOG_OF_WAR: the revealed objects seen around them, wherever they go, until their time
	// is up (an Act of God's bishop, the artillery firing at the player)
	if (!client->fogReveals.empty()) {
		const float now = client->timeManager.currentTime;
		std::erase_if(client->fogReveals, [&](const Client::FogReveal& r) {
			CommonGameObject* obj = client->findObject(r.object);
			return (r.until >= 0.0f && r.until <= now) || !obj || (obj->flags & CommonGameObject::fTerminated);
		});
		for (const Client::FogReveal& r : client->fogReveals)
			if (CommonGameObject* obj = client->findObject(r.object))
				stamp(obj->position, r.radius);
	}
	if (next != tiles || drawNext != drawTiles) {
		tiles = std::move(next);
		drawTiles = std::move(drawNext);
		drawWidth = (uint32_t)DW;
		drawHeight = (uint32_t)DH;
		version++;
	}
	updateMemory(client);
}

bool FogOfWar::isRemembered(Client* client, CommonGameObject* obj) const
{
	if (obj->blueprint->bpClass != Tags::GAMEOBJCLASS_BUILDING)
		return false;
	// the neutral player's buildings are part of the map, seen from the start
	CommonGameObject* owner = obj->getPlayer();
	if (!owner || owner->blueprint->name == "Neutral Player")
		return false;
	return !isFriendly(client, obj);
}

void FogOfWar::updateMemory(Client* client)
{
	if (memoryLevel != client->getLevel() || memoryPlayer != client->clientPlayer.get()) {
		remembered.clear();
		treeGhosts.clear();
		treeUnknown.clear();
		treesOn = false;
		memoryLevel = client->getLevel();
		memoryPlayer = client->clientPlayer.get();
	}
	// what was remembered where the player sees now: gone if it was destroyed or changed sides
	for (auto it = remembered.begin(); it != remembered.end();) {
		CommonGameObject* live = client->findObject(it->first);
		if (isBuildingSeen(it->second->position) && (!live || (live->flags & CommonGameObject::fTerminated) || !isRemembered(client, live)))
			it = remembered.erase(it);
		else
			++it;
	}
	// the enemy buildings in sight, as they look now
	auto walk = [&](CommonGameObject* obj, auto& self) -> void {
		for (const auto& [type, children] : obj->children)
			for (CommonGameObject* child : children) {
				self(child, self);
				if (!isRemembered(client, child) || (child->flags & CommonGameObject::fTerminated) || !isBuildingSeen(child->position))
					continue;
				copyLook(client, child, remembered[child->id]);
			}
	};
	walk(client->getLevel(), walk);
	updateTrees(client);
}

void FogOfWar::copyLook(Client* client, CommonGameObject* child, std::unique_ptr<ClientGameObject>& copy)
{
	if (!copy || copy->blueprint != child->blueprint)
		copy = std::make_unique<ClientGameObject>(child->id, child->blueprint);
	copy->position = child->position;
	copy->orientation = child->orientation;
	copy->scale = child->scale;
	copy->tilted = child->tilted;
	copy->groundUp = child->groundUp;
	copy->subtype = child->subtype;
	copy->appearance = child->appearance;
	CommonGameObject* owner = child->getPlayer();
	copy->color = owner ? owner->color : 0;
	copy->animationIndex = child->animationIndex;
	copy->animationVariant = child->animationVariant;
	copy->animClamped = child->animClamped;
	// the frame it had then, held (a building going up stays as far as it was)
	uint32_t animTime = (uint32_t)std::max(0.0f, (client->timeManager.currentTime - child->animStartTime) * child->animSpeed * 1000.0f);
	if (child->animSynchronizedTask != -1 && client->gameSet) {
		CliScriptContext ctx{ client, (ClientGameObject*)child };
		const float frac = client->gameSet->tasks[child->animSynchronizedTask].synchAnimationToFraction->eval(&ctx);
		Model* model = child->getModel();
		animTime = model ? (uint32_t)(model->getDuration() * frac * 1000.0f) : 0;
	}
	copy->sceneEntity.animTime = animTime;
	copy->flags = CommonGameObject::fRenderable;
}

void FogOfWar::lookUpTreeItems(Client* client)
{
	if (treeItemsOf == client->gameSet.get())
		return;
	treeItemsOf = client->gameSet.get();
	treeItem = client->gameSet ? client->gameSet->items.names.getIndex("I am a tree") : -1;
	workedItem = client->gameSet ? client->gameSet->items.names.getIndex("I'm Being Worked at") : -1;
}

bool FogOfWar::isTree(Client* client, const CommonGameObject* obj) const
{
	if (!obj || !obj->blueprint)
		return false;
	const int cls = obj->blueprint->bpClass;
	if (cls != Tags::GAMEOBJCLASS_CONTAINER && cls != Tags::GAMEOBJCLASS_PROP)
		return false;
	const std::string& name = obj->blueprint->name;
	if (name.find("Tree") != std::string::npos)
		return name.find("Bird") == std::string::npos;
	return treeItem >= 0 && obj->blueprint->startItemValues.count(treeItem) != 0;
}

bool FogOfWar::treeHiddenLookUp(const ClientGameObject* obj) const
{
	const int cls = obj->blueprint->bpClass;
	if (cls != Tags::GAMEOBJCLASS_CONTAINER && cls != Tags::GAMEOBJCLASS_PROP)
		return false;
	return (treeGhosts.count(obj->id) || treeUnknown.count(obj->id)) && !isSeen(obj->position);
}

void FogOfWar::resetTrees()
{
	treeGhosts.clear();
	treeUnknown.clear();
	treesOn = false;
	treesDirty = false;
	// the next update starts the memory afresh, for the new game's level
	memoryLevel = nullptr;
}

void FogOfWar::beforeTreeChange(Client* client, ClientGameObject* obj)
{
	if (!treesOn || !memoryOn || !obj || tiles.empty())
		return;
	// already remembered as it was, or not known at all
	if (treeGhosts.count(obj->id) || treeUnknown.count(obj->id))
		return;
	lookUpTreeItems(client);
	if (!isTree(client, obj) || isSeen(obj->position))
		return;
	copyLook(client, obj, treeGhosts[obj->id]);
	treesDirty = true;
}

void FogOfWar::treeCreated(Client* client, ClientGameObject* obj)
{
	if (!treesOn || !memoryOn || !obj)
		return;
	lookUpTreeItems(client);
	if (!isTree(client, obj) || treeGhosts.count(obj->id))
		return;
	// (its position comes with the next message: the next update shows it if it is in sight)
	treeUnknown.insert(obj->id);
	treesDirty = true;
}

bool FogOfWar::hidesTreeEffect(Client* client, ClientGameObject* obj) const
{
	if (!memoryOn || !obj || tiles.empty() || isSeen(obj->position))
		return false;
	const_cast<FogOfWar*>(this)->lookUpTreeItems(client);
	return isTree(client, obj);
}

void FogOfWar::updateTrees(Client* client)
{
	if (!memoryOn || tiles.empty())
		return;
	lookUpTreeItems(client);
	// begun at the first update of a game, once it has loaded: every tree known as it stands, or
	// the memory as the save had it
	if (!treesOn) {
		if (LoadingActive())
			return;
		treesOn = true;
		treeGhosts.clear();
		treeUnknown.clear();
		if (Server* server = client->localServer ? Server::instance : nullptr; server && client->clientPlayer)
			loadTreeMemory(client, server->getTreeMemory(client->clientPlayer->id));
		treesDirty = false;
	}
	// seen again: the live tree as it is now (or gone)
	for (auto it = treeGhosts.begin(); it != treeGhosts.end();)
		if (isSeen(it->second->position)) {
			it = treeGhosts.erase(it);
			treesDirty = true;
		}
		else
			++it;
	for (auto it = treeUnknown.begin(); it != treeUnknown.end();) {
		CommonGameObject* live = client->findObject(*it);
		if (!live || isSeen(live->position)) {
			it = treeUnknown.erase(it);
			treesDirty = true;
		}
		else
			++it;
	}
	// for a save game of the server on this machine
	if (treesDirty && client->localServer && Server::instance && client->clientPlayer) {
		Server::instance->setTreeMemory(client->clientPlayer->id, treeMemoryText(client));
		treesDirty = false;
	}
}

std::string FogOfWar::treeMemoryText(Client* client) const
{
	// ids ascending: the same memory gives the same text
	std::vector<uint32_t> ids;
	for (const auto& [id, copy] : treeGhosts)
		ids.push_back(id);
	std::sort(ids.begin(), ids.end());
	std::string out;
	char line[512];
	for (uint32_t id : ids) {
		const ClientGameObject* g = treeGhosts.at(id).get();
		const Vector3 &p = g->position, &o = g->orientation, &s = g->scale, &u = g->groundUp;
		snprintf(line, sizeof(line), "ghost %u %d \"%s\" %d %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %d %.9g %.9g %.9g %d %d %d %u\n",
			id, g->blueprint->bpClass, g->blueprint->name.c_str(), g->subtype, g->appearance, p.x, p.y, p.z, o.x, o.y, o.z, s.x, s.y, s.z,
			(int)g->tilted, u.x, u.y, u.z, g->animationIndex, g->animationVariant, (int)g->animClamped, g->sceneEntity.animTime);
		out += line;
	}
	ids.assign(treeUnknown.begin(), treeUnknown.end());
	std::sort(ids.begin(), ids.end());
	for (uint32_t id : ids)
		out += "unknown " + std::to_string(id) + "\n";
	return out;
}

void FogOfWar::loadTreeMemory(Client* client, const std::string& text)
{
	if (!client->gameSet)
		return;
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(start, end - start);
		start = end + 1;
		uint32_t id = 0;
		if (line.rfind("unknown ", 0) == 0) {
			id = (uint32_t)std::strtoul(line.c_str() + 8, nullptr, 10);
			if (client->findObject(id))
				treeUnknown.insert(id);
			continue;
		}
		if (line.rfind("ghost ", 0) != 0)
			continue;
		int cls = -1;
		int n = 0;
		if (sscanf(line.c_str(), "ghost %u %d \"%n", &id, &cls, &n) < 2 || n <= 0)
			continue;
		const size_t q = line.find('"', (size_t)n);
		if (q == std::string::npos || cls < 0 || cls >= Tags::GAMEOBJCLASS_COUNT)
			continue;
		const GameObjBlueprint* bp = client->gameSet->findBlueprint(cls, line.substr((size_t)n, q - (size_t)n));
		if (!bp)
			continue;
		int subtype = 0, appearance = 0, tilted = 0, anim = 0, variant = 0, clamped = 0;
		unsigned frame = 0;
		Vector3 p, o, s, u;
		if (sscanf(line.c_str() + q + 1, " %d %d %f %f %f %f %f %f %f %f %f %d %f %f %f %d %d %d %u", &subtype, &appearance, &p.x, &p.y, &p.z,
			&o.x, &o.y, &o.z, &s.x, &s.y, &s.z, &tilted, &u.x, &u.y, &u.z, &anim, &variant, &clamped, &frame) != 19)
			continue;
		auto copy = std::make_unique<ClientGameObject>(id, bp);
		copy->subtype = subtype;
		copy->appearance = appearance;
		copy->position = p;
		copy->orientation = o;
		copy->scale = s;
		copy->tilted = tilted != 0;
		copy->groundUp = u;
		copy->animationIndex = anim;
		copy->animationVariant = variant;
		copy->animClamped = clamped != 0;
		copy->sceneEntity.animTime = frame;
		copy->flags = CommonGameObject::fRenderable;
		treeGhosts[id] = std::move(copy);
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

bool FogOfWar::isBuildingSeen(const Vector3& position) const
{
	if (tiles.empty())
		return true;
	const int cx = (int)(position.x / 5.0f), cz = (int)(position.z / 5.0f);
	for (int z = std::max(0, cz - 1); z <= std::min((int)height - 1, cz + 1); z++)
		for (int x = std::max(0, cx - 1); x <= std::min((int)width - 1, cx + 1); x++)
			if (tiles[z * width + x] >= 128)
				return true;
	return false;
}
