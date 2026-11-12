// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <cstdint>
#include <queue>
#include <memory>
#include <string>
#include <cassert>
#include <mutex>
#include "util/BinaryReader.h"
#include "util/vecmat.h"

enum NetClientMessages {
	NETCLIMSG_HELLO = 1,
	NETCLIMSG_TEST,
	NETCLIMSG_GAME_SET,
	NETCLIMSG_OBJECT_CREATED,
	NETCLIMSG_OBJECT_PARENT_SET,
	NETCLIMSG_OBJECT_ITEM_SET,
	NETCLIMSG_TERRAIN_SET,
	NETCLIMSG_OBJECT_POSITION_SET,
	NETCLIMSG_OBJECT_ORIENTATION_SET,
	NETCLIMSG_OBJECT_SUBTYPE_APPEARANCE_SET,
	NETCLIMSG_OBJECT_COLOR_SET,
	NETCLIMSG_TIME_SYNC,
	NETCLIMSG_OBJECT_ANIM_SET,
	NETCLIMSG_OBJECT_MOVEMENT_STARTED,
	NETCLIMSG_OBJECT_MOVEMENT_STOPPED,
	NETCLIMSG_OBJECT_REMOVED,
	NETCLIMSG_OBJECT_CONVERTED,
	NETCLIMSG_DIPLOMATIC_STATUS_SET,
	NETCLIMSG_SHOW_GAME_TEXT_WINDOW,
	NETCLIMSG_OBJECT_SCALE_SET,
	NETCLIMSG_HIDE_GAME_TEXT_WINDOW,
	NETCLIMSG_HIDE_CURRENT_GAME_TEXT_WINDOW,
	NETCLIMSG_OBJECT_FLAGS_SET,
	NETCLIMSG_OBJECT_DISABLE_COUNT_SET,
	NETCLIMSG_STORE_CAMERA_POSITION,
	NETCLIMSG_RESTORE_CAMERA_POSITION,
	NETCLIMSG_PLAY_CAMERA_PATH,
	NETCLIMSG_STOP_CAMERA_PATH,
	NETCLIMSG_OBJECT_TRAJECTORY_STARTED,
	NETCLIMSG_GAME_SPEED_CHANGED,
	NETCLIMSG_SOUND_AT_OBJECT,
	NETCLIMSG_SOUND_AT_POSITION,
	NETCLIMSG_MUSIC_CHANGED,
	NETCLIMSG_CONTROL_PLAYER,
	NETCLIMSG_SNAP_CAMERA_POSITION,
	NETCLIMSG_SPECIAL_EFFECT_PLAYED_AT_POS,
	NETCLIMSG_SPECIAL_EFFECT_PLAYED_BETWEEN,
	NETCLIMSG_ATTACHED_SPECIAL_EFFECT_PLAYED,
	NETCLIMSG_OBJECT_NAME_SET,
	NETCLIMSG_CURRENT_ORDER_REPORT,
	NETCLIMSG_LOOPING_SPECIAL_EFFECT_ATTACHED,
	NETCLIMSG_LOOPING_SPECIAL_EFFECT_DETACHED,
	NETCLIMSG_SKIP_CAMERA_PATH,
	NETCLIMSG_ALIAS_ASSIGNED,
	NETCLIMSG_ALIAS_UNASSIGNED,
	NETCLIMSG_ALIAS_CLEARED,
	NETCLIMSG_INTERPOLATE_CAMERA_TO_POSITION,
	NETCLIMSG_INTERPOLATE_CAMERA_TO_STORED_POSITION,
	NETCLIMSG_UPDATE_BUILDING_ORDER_COUNT_MAP,
	NETCLIMSG_CITY_RECTANGLE_ADDED,
	NETCLIMSG_BUILDING_SPAWNED_UNIT_ORDER_CHANGED,
	// The multiplayer lobby as the host shows it, for the guests (text, in parts: a packet is small)
	NETCLIMSG_LOBBY_STATE,
	// The host's AI General files (the game set links AI Generals\\*.gsf), for the guests to load
	// the same game set: file index, file count, part, parts, name, text (in parts)
	NETCLIMSG_GAME_SET_FILE,
	// Terrain vertices set to one height (a building levelling the ground under it): count,
	// height in steps (float), the vertex indices
	NETCLIMSG_TERRAIN_HEIGHTS,
	// The loading screen's rows, one per client (human player): count, then for each its stage
	// (1-8), its player's colour index, whether it is the receiving client, its name
	NETCLIMSG_LOADING_STAGES,
	// The host turned the guest away (a wrong password, a running game with no place left): the
	// reason; the host disconnects it
	NETCLIMSG_JOIN_REFUSED,
	// A running game's places for a guest who joined it, before the game's state: text, in parts
	// as NETCLIMSG_LOBBY_STATE, a line each: a title, then "A|name|colour|team|player id" (A: an AI
	// plays it, D: its human left), a last line "S|n": n spectator places
	NETCLIMSG_SEATS,
	// The client watches (a spectator): no player of its own, no fog of war, its orders ignored
	NETCLIMSG_SPECTATOR,
	// What the host works out from its own files, that a client's own files could have
	// otherwise: kind (1: the timeline's map regions, MapRegions' JSON; 2: the map's water
	// design, WaterDesign's JSON, empty: none), part, parts (16 bits each), length (8 bits),
	// the bytes
	NETCLIMSG_HOST_DATA,
	// (NETCLIMSG_HOST_DATA kinds 3 and 4: a joining guest's mods for this game, when the host
	// sends its mods: 4 is one logic file of a mod the guest lacks, "<mod index>\n<game path>\n
	// <timestamp>\n" then its bytes; 3, after the files, the mods to mount in order, JSON
	// { "customOrder", "mods": [ { "local": "<the guest's mod id>" } or { "sent": <index>,
	// "name" } ] }: the guest mounts them and says hello again)
	// wkbai's day and night, seasons and weather (Environment.h), once a game second: flags (1 day
	// and night, 2 seasons), day length (16 bits, minutes), start hour, season length (16 bits),
	// start season, temperature scale, then EnvironmentState::write
	NETCLIMSG_ENVIRONMENT,
	// the trees a wildfire charred (wkbai's seasons; drawn black and bare): reset (1: the client
	// drops the ones it has first), count (up to 60), the ids (32 bits each); the new ones after
	// each NETCLIMSG_ENVIRONMENT, all of them to a client joining
	NETCLIMSG_CHARRED_TREES,
	// wkbai's unit history: an object's kill counts (UnitHistory): its id, the victim's (0: no new
	// kill, the counts only: a client joining, a game loaded), kills, buildings destroyed (32 bits each)
	NETCLIMSG_UNIT_KILLS,
	// a tracked unit's history (UnitHistory::sendRecord): id, birth (float, game s), kind (8 bits:
	// person, ship, no name, own name), flags (8 bits: hero, age unknown, just became a hero,
	// dead, female), the name shown and the birth name (zero-ended)
	NETCLIMSG_UNIT_HISTORY,
	// a unit no longer tracked (a conscript back to the fields without a kill, a dead one that
	// didn't count): its id
	NETCLIMSG_UNIT_UNTRACKED,
	// the kills to become a hero (32 bits)
	NETCLIMSG_HERO_THRESHOLD,
	// the server's timeline and statistics (ServerChronicle): a line of text ("RESET", "EVENT ...",
	// "SAMPLE ...") in parts: part, parts (16 bits each), length (8 bits), the bytes
	NETCLIMSG_CHRONICLE,
	// a player's game is over (REMOVE_MULTIPLAYER_PLAYER, Server::finishedPlayers): its id (32
	// bits), the outcome (8 bits: 1 won, 2 lost); id 0: the list starts over (a game loaded, a
	// guest joining; the players follow). Its client then sees the whole map and everyone's statistics.
	NETCLIMSG_PLAYER_FINISHED,
	// the player's mission objectives (MISSION_OBJECTIVES): SET, the active set's index (32 bits,
	// -1: none), its entries at their start states; ENTRY, an entry's key (string) and state
	// (8 bits: MissionObjectives::State)
	NETCLIMSG_MISSION_OBJECTIVES_SET,
	NETCLIMSG_MISSION_OBJECTIVES_ENTRY,
	// diplomacy (ServerDiplomacy.cpp): an offer from a player to another, from and to ids (32 bits
	// each), the status offered (8 bits signed, -1: withdrawn)
	NETCLIMSG_DIPLOMATIC_OFFER,
	// an AI General's message (SEND_CHAT_MESSAGE) to this client's player: the sender's id (32
	// bits), the Language key (zero-ended), the number of values (8 bits), the values (floats)
	NETCLIMSG_CHAT_DISPLAY,
	// a tribute to this client's player: the giver's id (32 bits), the item (16 bits), the amount (float)
	NETCLIMSG_TRIBUTE_RECEIVED,
	// a top bar diplomacy button on this client's player's screen: the window (8 bits:
	// Server::DiplomacyWindow), enabled (8 bits), the tooltip's Language key (zero-ended)
	NETCLIMSG_DIPLOMACY_WINDOW,
	// a disguise (ADOPT_APPEARANCE_FOR, Server::Disguise): the object id (32 bits), the blueprint's
	// full id (32 bits, 0: none), subtype and appearance (32 bits each); seen by every enemy again
	NETCLIMSG_DISGUISE,
	// the disguise unmasked for this client's player (ADOPT_DEFAULT_APPEARANCE_FOR): the object id
	NETCLIMSG_UNMASK,
	// REVEAL_FOG_OF_WAR for this client's player: the object id (32 bits), the seconds (float, 0:
	// for good), the radius in 5 m tiles (float)
	NETCLIMSG_FOG_REVEAL,
	// a minimap alert for this client's player (SHOW_BLINKING_DOT_ON_MINIMAP, COLLAPSING_CIRCLE_ON_MINIMAP):
	// circle (8 bits), x and z (floats), the colour (32 bits, Server::sendMinimapAlert), the seconds
	// (float), the circles (8 bits)
	NETCLIMSG_MINIMAP_ALERT,
};

