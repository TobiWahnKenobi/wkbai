// wkbai - diplomacy on the server, as WKB.exe's players have it (docs/engine/diplomacy.md):
// status changes, offers, AI Generals' messages, tributes, the diplomacy windows' switches.
// Licensed under the GNU General Public License 3

#include "server.h"
#include "network.h"
#include "gameset/gameset.h"
#include "gameset/values.h"
#include "gameset/ScriptContext.h"
#include "util/SimRandom.h"
#include <algorithm>

namespace {
	int ItemIndex(const GameSet* gs, const char* name) { return gs->items.names.getIndex(name); }
	void SetNamedItem(ServerGameObject* obj, const GameSet* gs, const char* name, float value)
	{
		if (const int item = ItemIndex(gs, name); item >= 0)
			obj->setItem(item, value);
	}
}

// WKB.exe's player SetStatus (0x5768a0): symmetric; a status at least as good as an offer
// between the two ends it; the items "Diplomatic Status Player ID/Old/New" on both, each with the
// other's id, and "On Diplomatic Status Changed" to both with the other as sender
void Server::setDiplomaticStatus(ServerGameObject* a, ServerGameObject* b, int status, ServerGameObject* initiator, bool events)
{
	if (!a || !b || a == b || status < 0)
		return;
	const int old = getDiplomaticStatus(a, b);
	if (old == status)
		return;
	int& ref = (a->id <= b->id) ? diplomaticStatuses[{ a->id, b->id }] : diplomaticStatuses[{ b->id, a->id }];
	ref = status;
	NetPacketWriter msg{ NETCLIMSG_DIPLOMATIC_STATUS_SET };
	msg.writeUint32(a->id);
	msg.writeUint32(b->id);
	msg.writeUint8(status);
	msg.writeUint32(initiator ? initiator->id : 0);
	sendToAll(msg);

	// (a lower ladder index is the better status: Allied, Neutral, Enemy)
	for (auto [from, to] : { std::pair(a, b), std::pair(b, a) }) {
		const int offer = getDiplomaticOffer(from, to);
		if (offer >= 0 && status <= offer)
			makeDiplomaticOffer(from, to, -1);
	}
	if (!events)
		return;
	const int changed = gameSet->events.names.getIndex("On Diplomatic Status Changed");
	for (auto [me, other] : { std::pair(a, b), std::pair(b, a) }) {
		SetNamedItem(me, gameSet.get(), "Diplomatic Status Player ID", (float)other->id);
		SetNamedItem(me, gameSet.get(), "Diplomatic Status Old", (float)old);
		SetNamedItem(me, gameSet.get(), "Diplomatic Status New", (float)status);
		if (changed >= 0)
			me->sendEvent(changed, other);
	}
}

int Server::getDiplomaticOffer(const ServerGameObject* from, const ServerGameObject* to) const
{
	auto it = diplomaticOffers.find({ from->id, to->id });
	return it != diplomaticOffers.end() ? it->second : -1;
}

// WKB.exe's MakeOffer (0x576d20): only a better status than the present one is an offer (it
// sends "Diplomatic Offer - <status>" to the other, from as the sender); anything else withdraws
// the offer there is. Offers don't expire.
void Server::makeDiplomaticOffer(ServerGameObject* from, ServerGameObject* to, int status)
{
	if (!from || !to || from == to)
		return;
	const auto key = std::make_pair(from->id, to->id);
	if (status >= 0 && status < getDiplomaticStatus(from, to)) {
		if (getDiplomaticOffer(from, to) == status)
			return;
		diplomaticOffers[key] = status;
		NetPacketWriter msg{ NETCLIMSG_DIPLOMATIC_OFFER };
		msg.writeUint32(from->id);
		msg.writeUint32(to->id);
		msg.writeUint8((uint8_t)(int8_t)status);
		sendToAll(msg);
		const int ev = gameSet->events.names.getIndex("Diplomatic Offer - " + gameSet->diplomaticStatuses.names.getString(status));
		if (ev >= 0)
			to->sendEvent(ev, from);
		return;
	}
	if (diplomaticOffers.erase(key)) {
		NetPacketWriter msg{ NETCLIMSG_DIPLOMATIC_OFFER };
		msg.writeUint32(from->id);
		msg.writeUint32(to->id);
		msg.writeUint8((uint8_t)(int8_t)-1);
		sendToAll(msg);
	}
}

