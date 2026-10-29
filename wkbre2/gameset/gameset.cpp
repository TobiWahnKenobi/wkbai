// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "gameset.h"
#include "../file.h"
#include "../util/GSFileParser.h"
#include "../tags.h"
#include "../util/util.h"
#include <vector>
#include <string>
#include <set>
#include "finder.h"

void ignoreBlueprintEx(GSFileParser &gsf, const std::string &endtag)
{
	gsf.advanceLine();
	while (!gsf.eof)
	{
		if (gsf.nextTag() == endtag)
			break;
		gsf.advanceLine();
	}
}
void ignoreBlueprint(GSFileParser& gsf, const std::string& tag) { ignoreBlueprintEx(gsf, "END_" + tag); }

namespace {
	// The items the original engine declares itself (WKB.exe, its item names from 0x8adda0): the
	// game set uses them without DECLARE_ITEM (the AI stats, "Diplomacy Enabled", ...)
	// and the events it sends itself (from 0x8ad820), some of which wkbre2 doesn't send yet
	const char* const ENGINE_EVENTS[] = {
		"On Terrain Changed",
		"On Diplomatic Status Changed",
		"On Tribute Received",
		"On Commissioned",
		"On Form Up Failed",
		"On Formation Disband",
		"On Formation Form Up",
		"On Subordinate Created",
		"On Subordinate Received",
		"On Control Transferred",
		"On Last Subordinate Released",
		"On Spawn",
		"On Stampdown",
		"On Session Start",
		"On Level Start",
		"On All Players Terminated",
		"On Last Player Standing",
		"On Busy",
		"On Idle",
		"On Object Exits",
		"On Object Enters",
		"On Exit Zone",
		"On Enter Zone",
		"On Not Seen Anymore",
		"On First Seen",
		"On Stop Seeing Object",
		"On Seeing Object",
		"On Share Tile",
		"On Collision",
		"On Conversion End",
		"On Conversion Begin",
		"On Termination",
		"On Destruction"
	};
	const char* const ENGINE_ITEMS[] = {
	"Structural Points Capacity",
	"Structural Points",
	"Amount Of Spare Heavy Arms",
	"Amount Of Spare Horses",
	"Habitation Limit",
	"Teams Selected",
	"Tutorial Level",
	"Settings - Fog Of War",
	"Settings - Team Recon",
	"Settings - Barbarian Tribes",
	"Settings - Team Placement",
	"Settings - Resource Level",
	"Settings - Capturable Flags",
	"Settings - Unit Lives",
	"Settings - Resource Points",
	"Settings - Valhalla Mode Food",
	"Settings - Victory Time",
	"Settings - Population Limit",
	"Settings - Start Gold",
	"Settings - Start Wood",
	"Settings - Start Food",
	"Settings - Multiplayer Game Type",
	"Disallow Diplomacy",
	"Renaissance Tech Level",
	"Imperial Tech Level",
	"Pagan Tech Level",
	"Tribute Received Type",
	"Random Barbarians Chosen",
	"This Prestige Level",
	"Current Prestige",
	"Number Of Capturable Flags",
	"Diplomacy Enabled",
	"Skirmish Campaign Level",
	"Military Status",
	"I Am A Player Designed AI General",
	"Chattiness AI Stat",
	"Friendliness AI Stat",
	"Pride AI Stat",
	"Treachery AI Stat",
	"Greediness AI Stat",
	"Diplomatic Frequency AI Stat",
	"Toady - Neutral - Arrogant AI Stat",
	"Fervour AI Stat",
	"Leadership AI Stat",
	"Heavy - Light AI Stat",
	"Infantry - Cavalry AI Stat",
	"Pagan - Tech - Imperial AI Stat",
	"Raiding - Siege AI Stat",
	"Economic - Military AI Stat",
	"Valhalla Resource Points",
	"No. Of Lives Left",
	"Amount Of Resources",
	"Valhalla Mode Chosen",
	"I Am A Kingmaker Player",
	"Diplomatic Status New",
	"Diplomatic Status Old",
	"Diplomatic Status Player ID",
	"Tribute Received Amount",
	"Tribute Received Player ID",
	"Victory Time Limit",
	"Battle Mode Food",
	"King Piece Lose Condition Set",
	"I am a Dock",
	"I am a Cog",
	"Wood Repair Cost",
	"Food Repair Cost",
	"Gold Repair Cost",
	"Invulnerable",
	"Trade Goods",
	"Lose on Manor Destroyed",
	"Number of Farmers",
	"Idle Peasant Count",
	"Gold Miner Count",
	"Stone Miner Count",
	"Builder Count",
	"Player Shut Down",
	"I am a tree",
	"Shop Keeper Count",
	"Wood Chopper Count",
	"Food Support Value",
	"Player Score",
	"I am a stone sack",
	"I am a gold sack",
	"I am a food sack",
	"I am a wood sack",
	"Single Click Selects All Units Of This Type",
	"I Am A Cart Sans Rider",
	"I Am A Cart",
	"Light Cavalry Weapon Upgrade",
	"Light Cavalry Armour Upgrade",
	"Heavy Cavalry Weapon Upgrade",
	"Heavy Cavalry Armour Upgrade",
	"Light Infantry Weapon Upgrade",
	"Light Infantry Armour Upgrade",
	"Heavy Infantry Weapon Upgrade",
	"Heavy Infantry Armour Upgrade",
	"Maximum Experience",
	"Experience",
	"Front Armour",
	"Missile Damage",
	"H2H Damage",
	"Palace Building",
	"I Am A Peasant",
	"Total Population Count",
	"Population Count",
	"Sight Range",
	"Follow Y Offset",
	"Follow X Offset",
	"Race Type",
	"Wall Index",
	"Maximum Missile Range",
	"Charge Bar Capacity",
	"Charge Bar",
	"Building Paid For",
	"I'm A Military Unit",
	"Actual Sight Range",
	"I Am A Merchant",
	"Food Consumption",
	"Lead Unit Relative Speed",
	"Movement Maximum Relative Speed",
	"Movement Minimum Relative Speed",
	"Is a Foundation",
	"Structural Integrity Capacity",
	"Structural Integrity",
	"Hit Point Capacity",
	"Hit Point Capacity Of Object Being Spawned",
	"Hit Points Of Object Being Spawned",
	"Hit Points",
	"I Am A Village",
	"Stone",
	"Wood",
	"Wood Cost",
	"Food",
	"Food Cost",
	"Gold",
	"Gold Cost"
	};
}