enum NetServerMessages {
	NETSRVMSG_HELLO = 1,
	NETSRVMSG_TEST,
	NETSRVMSG_COMMAND,
	NETSRVMSG_PAUSE,
	NETSRVMSG_STAMPDOWN,
	NETSRVMSG_START_LEVEL,
	NETSRVMSG_GAME_TEXT_WINDOW_BUTTON_CLICKED,
	NETSRVMSG_CAMERA_PATH_ENDED,
	NETSRVMSG_CHANGE_GAME_SPEED,
	NETSRVMSG_MUSIC_COMPLETED,
	NETSRVMSG_TERMINATE_OBJECT,
	NETSRVMSG_BUILD_LAST_STAMPDOWNED_OBJECT,
	NETSRVMSG_CANCEL_COMMAND,
	NETSRVMSG_PUT_UNIT_INTO_NEW_FORMATION,
	NETSRVMSG_CREATE_NEW_FORMATION,
	NETSRVMSG_SET_BUILDING_SPAWNED_UNIT_ORDER_TO_TARGET,
	NETSRVMSG_SET_BUILDING_SPAWNED_UNIT_ORDER_TO_DESTINATION,
	// A guest joining the lobby: its player name
	NETSRVMSG_LOBBY_HELLO,
	// A guest asking the lobby's host for something: "slot <n>", "colour <c>", "team <t>", "ready <0|1>"
	NETSRVMSG_LOBBY_REQUEST,
	// A client's loading stage (1-8), for the others' loading screens
	NETSRVMSG_LOADING_STAGE,
	// A guest who joined a running game takes a place: the player's id (one NETCLIMSG_SEATS
	// offered), 0: it watches
	NETSRVMSG_TAKE_SEAT,
	// The guest's mods (ModManifest, JSON), before its NETSRVMSG_LOBBY_HELLO: part, parts (16 bits
	// each), length (8 bits), the bytes
	NETSRVMSG_MOD_MANIFEST,
	// The environment's debug panel (F9, single player): a value (float), then its name (zero-ended)
	// for ServerEnvironment::debugSet. As every message, recorded, so a replay does the same.
	NETSRVMSG_ENVIRONMENT_DEBUG,
	// The Diplomacy window's Send (WKB.exe's 0x1a and 0xb7): a status set directly with a player
	// (declaring war, accepting an offer), the other player's id (32 bits), the status (8 bits);
	// and an offer to a player, its id (32 bits), the status (8 bits signed, -1: withdrawn)
	NETSRVMSG_DIPLOMATIC_STATUS,
	NETSRVMSG_DIPLOMATIC_OFFER,
	// The Tributes window's Send (WKB.exe's 0xa9): the receiver's id (32 bits), the item (16
	// bits), the amount (float)
	NETSRVMSG_TRIBUTE,
	// R held with a right-click (WKB.exe's patrol): the object (32 bits), whether the point is one
	// more of its patrol (8 bits: Shift), the point (3 floats)
	NETSRVMSG_PATROL,
};

