/* wkbai matchmaking plugin: Epic Online Services
 *
 * This file is under the MIT licence (like wkbai_mm_plugin.h, unlike the rest of wkbai), so
 * that it may be linked with Epic's closed EOS SDK:
 *
 *   Copyright (c) 2026 the wkbai contributors.
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this file,
 *   to deal in it without restriction, including without limitation the rights to use, copy,
 *   modify, merge, publish, distribute, sublicense, and/or sell copies of it, and to permit
 *   persons to whom it is furnished to do so. THE FILE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
 *   ANY KIND.
 *
 * What it uses of EOS (no Epic account or store needed by the players):
 *  - Connect with a device id (EOS_Connect_CreateDeviceId, EOS_Connect_Login with
 *    EOS_ECT_DEVICEID_ACCESS_TOKEN, EOS_Connect_CreateUser the first time);
 *  - a lobby per hosted game, public and searchable: attribute "wkbai" = the game version, the
 *    listing (JSON) in attributes "listing0".."listingN" of at most 900 bytes each, "listingParts";
 *  - P2P sockets ("wkbai"): NAT traversal and Epic's relay; the game's packets on channel 0
 *    (reliable, ordered), channel 1 for the guest's first packet that opens the connection.
 *
 * wkconfig.json "matchmaking": { "eos": { "productId", "sandboxId", "deploymentId", "clientId",
 *   "clientSecret" } }: the product's ids from Epic's Developer Portal (a product of its own,
 *   with a client of the "GameClient" policy that may use Connect, Lobbies and P2P).
 *
 * Built without the SDK (WKBAI_HAVE_EOS_SDK undefined) it is a stub that says so in the list.
 */

#include "wkbai_mm_plugin.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#ifdef WKBAI_HAVE_EOS_SDK
#include <eos_sdk.h>
#include <eos_logging.h>
#include <eos_connect.h>
#include <eos_lobby.h>
#include <eos_p2p.h>
#endif

using json = nlohmann::json;

struct wkbai_mm_service {
	const wkbai_mm_host* host = nullptr;
	json settings;
	int state = WKBAI_MM_OFF;
	std::string stateText;
	std::string listingsText = "[]";
	std::string addressText;

	void log(const std::string& line) { if (host && host->log) host->log(("EOS: " + line).c_str()); }

#ifdef WKBAI_HAVE_EOS_SDK
	EOS_HPlatform platform = nullptr;
	EOS_ProductUserId me = nullptr;
	EOS_P2P_SocketId socket = {};
	std::string playerName;

	// Host
	std::string lobbyId, listing;
	bool hosting = false, creating = false;
	EOS_NotificationId connectionRequest = EOS_INVALID_NOTIFICATIONID, connectionClosed = EOS_INVALID_NOTIFICATIONID,
		connectionEstablished = EOS_INVALID_NOTIFICATIONID;

	// Guest: the games found (the lobby owner by listing id)
	EOS_HLobbySearch search = nullptr;
	std::map<std::string, std::string> owners;

	// Connections, both sides: by id, the other side's product user id
	struct Connection {
		EOS_ProductUserId peer = nullptr;
		std::deque<std::string> packets;
		bool established = false, guestSide = false;
	};
	std::map<int64_t, Connection> connections;
	std::deque<std::pair<int, int64_t>> events;
	int64_t nextConnection = 1;

	int64_t connectionOf(EOS_ProductUserId peer)
	{
		for (auto& [id, c] : connections)
			if (c.peer == peer)
				return id;
		return 0;
	}
#endif
};

#ifdef WKBAI_HAVE_EOS_SDK

