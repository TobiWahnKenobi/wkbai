// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include "../util/GSFileParser.h"
//#include "gameset.h"
#include <map>
#include <cstdint>
#include <string>
#include <vector>
#include "../util/IndexedStringList.h"
#include "../util/vecmat.h"

struct GameSet;
struct Command;
struct Model;
struct Reaction;
struct ValueDeterminer;
struct Footprint;
struct ObjectFinder;

struct GameObjBlueprint {
	struct BPAppearance {
		std::string dir;
		std::map<int, std::vector<Model*>> animations;
	};

	struct SoundRef {
		std::string filePath;
		int soundBlueprint = -1;
		SoundRef(std::string&& name) : filePath(name) {}
		SoundRef(int sndBp) : soundBlueprint(sndBp) {}
	};

	struct PhysicalSubtype {
		std::string dir;
		std::map<int, BPAppearance> appearances;
		std::map<int, std::vector<SoundRef>> soundMap;
	};

	struct MovementBand {
		float naturalMovementSpeed = 1.0f;
		int defaultAnim = -1;
		std::vector<std::pair<int, int>> onEquAnims;
	};

	int bpClass, bpId;
	std::string name;
	GameSet *gameSet;

	std::map<int, float> startItemValues;

	IndexedStringList subtypeNames;
	std::map<int, PhysicalSubtype> subtypes;
	std::string modelPath;

	std::vector<Command*> offeredCommands;

	std::vector<Reaction*> intrinsicReactions;

	std::map<int, const GameObjBlueprint*> mappedTypeTags;
	std::map<int, ValueDeterminer*> mappedValueTags;

	Model* representAs = nullptr;

	ValueDeterminer* missileSpeed = nullptr;
	ValueDeterminer* doNotCollideWith = nullptr;
	int strikeFloorSound = -1;
	int strikeWaterSound = -1;

	std::map<int, std::vector<SoundRef>> soundMap;
	std::map<int, std::vector<std::string>> musicMap;
	std::map<int, std::vector<Model*>> specialEffectMap;

	bool generateSightRangeEvents = false;
	bool receiveSightRangeEvents = false;
	ValueDeterminer* shouldProcessSightRange = nullptr;
	int sightRangeEquation = -1;

	Footprint* footprint = nullptr;

	Vector3 scaleAppearance = Vector3(1, 1, 1);
	Vector3 minScaleVary = Vector3(1, 1, 1);
	Vector3 maxScaleVary = Vector3(1, 1, 1);

	bool floatsOnWater = false;

	bool removeWhenNotReferenced = false;

	int movementSpeedEquation = -1;
	std::vector<MovementBand> movementBands;

	ObjectFinder* aiSpawnLocationSelector = nullptr;

	int buildingType = -1;

	bool objectIsRenderable = true;
	bool objectIsSelectable = true;
	bool objectIsTargetable = true;

	bool hasDynamicShadow = false;
	bool hasStaticShadow = false;
	// SHEAR_MODEL (walls, palings, the battering ram): the model is sheared vertically to
	// follow the slope of the ground under it.
	bool shearModel = false;

	// DISPLAY_ON_MINIMAP. Default inferred from the shipped game set: it only ever sets 0 on
	// characters and buildings and 1 on containers and props (not verified in the exe yet).
	bool displayOnMinimap = false;
	// MINIMAP_DISPLAY_CATEGORY MILITARY / ECONOMIC / RESOURCE (several allowed): which of the
	// minimap's display toggles show the object. 0 = always shown.
	enum { MINIMAP_MILITARY = 1, MINIMAP_ECONOMIC = 2, MINIMAP_RESOURCE = 4 };
	uint8_t minimapCategories = 0;
	// DISPLAY_CATEGORY (ECONOMIC, MILITARY, ALIGNMENT): the build menu tab of a building (the
	// original offers no build command for a building without one)
	std::string displayCategory;

	// The original's range circles: a band texture (Textures\Border Bands) drawn on the ground
	// around the object, its radius a value in metres. DISPLAY_SPRITE_WHEN_SELECTED: while the
	// object is selected (value of the object); DISPLAY_SPRITE_WHEN_STAMPING_DOWN: while it is
	// being placed (value of the player); DISPLAY_SPRITE_WHEN_SELECTED_FOR: selecting the object
	// also shows the found objects' bands (a farm shows its windmills').
	struct RangeSprite {
		std::string texture;
		ValueDeterminer* radius = nullptr;
	};
	std::vector<RangeSprite> spritesWhenSelected, spritesWhenStampingDown;
	// FORMATION_SEPERATION_X/Y: a character's distance to its neighbours in a formation, across
	// and between rows (the original's default 2.8 m; cavalry 4 x 6)
	float formationSeparationX = 2.8f, formationSeparationY = 2.8f;
	std::vector<ObjectFinder*> spritesWhenSelectedFor;

	void parse(GSFileParser &gsf, const std::string &directory, bool isExtension = false);
	void init(int i_bpClass, int i_bpId, const std::string &i_name, GameSet *i_gameSet);

	uint32_t getFullId() const;
	std::string getFullName() const;

	std::tuple<std::string, float, float> getSound(int sndTag, int subtype) const;
	const BPAppearance* getAppearance(int subtype, int appear) const;
	Model* getModel(int subtype, int appear, int anim, int variant) const;
	Model* getSpecialEffect(int sfxTag) const;

	bool canWalkOnWater() const;

	float getStartingItemValue(int itemIndex) const;
	int getStartingFlags() const;

	//GameObjBlueprint() {}
	//GameObjBlueprint(GameSet *gameSet) : gameSet(gameSet) {}
	//GameObjBlueprint(int cls, const std::string &name, GameSet &gameSet) : bpClass(cls), name(name), gameSet(gameSet) {}

private:
	void loadAnimations(GameObjBlueprint::BPAppearance &ap, const std::string &dir, bool overrideAnims = false);
};

class GameObjBlueprintIndex
{
public:
	GameObjBlueprintIndex(int classTag, int id) : m_data(id | (classTag << 24)) {}
	GameObjBlueprintIndex(const GameObjBlueprint* blueprint) : GameObjBlueprintIndex(blueprint->bpClass, blueprint->bpId) {}
	int bpId() const { return m_data & 0xFFFFFF; }
	int bpClass() const { return (m_data >> 24) & 0xFF; }

	uint32_t fullId() const { return m_data; }
	static GameObjBlueprintIndex fromFullId(uint32_t data) { return GameObjBlueprintIndex(data); }

	bool operator<(const GameObjBlueprintIndex& other) const { return m_data < other.m_data; }
	bool operator==(const GameObjBlueprintIndex& other) const { return m_data == other.m_data; }
private:
	GameObjBlueprintIndex(uint32_t data) : m_data(data) {}
	uint32_t m_data;
};

inline uint32_t GameObjBlueprint::getFullId() const { return GameObjBlueprintIndex(this).fullId(); }