struct NetPacketWriter {
	static const size_t NET_MAX_PACKET_SIZE = 256;

	uint8_t *nextWritePnt;
	uint8_t data[NET_MAX_PACKET_SIZE];

	size_t size() const { return nextWritePnt - data; }
	size_t rem() const { return NET_MAX_PACKET_SIZE - size(); }

	void writeUint8(uint8_t a) { assert(rem() >= 1);  *nextWritePnt = a; nextWritePnt++; }
	void writeUint16(uint16_t a) { assert(rem() >= 2); *(uint16_t*)nextWritePnt = a; nextWritePnt += 2; }
	void writeUint32(uint32_t a) { assert(rem() >= 4); *(uint32_t*)nextWritePnt = a; nextWritePnt += 4; }
	void writeFloat(float a) { assert(rem() >= 4); *(float*)nextWritePnt = a; nextWritePnt += 4; }
	void writeString(const std::string &str) { size_t len = str.size();  assert(rem() >= len); memcpy(nextWritePnt, str.c_str(), len); nextWritePnt += len; }
	void writeStringZ(const std::string &str) { writeString(str); writeUint8(0); }
	void writeVector3(const Vector3 &vec) { writeFloat(vec.x); writeFloat(vec.y); writeFloat(vec.z); }

	void writeValues(uint8_t a) { writeUint8(a); }
	void writeValues(uint16_t a) { writeUint16(a); }
	void writeValues(uint32_t a) { writeUint32(a); }
	void writeValues(int32_t a) { writeUint32((uint32_t)a); }
	void writeValues(float a) { writeFloat(a); }
	void writeValues(const std::string & str) { writeStringZ(str); }
	void writeValues(const Vector3 &vec) { writeVector3(vec); }
	template<typename T, typename ... Args> void writeValues(T val, Args ... rest) { writeValues(val); writeValues(rest...); }