namespace {

std::string IdText(EOS_ProductUserId id)
{
	char buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
	int32_t length = sizeof(buffer);
	if (EOS_ProductUserId_ToString(id, buffer, &length) != EOS_Success)
		return std::string();
	return buffer;
}

void Login(wkbai_mm_service* s);

void CreateUser(wkbai_mm_service* s, EOS_ContinuanceToken token)
{
	EOS_Connect_CreateUserOptions o = {};
	o.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
	o.ContinuanceToken = token;
	EOS_Connect_CreateUser(EOS_Platform_GetConnectInterface(s->platform), &o, s, [](const EOS_Connect_CreateUserCallbackInfo* info) {
		auto* s = (wkbai_mm_service*)info->ClientData;
		if (info->ResultCode == EOS_EResult::EOS_Success) {
			s->me = info->LocalUserId;
			s->state = WKBAI_MM_READY;
			s->stateText.clear();
			s->log("signed in (new user " + IdText(s->me) + ")");
		}
		else {
			s->state = WKBAI_MM_FAILED;
			s->stateText = std::string("creating the user failed: ") + EOS_EResult_ToString(info->ResultCode);
		}
	});
}

void Login(wkbai_mm_service* s)
{
	EOS_Connect_Credentials credentials = {};
	credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
	credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
	credentials.Token = nullptr;
	EOS_Connect_UserLoginInfo user = {};
	user.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
	user.DisplayName = s->playerName.c_str();
	EOS_Connect_LoginOptions o = {};
	o.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
	o.Credentials = &credentials;
	o.UserLoginInfo = &user;
	EOS_Connect_Login(EOS_Platform_GetConnectInterface(s->platform), &o, s, [](const EOS_Connect_LoginCallbackInfo* info) {
		auto* s = (wkbai_mm_service*)info->ClientData;
		if (info->ResultCode == EOS_EResult::EOS_Success) {
			s->me = info->LocalUserId;
			s->state = WKBAI_MM_READY;
			s->stateText.clear();
			s->log("signed in as " + IdText(s->me));
		}
		else if (info->ResultCode == EOS_EResult::EOS_InvalidUser)
			CreateUser(s, info->ContinuanceToken); // the first time on this device
		else {
			s->state = WKBAI_MM_FAILED;
			s->stateText = std::string("sign-in failed: ") + EOS_EResult_ToString(info->ResultCode);
		}
	});
}

void Listen(wkbai_mm_service* s)
{
	EOS_HP2P p2p = EOS_Platform_GetP2PInterface(s->platform);
	if (s->connectionRequest == EOS_INVALID_NOTIFICATIONID) {
		EOS_P2P_AddNotifyPeerConnectionRequestOptions o = {};
		o.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
		o.LocalUserId = s->me;
		o.SocketId = &s->socket;
		s->connectionRequest = EOS_P2P_AddNotifyPeerConnectionRequest(p2p, &o, s, [](const EOS_P2P_OnIncomingConnectionRequestInfo* info) {
			auto* s = (wkbai_mm_service*)info->ClientData;
			const bool known = s->connectionOf(info->RemoteUserId) != 0;
			// a guest knocks: taken while the game is offered
			if (!s->hosting && !known)
				return;
			EOS_P2P_AcceptConnectionOptions a = {};
			a.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
			a.LocalUserId = s->me;
			a.RemoteUserId = info->RemoteUserId;
			a.SocketId = &s->socket;
			EOS_P2P_AcceptConnection(EOS_Platform_GetP2PInterface(s->platform), &a);
			if (!known) {
				const int64_t id = s->nextConnection++;
				s->connections[id].peer = info->RemoteUserId;
				s->connections[id].established = true;
				s->events.push_back({ WKBAI_MM_EVENT_CONNECTED, id });
			}
		});
	}
	if (s->connectionEstablished == EOS_INVALID_NOTIFICATIONID) {
		EOS_P2P_AddNotifyPeerConnectionEstablishedOptions o = {};
		o.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;
		o.LocalUserId = s->me;
		o.SocketId = &s->socket;
		s->connectionEstablished = EOS_P2P_AddNotifyPeerConnectionEstablished(p2p, &o, s, [](const EOS_P2P_OnPeerConnectionEstablishedInfo* info) {
			auto* s = (wkbai_mm_service*)info->ClientData;
			const int64_t id = s->connectionOf(info->RemoteUserId);
			auto it = s->connections.find(id);
			if (it != s->connections.end() && it->second.guestSide && !it->second.established) {
				it->second.established = true;
				s->events.push_back({ WKBAI_MM_EVENT_CONNECTED, id });
			}
		});
	}
	if (s->connectionClosed == EOS_INVALID_NOTIFICATIONID) {
		EOS_P2P_AddNotifyPeerConnectionClosedOptions o = {};
		o.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
		o.LocalUserId = s->me;
		o.SocketId = &s->socket;
		s->connectionClosed = EOS_P2P_AddNotifyPeerConnectionClosed(p2p, &o, s, [](const EOS_P2P_OnRemoteConnectionClosedInfo* info) {
			auto* s = (wkbai_mm_service*)info->ClientData;
			if (const int64_t id = s->connectionOf(info->RemoteUserId)) {
				s->connections.erase(id);
				s->events.push_back({ WKBAI_MM_EVENT_DISCONNECTED, id });
			}
		});
	}
}

void SetListing(wkbai_mm_service* s)
{
	EOS_HLobby lobby = EOS_Platform_GetLobbyInterface(s->platform);
	EOS_Lobby_UpdateLobbyModificationOptions m = {};
	m.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
	m.LocalUserId = s->me;
	m.LobbyId = s->lobbyId.c_str();
	EOS_HLobbyModification mod = nullptr;
	if (EOS_Lobby_UpdateLobbyModification(lobby, &m, &mod) != EOS_EResult::EOS_Success)
		return;
	auto add = [&](const std::string& key, const std::string& value) {
		EOS_Lobby_AttributeData a = {};
		a.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
		a.Key = key.c_str();
		a.Value.AsUtf8 = value.c_str();
		a.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
		EOS_LobbyModification_AddAttributeOptions o = {};
		o.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
		o.Attribute = &a;
		o.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
		EOS_LobbyModification_AddAttribute(mod, &o);
	};
	add("wkbai", s->host->game_version);
	const size_t PART = 900;
	const size_t parts = (s->listing.size() + PART - 1) / PART;
	for (size_t i = 0; i < parts; i++)
		add("listing" + std::to_string(i), s->listing.substr(i * PART, PART));
	add("listingParts", std::to_string(parts));
	EOS_Lobby_UpdateLobbyOptions u = {};
	u.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
	u.LobbyModificationHandle = mod;
	EOS_Lobby_UpdateLobby(lobby, &u, s, [](const EOS_Lobby_UpdateLobbyCallbackInfo* info) {
		if (info->ResultCode != EOS_EResult::EOS_Success)
			((wkbai_mm_service*)info->ClientData)->log(std::string("the listing wasn't updated: ") + EOS_EResult_ToString(info->ResultCode));
	});
	EOS_LobbyModification_Release(mod);
}

std::string Attribute(EOS_HLobbyDetails details, const std::string& key)
{
	EOS_LobbyDetails_CopyAttributeByKeyOptions o = {};
	o.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;
	o.AttrKey = key.c_str();
	EOS_Lobby_Attribute* a = nullptr;
	std::string value;
	if (EOS_LobbyDetails_CopyAttributeByKey(details, &o, &a) == EOS_EResult::EOS_Success && a) {
		if (a->Data && a->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING && a->Data->Value.AsUtf8)
			value = a->Data->Value.AsUtf8;
		EOS_Lobby_Attribute_Release(a);
	}
	return value;
}

}

