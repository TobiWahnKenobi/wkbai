// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "netenetlink.h"
#include <enet/enet.h>

bool NetEnetLink::available() {
	return !packets.empty();
}

NetPacket NetEnetLink::receive() {
	ENetPacket *epacket = (ENetPacket*)packets.front();
	packets.pop();
	NetPacket netpacket = NetPacket(std::string((char*)epacket->data, epacket->dataLength));
	enet_packet_destroy(epacket);
	return netpacket;
}

void NetEnetLink::send(const NetPacket & packet) {
	ENetPacket *epacket = enet_packet_create(packet.data.c_str(), packet.data.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send((ENetPeer*)peer, 0, epacket);
}

void NetEnetLink::disconnect(bool afterSending) {
	if (afterSending)
		enet_peer_disconnect_later((ENetPeer*)peer, 0);
	else
		enet_peer_disconnect((ENetPeer*)peer, 0);
}

bool NetEnetLink::peekLast(uint8_t type, std::string& data) {
	// (the queue's container: std::queue keeps it protected)
	struct Access : std::queue<_ENetPacket*> { static const std::deque<_ENetPacket*>& of(const std::queue<_ENetPacket*>& q) { return q.*&Access::c; } };
	const auto& all = Access::of(packets);
	for (auto it = all.rbegin(); it != all.rend(); ++it) {
		const ENetPacket* p = (const ENetPacket*)*it;
		if (p->dataLength > 0 && p->data[0] == type) {
			data.assign((const char*)p->data, p->dataLength);
			return true;
		}
	}
	return false;
}