// WKB.exe's SendChat (0x578bc0): nothing without a chat personality; one of the sender
// blueprint's MAP_CHAT_MESSAGE entries for it and the display, picked with the game's random
// numbers (deterministic), its values evaluated with the sender as self. Only a player with a
// client sees it (the original shows it to the local player only).
void Server::sendChatDisplay(ServerGameObject* from, ServerGameObject* to, const std::string& display)
{
	if (!from || !to)
		return;
	auto pers = chatPersonalities.find(from->id);
	if (pers == chatPersonalities.end())
		return;
	auto bp = from->blueprint->chatMessages.find(pers->second);
	if (bp == from->blueprint->chatMessages.end())
		return;
	auto list = bp->second.find(display);
	if (list == bp->second.end() || list->second.empty())
		return;
	const size_t pick = (size_t)SimRand() * list->second.size() / (SIM_RAND_MAX + 1);
	const GameObjBlueprint::ChatMessage& chat = list->second[std::min(pick, list->second.size() - 1)];
	if (to->clientIndex < 0)
		return;
	NetPacketWriter msg{ NETCLIMSG_CHAT_DISPLAY };
	msg.writeUint32(from->id);
	msg.writeStringZ(chat.key);
	const size_t count = std::min<size_t>(chat.values.size(), 8);
	msg.writeUint8((uint8_t)count);
	SrvScriptContext ctx(this, from);
	for (size_t i = 0; i < count; i++)
		msg.writeFloat(chat.values[i] ? chat.values[i]->eval(&ctx) : 0.0f);
	sendTo(to, msg);
}

// WKB.exe's tribute command (0xa9 in 0x56f850): the amount moved at once (no more than the giver
// has); on the receiver "Tribute Received Player ID", "... Amount" and "... Type" (1 Wood, 2 Food,
// 3 Gold), then "On Tribute Received" with the giver as sender; the receiver's client is told
void Server::giveTribute(ServerGameObject* from, ServerGameObject* to, int item, float amount)
{
	if (!from || !to || from == to || item < 0 || !(amount > 0.0f))
		return;
	if (std::find(gameSet->tributeItems.begin(), gameSet->tributeItems.end(), item) == gameSet->tributeItems.end())
		return;
	amount = std::min(amount, from->getItem(item));
	if (!(amount > 0.0f))
		return;
	from->setItem(item, from->getItem(item) - amount);
	to->setItem(item, to->getItem(item) + amount);
	// (by the item's index: the declared names' spelling varies, "FOOD" in the base game set)
	const float type = item == gameSet->items.names.getIndex("Wood") ? 1.0f : item == gameSet->items.names.getIndex("Food") ? 2.0f
		: item == gameSet->items.names.getIndex("Gold") ? 3.0f : 0.0f;
	SetNamedItem(to, gameSet.get(), "Tribute Received Player ID", (float)from->id);
	SetNamedItem(to, gameSet.get(), "Tribute Received Amount", amount);
	SetNamedItem(to, gameSet.get(), "Tribute Received Type", type);
	if (const int ev = gameSet->events.names.getIndex("On Tribute Received"); ev >= 0)
		to->sendEvent(ev, from);
	if (to->clientIndex >= 0) {
		NetPacketWriter msg{ NETCLIMSG_TRIBUTE_RECEIVED };
		msg.writeUint32(from->id);
		msg.writeUint16((uint16_t)item);
		msg.writeFloat(amount);
		sendTo(to, msg);
	}
}

void Server::setDiplomacyWindow(ServerGameObject* player, int window, bool enabled, const std::string& tooltip)
{
	if (!player || window < 0 || window > 2)
		return;
	diplomacyWindows[{ player->id, window }] = { enabled, tooltip };
	if (player->clientIndex < 0)
		return;
	NetPacketWriter msg{ NETCLIMSG_DIPLOMACY_WINDOW };
	msg.writeUint8((uint8_t)window);
	msg.writeUint8(enabled ? 1 : 0);
	msg.writeStringZ(tooltip);
	sendTo(player, msg);
}

void Server::sendDiplomacyState(int clientIndex)
{
	for (const auto& [pair, status] : diplomaticOffers) {
		NetPacketWriter msg{ NETCLIMSG_DIPLOMATIC_OFFER };
		msg.writeUint32(pair.first);
		msg.writeUint32(pair.second);
		msg.writeUint8((uint8_t)(int8_t)status);
		sendToClient(clientIndex, msg);
	}
	const ServerGameObject* player = clientIndex >= 0 && (size_t)clientIndex < clientPlayerObjects.size() ? clientPlayerObjects[clientIndex].get() : nullptr;
	if (!player)
		return;
	for (const auto& [key, state] : diplomacyWindows) {
		if (key.first != player->id)
			continue;
		NetPacketWriter msg{ NETCLIMSG_DIPLOMACY_WINDOW };
		msg.writeUint8((uint8_t)key.second);
		msg.writeUint8(state.first ? 1 : 0);
		msg.writeStringZ(state.second);
		sendToClient(clientIndex, msg);
	}
}