std::map<std::string, std::map<std::string, std::string, StrCICompareClass>, StrCICompareClass> g_gameSetFolderOverrides;

// The text of a file in an overridden folder, or null
static const std::string* OverriddenFile(const std::string& path)
{
	const size_t slash = path.rfind('\\');
	if (slash == std::string::npos)
		return nullptr;
	auto folder = g_gameSetFolderOverrides.find(path.substr(0, slash));
	if (folder == g_gameSetFolderOverrides.end())
		return nullptr;
	auto file = folder->second.find(path.substr(slash + 1));
	return file == folder->second.end() ? nullptr : &file->second;
}

void GameSet::parseFile(const char * fn, int pass)
{
	static std::set<std::string, StrCICompareClass> processedFiles;
	static int callcount = 0;
	callcount++;
	processedFiles.insert(fn);
	//printf("Processing %s\n", fn);

	std::string directory;
	if (const char *lastbs = strrchr(fn, '\\')) {
		directory = std::string(fn, lastbs - fn + 1);
	}

	char *text; int filesize;
	if (const std::string* given = OverriddenFile(fn)) {
		filesize = (int)given->size();
		text = (char*)malloc(filesize + 1);
		memcpy(text, given->data(), filesize);
	}
	else
		LoadFile(fn, &text, &filesize, 1);
	text[filesize] = 0;
	GSFileParser gsf(text);
	gsf.fileName = fn;
	gsf.fileIndex = gsfFileList.insertString(fn);

	std::vector<std::string> linkedFiles;

	int linecnt = 1;
	while (!gsf.eof)
	{
		//printf("Line %i\n", linecnt++);
		std::string strtag = gsf.nextTag();
		int tag = Tags::GAMESET_tagDict.getTagID(strtag.c_str());
		bool isExtension = false;

		if (tag == Tags::GAMESET_LINK_GAME_SET)
		{
			std::string lf = gsf.nextString(true);
			if(lf.find('*') == std::string::npos)
				linkedFiles.push_back(lf);
			else {
				// "AI Generals\*.gsf": every file of that folder with that ending (data.bcp, mods,
				// the user folder's saved\, where the AI General Editor writes), in name order
				const size_t slash = lf.rfind('\\');
				const std::string dir = slash == std::string::npos ? std::string() : lf.substr(0, slash);
				const std::string ending = lf.substr(lf.find('*') + 1);
				std::unique_ptr<std::vector<std::string>> files(ListFiles(("Warrior Kings Game Set\\" + dir).c_str()));
				if (auto over = g_gameSetFolderOverrides.find("Warrior Kings Game Set\\" + dir); over != g_gameSetFolderOverrides.end()) {
					files = std::make_unique<std::vector<std::string>>();
					for (const auto& [name, text] : over->second)
						files->push_back(name);
				}
				std::vector<std::string> found;
				if (files)
					for (const std::string& f : *files)
						if (f.size() >= ending.size() && StrCICompare(std::string_view(f).substr(f.size() - ending.size()), ending) == 0)
							found.push_back(dir.empty() ? f : dir + "\\" + f);
				std::sort(found.begin(), found.end());
				linkedFiles.insert(linkedFiles.end(), found.begin(), found.end());
			}
		}
		else if (pass == 0) {
			switch (tag) {
			case Tags::GAMESET_DECLARE_ITEM:
			case Tags::GAMESET_INDIVIDUAL_ITEM: {
				items.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_DEFINE_VALUE: {
				std::string tag = gsf.nextString(true);
				definedValues[tag] = gsf.nextFloat();
				break;
			}
			case Tags::GAMESET_APPEARANCE_TAG: {
				appearances.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_DECLARE_ALIAS: {
				aliases.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_EQUATION: {
				equations.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_ACTION_SEQUENCE: {
				actionSequences.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_COMMAND: {
				commands.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_ANIMATION_TAG: {
				animations.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_ORDER: {
				orders.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_TASK: {
				tasks.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_ORDER_ASSIGNMENT: {
				orderAssignments.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_GAME_EVENT: {
				events.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_REACTION: {
				reactions.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_PACKAGE_RECEIPT_TRIGGER: {
				prTriggers.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_OBJECT_CREATION: {
				objectCreations.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_OBJECT_FINDER_DEFINITION: {
				objectFinderDefinitions.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_ASSOCIATE_CATEGORY: {
				associations.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_TYPE_TAG: {
				typeTags.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_VALUE_TAG: {
				valueTags.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_DIPLOMATIC_LADDER: {
				gsf.advanceLine();
				auto tag = gsf.nextTag();
				while (tag != "END_DIPLOMATIC_LADDER") {
					if (tag == "STATUS")
						diplomaticStatuses.names.insertString(gsf.nextString(true));
					gsf.advanceLine();
					tag = gsf.nextTag();
				}
				break;
			}
			case Tags::GAMESET_CONDITION: {
				conditions.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_GAME_TEXT_WINDOW: {
				gameTextWindows.names.insertString(gsf.nextString(true));
				ignoreBlueprintEx(gsf, "GAME_TEXT_WINDOW_END");
				break;
			}
			case Tags::GAMESET_TASK_CATEGORY: {
				taskCategories.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_ORDER_CATEGORY: {
				orderCategories.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_PACKAGE: {
				packages.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_3D_CLIP: {
				clips.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_CAMERA_PATH: {
				cameraPaths.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_SOUND_TAG: {
				soundTags.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_SOUND: {
				sounds.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_MUSIC_TAG: {
				musicTags.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_SPECIAL_EFFECT_TAG: {
				specialEffectTags.names.insertString(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_FOOTPRINT: {
				footprints.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_TERRAIN: {
				terrains.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_PLAN: {
				plans.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_ARMY_CREATION_SCHEDULE: {
				armyCreationSchedules.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_WORK_ORDER: {
				workOrders.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_COMMISSION: {
				commissions.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_CHARACTER_LADDER: {
				characterLadders.names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}

			case Tags::GAMESET_LEVEL:
			case Tags::GAMESET_PLAYER:
			case Tags::GAMESET_CHARACTER:
			case Tags::GAMESET_BUILDING:
			case Tags::GAMESET_MARKER:
			case Tags::GAMESET_CONTAINER:
			case Tags::GAMESET_PROP:
			case Tags::GAMESET_CITY:
			case Tags::GAMESET_TOWN:
			case Tags::GAMESET_FORMATION:
			case Tags::GAMESET_ARMY:
			case Tags::GAMESET_MISSILE:
			case Tags::GAMESET_TERRAIN_ZONE:
			case Tags::GAMESET_USER:
			{
				int cls = Tags::GAMEOBJCLASS_tagDict.getTagID(strtag.c_str());
				objBlueprints[cls].names.insertString(gsf.nextString(true));
				ignoreBlueprint(gsf, strtag);
				break;
			}
			}
		}
		else if (pass == 1) {
			switch (tag) {
			case Tags::GAMESET_PASSABILITY_FIELDS: {
				std::vector<bool>& passable = passabilityFields[gsf.nextString(true)];
				passable.assign(terrains.size(), false);
				gsf.advanceLine();
				while (!gsf.eof) {
					const std::string ptag = gsf.nextTag();
					if (ptag == "END_PASSABILITY_FIELDS")
						break;
					if (ptag == "PASSABLE_TERRAIN") {
						const int t = terrains.names.getIndex(gsf.nextString(true));
						if (t >= 0)
							passable[t] = true;
					}
					gsf.advanceLine();
				}
				break;
			}
			case Tags::GAMESET_DECLARE_ITEM: {
				int x = items.names.getIndex(gsf.nextString(true));
				//int side = 0;
				//itemSides[x] = side;
				break;
			}
			case Tags::GAMESET_LEVEL_EXTENSION:
			case Tags::GAMESET_PLAYER_EXTENSION:
			case Tags::GAMESET_CHARACTER_EXTENSION:
			case Tags::GAMESET_BUILDING_EXTENSION:
			case Tags::GAMESET_MARKER_EXTENSION:
			case Tags::GAMESET_CONTAINER_EXTENSION:
			case Tags::GAMESET_PROP_EXTENSION:
			case Tags::GAMESET_CITY_EXTENSION:
			case Tags::GAMESET_TOWN_EXTENSION:
			case Tags::GAMESET_FORMATION_EXTENSION:
			case Tags::GAMESET_ARMY_EXTENSION:
			case Tags::GAMESET_MISSILE_EXTENSION:
			//case Tags::GAMESET_TERRAIN_ZONE_EXTENSION:
			//case Tags::GAMESET_USER_EXTENSION:
				isExtension = true;
				strtag.resize(strtag.find("_EXTENSION"));
			case Tags::GAMESET_LEVEL:
			case Tags::GAMESET_PLAYER:
			case Tags::GAMESET_CHARACTER:
			case Tags::GAMESET_BUILDING:
			case Tags::GAMESET_MARKER:
			case Tags::GAMESET_CONTAINER:
			case Tags::GAMESET_PROP:
			case Tags::GAMESET_CITY:
			case Tags::GAMESET_TOWN:
			case Tags::GAMESET_FORMATION:
			case Tags::GAMESET_ARMY:
			case Tags::GAMESET_MISSILE:
			case Tags::GAMESET_TERRAIN_ZONE:
			case Tags::GAMESET_USER:
			{
				int cls = Tags::GAMEOBJCLASS_tagDict.getTagID(strtag.c_str());
				std::string name = gsf.nextString(true);
				int bpx = objBlueprints[cls].names.getIndex(name);
				objBlueprints[cls][bpx].init(cls, bpx, name, this);
				objBlueprints[cls][bpx].parse(gsf, directory, isExtension);
				break;
			}
			case Tags::GAMESET_EQUATION: {
				int x = equations.readIndex(gsf);
				equations[x] = ReadEquationNode(gsf, *this);
				break;
			}
			case Tags::GAMESET_ACTION_SEQUENCE: {
				int x = actionSequences.readIndex(gsf);
				actionSequences[x].init(gsf, *this, "END_ACTION_SEQUENCE");
				break;
			}
			case Tags::GAMESET_COMMAND: {
				int x = commands.readIndex(gsf);
				commands[x].id = x;
				commands[x].parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_ORDER: {
				int x = orders.readIndex(gsf);
				orders[x].bpid = x;
				orders[x].parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_TASK: {
				int x = tasks.readIndex(gsf);
				tasks[x].bpid = x;
				tasks[x].parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_ORDER_ASSIGNMENT: {
				int x = orderAssignments.readIndex(gsf);
				orderAssignments[x].parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_REACTION: {
				Reaction &r = reactions.readRef(gsf);
				r.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_PACKAGE_RECEIPT_TRIGGER: {
				PackageReceiptTrigger &prt = prTriggers.readRef(gsf);
				prt.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_OBJECT_CREATION: {
				ObjectCreation &oc = objectCreations.readRef(gsf);
				oc.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_OBJECT_FINDER_DEFINITION: {
				int ofd = objectFinderDefinitions.readIndex(gsf);
				objectFinderDefinitions[ofd] = ReadFinderNode(gsf, *this);
				//ignoreBlueprint(gsf, strtag);
				break;
			}
			case Tags::GAMESET_DEFAULT_VALUE_TAG_INTERPRETATION: {
				int vt = valueTags.readIndex(gsf);
				if (vt != -1)
					valueTags[vt].reset(ReadValueDeterminer(gsf, *this));
				break;
			}
			case Tags::GAMESET_DEFAULT_DIPLOMATIC_STATUS: {
				defaultDiplomaticStatus = diplomaticStatuses.readIndex(gsf);
				break;
			}
			case Tags::GAMESET_CONDITION: {
				GSCondition &cond = conditions.readRef(gsf);
				cond.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_GAME_TEXT_WINDOW: {
				GameTextWindow& gtw = gameTextWindows.readRef(gsf);
				gtw.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_PACKAGE: {
				GSPackage& pack = packages.readRef(gsf);
				pack.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_3D_CLIP: {
				GS3DClip& clip = clips.readRef(gsf);
				clip.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_CAMERA_PATH: {
				CameraPath& cp = cameraPaths.readRef(gsf);
				cp.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_SOUND: {
				GSSound& snd = sounds.readRef(gsf);
				snd.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_GLOBAL_MAP_SOUND_TAG: {
				int st = soundTags.readIndex(gsf);
				int unk1 = gsf.nextInt();
				auto unk2 = gsf.nextString();
				globalSoundMap[st].push_back(gsf.nextString(true));
				break;
			}
			case Tags::GAMESET_GLOBAL_SPECIAL_EFFECT_MAPPING: {
				auto& vec = specialEffectTags.readRef(gsf);
				vec.push_back(modelCache.getModel("Warrior Kings Game Set\\" + gsf.nextString(true)));
				break;
			}
			case Tags::GAMESET_FOOTPRINT: {
				Footprint& fp = footprints.readRef(gsf);
				fp.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_TERRAIN: {
				GSTerrain& trn = terrains.readRef(gsf);
				trn.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_PLAN: {
				PlanNodeSequence& plan = plans.readRef(gsf);
				plan.parse(gsf, *this, "END_PLAN");
				break;
			}
			case Tags::GAMESET_ARMY_CREATION_SCHEDULE: {
				ArmyCreationSchedule& acs = armyCreationSchedules.readRef(gsf);
				acs.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_WORK_ORDER: {
				WorkOrder& wo = workOrders.readRef(gsf);
				wo.parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_COMMISSION: {
				commissions.readRef(gsf).parse(gsf, *this);
				break;
			}
			case Tags::GAMESET_CHARACTER_LADDER: {
				characterLadders.readRef(gsf).parse(gsf, *this);
				break;
			}
			}
		}

		gsf.advanceLine();
	}

	free(text);

	for (std::string &lf : linkedFiles) {
		std::string lfpath = "Warrior Kings Game Set\\" + lf;
		if (processedFiles.count(lfpath))
			continue;
		if (!OverriddenFile(lfpath) && !FileExists(lfpath.c_str())) {
			// Usually mods that don't fit together in the current load order: skip and warn after loading
			if (pass == 0) {
				printf("WARNING: Linked game set file %s not found (linked from %s), skipping it\n", lfpath.c_str(), fn);
				missingLinkedFiles.push_back(lfpath + " (linked from " + fn + ")");
			}
			continue;
		}
		parseFile(lfpath.c_str(), pass);
	}

	if (--callcount == 0)
		processedFiles.clear();
}

void GameSet::load(const char * fn)
{
	appearances.names.insertString("Default");
	animations.names.insertString("Default");
	animations.names.insertString("Idle");
	soundTags.names.insertString("Selected");
	soundTags.names.insertString("Death");
	for (auto &pde : Tags::PDEVENT_tagDict.tags) {
		std::string s = pde;
		for (char &c : s) if (c == '_') c = ' ';
		events.names.insertString(s);
	}
	for (auto& pdi : Tags::PDITEM_tagDict.tags) {
		std::string s = pdi;
		for (char& c : s) if (c == '_') c = ' ';
		items.names.insertString(s);
	}
	for (const char* item : ENGINE_ITEMS)
		items.names.insertString(item);
	for (const char* event : ENGINE_EVENTS)
		events.names.insertString(event);

	printf("Gameset pass 1...\n");
	parseFile(fn, 0);

	for (auto &objbp : objBlueprints)
		objbp.pass();
	items.pass();
	equations.pass();
	actionSequences.pass();
	appearances.pass();
	commands.pass();
	animations.pass();
	orders.pass();
	tasks.pass();
	orderAssignments.pass();
	aliases.pass();
	events.pass();
	reactions.pass();
	prTriggers.pass();
	objectCreations.pass();
	objectFinderDefinitions.pass();
	associations.pass();
	typeTags.pass();
	valueTags.pass();
	diplomaticStatuses.pass();
	conditions.pass();
	gameTextWindows.pass();
	taskCategories.pass();
	orderCategories.pass();
	packages.pass();
	clips.pass();
	cameraPaths.pass();
	soundTags.pass();
	sounds.pass();
	musicTags.pass();
	specialEffectTags.pass();
	footprints.pass();
	terrains.pass();
	plans.pass();
	armyCreationSchedules.pass();
	workOrders.pass();
	commissions.pass();
	characterLadders.pass();

	printf("Gameset pass 2...\n");
	parseFile(fn, 1);
	printf("Gameset loaded!\n");

	if (!missingLinkedFiles.empty()) {
		std::string msg = "These game set files are linked but missing, and were skipped:\n\n";
		for (const std::string& missing : missingLinkedFiles)
			msg += "- " + missing + "\n";
		msg += "\nThe game may not work correctly. This usually means the active mods don't fit together "
			"in the current load order. Try \"Use original order\" or disabling a mod in the Mods menu.";
		ShowWarningDialog("Missing game set files", msg.c_str());
	}
}

const GameObjBlueprint* GameSet::readObjBlueprintPtr(GSFileParser& gsf) const
{
	int bpclass = Tags::GAMEOBJCLASS_tagDict.getTagID(gsf.nextString().c_str());
	if (bpclass == -1) return nullptr;
	return objBlueprints[bpclass].readPtr(gsf);
}