	std::string getString() const { return std::string((char*)data, size()); }

	//NetPacketWriter() : nextWritePnt(data) {}
	NetPacketWriter(uint8_t type) { nextWritePnt = data;  writeUint8(type); }
};

struct NetPacket {
	//std::unique_ptr<uint8_t> data;
	std::string data;
	NetPacket(const std::string &data) : data(data) {}
	NetPacket(const std::string &&data) : data(data) {}
};

struct NetLocalBuffer {
	std::mutex mutex;
	std::queue<NetPacket> queue;
};

struct NetLink {
	// Reading
	virtual bool available() = 0;
	virtual NetPacket receive() = 0;
	virtual void send(const NetPacket &packet) = 0;

	void send(const NetPacketWriter &writer) { send(NetPacket(writer.getString())); }

	// Ends the connection (the other side sees it gone): at once, or once what was sent before
	// has gone out. Links of no network (NetLocalLink) do nothing.
	virtual void disconnect(bool afterSending) {}
	// The newest packet of that type waiting to be read, left in the queue (the loading screen's
	// rows while the client is busy loading); none: false
	virtual bool peekLast(uint8_t type, std::string& data) { return false; }
	virtual ~NetLink() = default;

	//int beginRead() = 0;
	//void endRead() = 0;
};

struct NetLocalLink : NetLink {
	NetLocalBuffer *inMessages, *outMessages;

	bool available() override;
	NetPacket receive() override;
	void send(const NetPacket &packet) override;
	bool peekLast(uint8_t type, std::string& data) override;

	NetLocalLink(NetLocalBuffer *inMessages, NetLocalBuffer *outMessages) : inMessages(inMessages), outMessages(outMessages) {}
};
