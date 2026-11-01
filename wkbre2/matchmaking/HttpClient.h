// wkbai - matchmaking: HTTP(S) requests and WebSocket connections, for services with web APIs
// Licensed under the GNU General Public License 3
//
// On Windows through WinHTTP (TLS and proxies as the system has them); blocking, so call them
// from a worker thread.

#pragma once

#include <string>
#include <utility>
#include <vector>

struct HttpResult {
	int status = 0;     // 0: no answer (error says why)
	std::string body, error;
};
using HttpHeaders = std::vector<std::pair<std::string, std::string>>;
HttpResult HttpRequest(const std::string& method, const std::string& url, const std::string& body, const HttpHeaders& headers, int timeoutMs = 10000);

// A WebSocket client: one thread may send while another receives
struct WebSocketClient {
	// "ws://", "wss://" (or "http://", "https://") url
	bool open(const std::string& url, std::string& error);
	bool send(const std::string& text);
	// Waits for the next text message; false once closed
	bool receive(std::string& message);
	void close();
	bool isOpen() const { return socket != nullptr; }
	~WebSocketClient() { close(); }
private:
	void* session = nullptr, * connection = nullptr, * socket = nullptr;
};