#endif

namespace {

wkbai_mm_service* Create(const wkbai_mm_host* host)
{
	auto* s = new wkbai_mm_service;
	s->host = host;
	s->settings = json::parse(host->settings_json ? host->settings_json : "{}", nullptr, false);
	if (!s->settings.is_object())
		s->settings = json::object();
#ifdef WKBAI_HAVE_EOS_SDK
	s->playerName = host->player_name ? host->player_name : "Player";
	s->socket.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
	strncpy(s->socket.SocketName, "wkbai", sizeof(s->socket.SocketName) - 1);
	auto setting = [&](const char* key) { return s->settings.value(key, std::string()); };
	if (setting("productId").empty() || setting("sandboxId").empty() || setting("deploymentId").empty() || setting("clientId").empty() || setting("clientSecret").empty()) {
		s->stateText = "needs the product's ids from Epic's Developer Portal (wkconfig.json \"matchmaking\": { \"eos\": { \"productId\", \"sandboxId\", \"deploymentId\", \"clientId\", \"clientSecret\" } })";
		return s;
	}
	EOS_InitializeOptions init = {};
	init.ApiVersion = EOS_INITIALIZE_API_LATEST;
	init.ProductName = "wkbai";
	init.ProductVersion = host->game_version;
	const EOS_EResult r = EOS_Initialize(&init);
	if (r != EOS_EResult::EOS_Success && r != EOS_EResult::EOS_AlreadyConfigured) {
		s->state = WKBAI_MM_FAILED;
		s->stateText = std::string("the SDK didn't start: ") + EOS_EResult_ToString(r);
		return s;
	}
	static std::string productId, sandboxId, deploymentId, clientId, clientSecret, cache;
	productId = setting("productId");
	sandboxId = setting("sandboxId");
	deploymentId = setting("deploymentId");
	clientId = setting("clientId");
	clientSecret = setting("clientSecret");
	cache = std::string(host->user_path) + "/matchmaking/eos";
	EOS_Platform_Options o = {};
	o.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
	o.ProductId = productId.c_str();
	o.SandboxId = sandboxId.c_str();
	o.DeploymentId = deploymentId.c_str();
	o.ClientCredentials.ClientId = clientId.c_str();
	o.ClientCredentials.ClientSecret = clientSecret.c_str();
	o.CacheDirectory = cache.c_str();
	o.bIsServer = EOS_FALSE;
	o.Flags = EOS_PF_DISABLE_OVERLAY;
	s->platform = EOS_Platform_Create(&o);
	if (!s->platform) {
		s->state = WKBAI_MM_FAILED;
		s->stateText = "the platform wasn't created (are the product's ids right?)";
	}
#else
	s->stateText = "built without the Epic Online Services SDK (see docs/engine/matchmaking.md)";
#endif
	return s;
}

void Destroy(wkbai_mm_service* s)
{
#ifdef WKBAI_HAVE_EOS_SDK
	if (s->platform) {
		EOS_Platform_Release(s->platform);
		EOS_Shutdown();
	}
#endif
	delete s;
}

void Tick(wkbai_mm_service* s)
{
#ifdef WKBAI_HAVE_EOS_SDK
	if (!s->platform)
		return;
	EOS_Platform_Tick(s->platform);
	if (s->state != WKBAI_MM_READY)
		return;
	Listen(s);
	// the packets that came, to their connections
	EOS_HP2P p2p = EOS_Platform_GetP2PInterface(s->platform);
	for (int n = 0; n < 1000; n++) {
		EOS_P2P_GetNextReceivedPacketSizeOptions so = {};
		so.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
		so.LocalUserId = s->me;
		so.RequestedChannel = nullptr;
		uint32_t size = 0;
		if (EOS_P2P_GetNextReceivedPacketSize(p2p, &so, &size) != EOS_EResult::EOS_Success)
			break;
		std::string data(size, '\0');
		EOS_P2P_ReceivePacketOptions ro = {};
		ro.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
		ro.LocalUserId = s->me;
		ro.MaxDataSizeBytes = size;
		ro.RequestedChannel = nullptr;
		EOS_ProductUserId peer = nullptr;
		EOS_P2P_SocketId socket = {};
		uint8_t channel = 0;
		uint32_t written = 0;
		if (EOS_P2P_ReceivePacket(p2p, &ro, &peer, &socket, &channel, data.data(), &written) != EOS_EResult::EOS_Success)
			break;
		data.resize(written);
		if (channel != 0)
			continue; // the connection's opening packet
		if (const int64_t id = s->connectionOf(peer))
			s->connections[id].packets.push_back(std::move(data));
	}
#endif
}

int State(wkbai_mm_service* s, const char** text)
{
	if (text)
		*text = s->stateText.c_str();
	return s->state;
}

void SignIn(wkbai_mm_service* s)
{
#ifdef WKBAI_HAVE_EOS_SDK
	if (!s->platform || s->state == WKBAI_MM_SIGNING_IN || s->state == WKBAI_MM_READY)
		return;
	s->state = WKBAI_MM_SIGNING_IN;
	s->stateText = "signing in";
	EOS_Connect_CreateDeviceIdOptions o = {};
	o.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
	o.DeviceModel = "PC";
	EOS_Connect_CreateDeviceId(EOS_Platform_GetConnectInterface(s->platform), &o, s, [](const EOS_Connect_CreateDeviceIdCallbackInfo* info) {
		auto* s = (wkbai_mm_service*)info->ClientData;
		// this device has one already: fine
		if (info->ResultCode == EOS_EResult::EOS_Success || info->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed)
			Login(s);
		else {
			s->state = WKBAI_MM_FAILED;
			s->stateText = std::string("no device id: ") + EOS_EResult_ToString(info->ResultCode);
		}
	});
#endif
}

void Publish(wkbai_mm_service* s, const char* listing)
{
#ifdef WKBAI_HAVE_EOS_SDK
	if (s->state != WKBAI_MM_READY)
		return;
	s->listing = listing;
	s->hosting = true;
	if (!s->lobbyId.empty()) {
		SetListing(s);
		return;
	}
	if (s->creating)
		return;
	s->creating = true;
	const json l = json::parse(listing, nullptr, false);
	EOS_Lobby_CreateLobbyOptions o = {};
	o.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
	o.LocalUserId = s->me;
	o.MaxLobbyMembers = 16;
	o.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
	o.bPresenceEnabled = EOS_FALSE;
	o.bAllowInvites = EOS_FALSE;
	o.BucketId = s->host->game_version;
	EOS_Lobby_CreateLobby(EOS_Platform_GetLobbyInterface(s->platform), &o, s, [](const EOS_Lobby_CreateLobbyCallbackInfo* info) {
		auto* s = (wkbai_mm_service*)info->ClientData;
		s->creating = false;
		if (info->ResultCode != EOS_EResult::EOS_Success) {
			s->log(std::string("no lobby for the game: ") + EOS_EResult_ToString(info->ResultCode));
			return;
		}
		s->lobbyId = info->LobbyId;
		if (s->hosting)
			SetListing(s);
	});
#endif
}

void Withdraw(wkbai_mm_service* s)
{
#ifdef WKBAI_HAVE_EOS_SDK
	s->hosting = false;
	if (s->lobbyId.empty())
		return;
	EOS_Lobby_DestroyLobbyOptions o = {};
	o.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
	o.LocalUserId = s->me;
	o.LobbyId = s->lobbyId.c_str();
	EOS_Lobby_DestroyLobby(EOS_Platform_GetLobbyInterface(s->platform), &o, nullptr, [](const EOS_Lobby_DestroyLobbyCallbackInfo*) {});
	s->lobbyId.clear();
#endif
}

void Refresh(wkbai_mm_service* s, const char*)
{
#ifdef WKBAI_HAVE_EOS_SDK
	if (s->state != WKBAI_MM_READY || s->search)
		return;
	EOS_HLobby lobby = EOS_Platform_GetLobbyInterface(s->platform);
	EOS_Lobby_CreateLobbySearchOptions o = {};
	o.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
	o.MaxResults = 100;
	if (EOS_Lobby_CreateLobbySearch(lobby, &o, &s->search) != EOS_EResult::EOS_Success)
		return;
	EOS_Lobby_AttributeData version = {};
	version.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
	version.Key = "wkbai";
	version.Value.AsUtf8 = s->host->game_version;
	version.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
	EOS_LobbySearch_SetParameterOptions p = {};
	p.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
	p.Parameter = &version;
	p.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
	EOS_LobbySearch_SetParameter(s->search, &p);
	EOS_LobbySearch_FindOptions f = {};
	f.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
	f.LocalUserId = s->me;
	EOS_LobbySearch_Find(s->search, &f, s, [](const EOS_LobbySearch_FindCallbackInfo* info) {
		auto* s = (wkbai_mm_service*)info->ClientData;
		json list = json::array();
		s->owners.clear();
		if (info->ResultCode == EOS_EResult::EOS_Success) {
			EOS_LobbySearch_GetSearchResultCountOptions c = {};
			c.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
			const uint32_t count = EOS_LobbySearch_GetSearchResultCount(s->search, &c);
			for (uint32_t i = 0; i < count; i++) {
				EOS_LobbySearch_CopySearchResultByIndexOptions r = {};
				r.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
				r.LobbyIndex = i;
				EOS_HLobbyDetails details = nullptr;
				if (EOS_LobbySearch_CopySearchResultByIndex(s->search, &r, &details) != EOS_EResult::EOS_Success)
					continue;
				EOS_LobbyDetails_GetLobbyOwnerOptions oo = {};
				oo.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
				const EOS_ProductUserId owner = EOS_LobbyDetails_GetLobbyOwner(details, &oo);
				std::string text;
				const int parts = atoi(Attribute(details, "listingParts").c_str());
				for (int k = 0; k < parts; k++)
					text += Attribute(details, "listing" + std::to_string(k));
				EOS_LobbyDetails_Release(details);
				json l = json::parse(text, nullptr, false);
				if (!l.is_object() || owner == s->me)
					continue;
				const std::string id = IdText(owner);
				l["id"] = id;
				l["addresses"] = json::array(); // P2P: no address
				s->owners[id] = id;
				list.push_back(l);
			}
		}
		else
			s->log(std::string("the search failed: ") + EOS_EResult_ToString(info->ResultCode));
		s->listingsText = list.dump();
		EOS_LobbySearch_Release(s->search);
		s->search = nullptr;
	});
#endif
}

const char* Listings(wkbai_mm_service* s) { return s->listingsText.c_str(); }

int64_t Connect(wkbai_mm_service* s, const char* listingId, const char** enetAddress)
{
	if (enetAddress)
		*enetAddress = nullptr;
#ifdef WKBAI_HAVE_EOS_SDK
	if (s->state != WKBAI_MM_READY || !s->owners.count(listingId))
		return -1;
	const EOS_ProductUserId host = EOS_ProductUserId_FromString(listingId);
	if (!host)
		return -1;
	const int64_t id = s->nextConnection++;
	s->connections[id].peer = host;
	s->connections[id].guestSide = true;
	// this side takes the host's packets, and knocks (channel 1)
	EOS_HP2P p2p = EOS_Platform_GetP2PInterface(s->platform);
	EOS_P2P_AcceptConnectionOptions a = {};
	a.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
	a.LocalUserId = s->me;
	a.RemoteUserId = host;
	a.SocketId = &s->socket;
	EOS_P2P_AcceptConnection(p2p, &a);
	const char knock = 0;
	EOS_P2P_SendPacketOptions o = {};
	o.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
	o.LocalUserId = s->me;
	o.RemoteUserId = host;
	o.SocketId = &s->socket;
	o.Channel = 1;
	o.DataLengthBytes = 1;
	o.Data = &knock;
	o.bAllowDelayedDelivery = EOS_TRUE;
	o.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
	EOS_P2P_SendPacket(p2p, &o);
	return id;
#else
	(void)s;
	(void)listingId;
	return -1;
#endif
}

int PollEvent(wkbai_mm_service* s, int64_t* connection)
{
#ifdef WKBAI_HAVE_EOS_SDK
	if (s->events.empty())
		return WKBAI_MM_EVENT_NONE;
	const auto [event, id] = s->events.front();
	s->events.pop_front();
	*connection = id;
	return event;
#else
	(void)s;
	(void)connection;
	return WKBAI_MM_EVENT_NONE;
#endif
}

int Send(wkbai_mm_service* s, int64_t connection, const void* data, uint32_t size)
{
#ifdef WKBAI_HAVE_EOS_SDK
	auto it = s->connections.find(connection);
	if (it == s->connections.end())
		return 0;
	EOS_P2P_SendPacketOptions o = {};
	o.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
	o.LocalUserId = s->me;
	o.RemoteUserId = it->second.peer;
	o.SocketId = &s->socket;
	o.Channel = 0;
	o.DataLengthBytes = size;
	o.Data = data;
	o.bAllowDelayedDelivery = EOS_TRUE;
	o.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
	return EOS_P2P_SendPacket(EOS_Platform_GetP2PInterface(s->platform), &o) == EOS_EResult::EOS_Success;
#else
	(void)s; (void)connection; (void)data; (void)size;
	return 0;
#endif
}

uint32_t Receive(wkbai_mm_service* s, int64_t connection, void* buffer, uint32_t capacity)
{
#ifdef WKBAI_HAVE_EOS_SDK
	auto it = s->connections.find(connection);
	if (it == s->connections.end() || it->second.packets.empty())
		return 0;
	std::string& p = it->second.packets.front();
	const uint32_t size = (uint32_t)p.size();
	if (size <= capacity) {
		memcpy(buffer, p.data(), size);
		it->second.packets.pop_front();
	}
	return size;
#else
	(void)s; (void)connection; (void)buffer; (void)capacity;
	return 0;
#endif
}

void Close(wkbai_mm_service* s, int64_t connection, int)
{
#ifdef WKBAI_HAVE_EOS_SDK
	auto it = s->connections.find(connection);
	if (it == s->connections.end())
		return;
	EOS_P2P_CloseConnectionOptions o = {};
	o.ApiVersion = EOS_P2P_CLOSECONNECTION_API_LATEST;
	o.LocalUserId = s->me;
	o.RemoteUserId = it->second.peer;
	o.SocketId = &s->socket;
	EOS_P2P_CloseConnection(EOS_Platform_GetP2PInterface(s->platform), &o);
	s->connections.erase(it);
	s->events.push_back({ WKBAI_MM_EVENT_DISCONNECTED, connection });
#else
	(void)s; (void)connection;
#endif
}

const wkbai_mm_plugin g_plugin = {
	WKBAI_MM_API_VERSION, "eos", "Epic Online Services", "",
	Create, Destroy, Tick, State, SignIn, Publish, Withdraw, Refresh, Listings, Connect, PollEvent, Send, Receive, Close,
};

}

extern "C" WKBAI_MM_EXPORT const wkbai_mm_plugin* wkbai_mm_plugin_entry(uint32_t hostApiVersion)
{
	return hostApiVersion == WKBAI_MM_API_VERSION ? &g_plugin : nullptr;
}