// Disguises and fog reveals (docs/engine/disguises.md)

void Server::setDisguise(ServerGameObject* obj, const GameObjBlueprint* bp, int subtype, int appearance)
{
	if (!obj || !bp)
		return;
	Disguise& d = disguises[obj->id];
	d.blueprint = bp->getFullId();
	d.subtype = std::max(0, subtype);
	d.appearance = std::max(0, appearance);
	d.unmaskedFor.clear();
	NetPacketWriter msg{ NETCLIMSG_DISGUISE };
	msg.writeUint32(obj->id);
	msg.writeUint32(d.blueprint);
	msg.writeUint32((uint32_t)d.subtype);
	msg.writeUint32((uint32_t)d.appearance);
	sendToAll(msg);
}

void Server::unmaskDisguise(ServerGameObject* obj, const std::vector<ServerGameObject*>& players)
{
	if (!obj)
		return;
	auto it = disguises.find(obj->id);
	if (it == disguises.end())
		return;
	ServerGameObject* own = obj->getPlayer();
	// for its own player (Counter Espionage.cpp's "Discovered" reaction): dropped for everyone
	if (std::find(players.begin(), players.end(), own) != players.end()) {
		disguises.erase(it);
		NetPacketWriter msg{ NETCLIMSG_DISGUISE };
		msg.writeUint32(obj->id);
		msg.writeUint32(0);
		msg.writeUint32(0);
		msg.writeUint32(0);
		sendToAll(msg);
		return;
	}
	for (ServerGameObject* p : players) {
		if (!p)
			continue;
		ServerGameObject* player = p->blueprint->bpClass == Tags::GAMEOBJCLASS_PLAYER ? p : p->getPlayer();
		if (!player || !it->second.unmaskedFor.insert(player->id).second)
			continue;
		NetPacketWriter msg{ NETCLIMSG_UNMASK };
		msg.writeUint32(obj->id);
		sendTo(player, msg);
	}
}

void Server::revealFogOfWar(ServerGameObject* obj, ServerGameObject* player, float duration, float radius)
{
	if (!obj || !player || !(radius > 0.0f))
		return;
	const float until = duration > 0.0f ? timeManager.currentTime + duration : -1.0f;
	// the same object revealed again to the same player: the later end, the larger circle
	bool found = false;
	for (FogReveal& r : fogReveals)
		if (r.object == obj->id && r.player == player->id) {
			r.until = (r.until < 0.0f || until < 0.0f) ? -1.0f : std::max(r.until, until);
			r.radius = std::max(r.radius, radius);
			found = true;
		}
	if (!found)
		fogReveals.push_back({ obj->id, player->id, until, radius });
	NetPacketWriter msg{ NETCLIMSG_FOG_REVEAL };
	msg.writeUint32(obj->id);
	msg.writeFloat(duration > 0.0f ? duration : 0.0f);
	msg.writeFloat(radius);
	sendTo(player, msg);
}

void Server::sendDisguiseState(int clientIndex)
{
	const ServerGameObject* player = clientIndex >= 0 && (size_t)clientIndex < clientPlayerObjects.size() ? clientPlayerObjects[clientIndex].get() : nullptr;
	for (const auto& [id, d] : disguises) {
		NetPacketWriter msg{ NETCLIMSG_DISGUISE };
		msg.writeUint32(id);
		msg.writeUint32(d.blueprint);
		msg.writeUint32((uint32_t)d.subtype);
		msg.writeUint32((uint32_t)d.appearance);
		sendToClient(clientIndex, msg);
		if (player && d.unmaskedFor.count(player->id)) {
			NetPacketWriter un{ NETCLIMSG_UNMASK };
			un.writeUint32(id);
			sendToClient(clientIndex, un);
		}
	}
	if (!player)
		return;
	// the reveals still going (the ended ones are dropped here and when saving)
	fogReveals.erase(std::remove_if(fogReveals.begin(), fogReveals.end(), [this](const FogReveal& r) {
		return r.until >= 0.0f && r.until <= timeManager.currentTime; }), fogReveals.end());
	for (const FogReveal& r : fogReveals) {
		if (r.player != player->id)
			continue;
		NetPacketWriter msg{ NETCLIMSG_FOG_REVEAL };
		msg.writeUint32(r.object);
		msg.writeFloat(r.until < 0.0f ? 0.0f : r.until - timeManager.currentTime);
		msg.writeFloat(r.radius);
		sendToClient(clientIndex, msg);
	}
}
