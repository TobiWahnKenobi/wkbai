// wkbre2 - WK Engine Reimplementation
// (C) 2021-2022 AdrienTD
// Licensed under the GNU General Public License 3

#include "common.h"
#include "gameset/GameObjBlueprint.h"
#include "gameset/gameset.h"
#include "terrain.h"

float CommonGameObject::getItem(int item) const
{
	float value = 0.0f;
	if (auto it = items.find(item); it != items.end())
		value = it->second;
	else if (auto bt = blueprint->startItemValues.find(item); bt != blueprint->startItemValues.end())
		value = bt->second;
	// Height: WKB.exe's item getters for characters and buildings (0x51E580, 0x554B40) add 0.12
	// tiles to the Actual Sight Range per metre above the map's lowest point, unless it is 1 or
	// the blueprint says HEIGHT_SHOULD_NOT_AFFECT_SIGHT_RANGE. The scripts' missile range is that
	// sight in metres, so high ground also shoots further.
	if (item == Tags::PDITEM_ACTUAL_SIGHT_RANGE && value != 1.0f && blueprint->heightAffectsSight
		&& (blueprint->bpClass == Tags::GAMEOBJCLASS_CHARACTER || blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING))
		value += (position.y - g_lowestTerrainHeight) * 0.12f;
	return value;
}

float CommonGameObject::getIndexedItem(int item, int index) const {
	auto it = indexedItems.find(std::make_pair(item, index));
	if (it != indexedItems.end())
		return it->second;
	return 0.0f;
}

CommonGameObject* CommonGameObject::getPlayer() const {
	for (const CommonGameObject* obj = this; obj; obj = obj->parent) {
		if (obj->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER)
			return (CommonGameObject*)obj;
	}
	return nullptr;
}

Model* CommonGameObject::getModel() const {
	return blueprint->getModel(subtype, appearance, animationIndex, animationVariant);
}

int CommonGameState::getDiplomaticStatus(CommonGameObject* a, CommonGameObject* b) const {
	if (a == b) return 0; // player is always friendly with itself :)
	if (!a || !b) return gameSet->defaultDiplomaticStatus; // an object without a player
	auto it = (a->id <= b->id) ? diplomaticStatuses.find({ a->id, b->id }) : diplomaticStatuses.find({ b->id, a->id });
	if (it != diplomaticStatuses.end())
		return it->second;
	else
		return gameSet->defaultDiplomaticStatus;
}

void CommonGameState::updateOccupiedTiles(CommonGameObject* object, const Vector3& oldposition, const Vector3& oldorientation, const Vector3& newposition, const Vector3& neworientation)
{
	if (!this->tiles) return;
	int trnNumX, trnNumZ;
	std::tie(trnNumX, trnNumZ) = this->terrain->getNumPlayableTiles();
	if (object->blueprint->bpClass == Tags::GAMEOBJCLASS_BUILDING && object->blueprint->footprint) {
		// free tiles from old position
		auto rotOrigin = object->blueprint->footprint->rotateOrigin(oldorientation.y);
		int ox = (int)((oldposition.x - rotOrigin.first) / 5.0f);
		int oz = (int)((oldposition.z - rotOrigin.second) / 5.0f);
		for (auto& to : object->blueprint->footprint->tiles) {
			if (true /*!to.mode*/) {
				auto ro = to.rotate(oldorientation.y);
				int px = ox + ro.first, pz = oz + ro.second;
				if (px >= 0 && px < trnNumX && pz >= 0 && pz < trnNumZ) {
					auto& tile = this->tiles[pz * trnNumX + px];
					if (tile.building == object->id)
						tile.building = nullptr;
				}
			}
		}
		// take tiles from new position
		rotOrigin = object->blueprint->footprint->rotateOrigin(neworientation.y);
		ox = (int)((newposition.x - rotOrigin.first) / 5.0f);
		oz = (int)((newposition.z - rotOrigin.second) / 5.0f);
		for (auto& to : object->blueprint->footprint->tiles) {
			if (true /*!to.mode*/) {
				auto ro = to.rotate(neworientation.y);
				int px = ox + ro.first, pz = oz + ro.second;
				if (px >= 0 && px < trnNumX && pz >= 0 && pz < trnNumZ) {
					auto& tile = this->tiles[pz * trnNumX + px];
					if (!tile.building.getFrom(this) || tile.buildingPassable) {
						tile.building = object->id;
						tile.buildingPassable = to.mode;
					}
				}
			}
		}
	}
}
