// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#include "network.h"

bool NetLocalLink::available() {
	inMessages->mutex.lock();
	bool r = !inMessages->queue.empty();
	inMessages->mutex.unlock();
	return r;
}

NetPacket NetLocalLink::receive() {
	inMessages->mutex.lock();
	NetPacket p = inMessages->queue.front();
	inMessages->queue.pop();
	inMessages->mutex.unlock();
	return p;
}

void NetLocalLink::send(const NetPacket & packet) {
	outMessages->mutex.lock();
	outMessages->queue.push(packet);
	outMessages->mutex.unlock();
}

bool NetLocalLink::peekLast(uint8_t type, std::string& data) {
	struct Access : std::queue<NetPacket> { static const std::deque<NetPacket>& of(const std::queue<NetPacket>& q) { return q.*&Access::c; } };
	std::lock_guard<std::mutex> lock(inMessages->mutex);
	const auto& all = Access::of(inMessages->queue);
	for (auto it = all.rbegin(); it != all.rend(); ++it)
		if (!it->data.empty() && (uint8_t)it->data[0] == type) {
			data = it->data;
			return true;
		}
	return false;
}
