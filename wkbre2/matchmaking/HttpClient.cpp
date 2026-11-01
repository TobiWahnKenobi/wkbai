// wkbai - matchmaking: HTTP(S) requests and WebSocket connections, for services with web APIs
// Licensed under the GNU General Public License 3

#include "HttpClient.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

namespace {
	std::wstring Wide(const std::string& s)
	{
		if (s.empty())
			return std::wstring();
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}

	std::string LastError(const char* what)
	{
		return std::string(what) + " failed (" + std::to_string(GetLastError()) + ")";
	}

	struct Url {
		std::wstring host, path;
		INTERNET_PORT port = 0;
		bool secure = false;
	};

	bool CrackUrl(const std::string& text, Url& url)
	{
		std::string t = text;
		// WinHTTP knows http(s) only; a WebSocket url is the same server
		if (t.rfind("ws://", 0) == 0) t = "http://" + t.substr(5);
		else if (t.rfind("wss://", 0) == 0) t = "https://" + t.substr(6);
		const std::wstring w = Wide(t);
		URL_COMPONENTS c = {};
		c.dwStructSize = sizeof(c);
		c.dwHostNameLength = (DWORD)-1;
		c.dwUrlPathLength = (DWORD)-1;
		c.dwExtraInfoLength = (DWORD)-1;
		if (!WinHttpCrackUrl(w.c_str(), (DWORD)w.size(), 0, &c))
			return false;
		url.host.assign(c.lpszHostName, c.dwHostNameLength);
		url.path.assign(c.lpszUrlPath, c.dwUrlPathLength);
		if (c.lpszExtraInfo)
			url.path.append(c.lpszExtraInfo, c.dwExtraInfoLength);
		if (url.path.empty())
			url.path = L"/";
		url.port = c.nPort;
		url.secure = c.nScheme == INTERNET_SCHEME_HTTPS;
		return true;
	}
}

HttpResult HttpRequest(const std::string& method, const std::string& urlText, const std::string& body, const HttpHeaders& headers, int timeoutMs)
{
	HttpResult r;
	Url url;
	if (!CrackUrl(urlText, url)) {
		r.error = "bad address " + urlText;
		return r;
	}
	HINTERNET session = WinHttpOpen(L"wkbai", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) {
		r.error = LastError("WinHttpOpen");
		return r;
	}
	WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
	HINTERNET connection = WinHttpConnect(session, url.host.c_str(), url.port, 0);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, Wide(method).c_str(), url.path.c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;
	if (!request)
		r.error = LastError("connecting");
	else {
		std::wstring head;
		for (const auto& [k, v] : headers)
			head += Wide(k) + L": " + Wide(v) + L"\r\n";
		if (!WinHttpSendRequest(request, head.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : head.c_str(), (DWORD)head.size(),
			(LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) || !WinHttpReceiveResponse(request, nullptr))
			r.error = LastError("the request");
		else {
			DWORD status = 0, size = sizeof(status);
			WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
			r.status = (int)status;
			for (;;) {
				DWORD available = 0;
				if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
					break;
				const size_t at = r.body.size();
				r.body.resize(at + available);
				DWORD read = 0;
				if (!WinHttpReadData(request, r.body.data() + at, available, &read)) {
					r.body.resize(at);
					break;
				}
				r.body.resize(at + read);
			}
		}
	}
	if (request) WinHttpCloseHandle(request);
	if (connection) WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return r;
}

bool WebSocketClient::open(const std::string& urlText, std::string& error)
{
	close();
	Url url;
	if (!CrackUrl(urlText, url)) {
		error = "bad address";
		return false;
	}
	session = WinHttpOpen(L"wkbai", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) {
		error = LastError("WinHttpOpen");
		return false;
	}
	// no receive timeout: the socket waits for messages as long as it is open
	WinHttpSetTimeouts(session, 10000, 10000, 10000, 0);
	connection = WinHttpConnect(session, url.host.c_str(), url.port, 0);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", url.path.c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, url.secure ? WINHTTP_FLAG_SECURE : 0) : nullptr;
	bool ok = request && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)
		&& WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)
		&& WinHttpReceiveResponse(request, nullptr);
	if (ok) {
		DWORD status = 0, size = sizeof(status);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
		if (status != 101) {
			error = "the server answered " + std::to_string(status);
			ok = false;
		}
		else
			socket = WinHttpWebSocketCompleteUpgrade(request, 0);
	}
	else
		error = LastError("the WebSocket");
	if (request)
		WinHttpCloseHandle(request);
	if (!socket) {
		if (error.empty())
			error = LastError("the WebSocket upgrade");
		close();
		return false;
	}
	return true;
}

bool WebSocketClient::send(const std::string& text)
{
	HINTERNET s = socket;
	return s && WinHttpWebSocketSend(s, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, (PVOID)text.data(), (DWORD)text.size()) == NO_ERROR;
}

bool WebSocketClient::receive(std::string& message)
{
	message.clear();
	char buffer[8192];
	for (;;) {
		HINTERNET s = socket;
		if (!s)
			return false;
		DWORD read = 0;
		WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
		if (WinHttpWebSocketReceive(s, buffer, sizeof(buffer), &read, &type) != NO_ERROR)
			return false;
		if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
			return false;
		message.append(buffer, read);
		if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
			return true;
	}
}

void WebSocketClient::close()
{
	if (socket) {
		HINTERNET s = socket;
		socket = nullptr;
		WinHttpWebSocketShutdown(s, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		WinHttpCloseHandle(s);
	}
	if (connection) WinHttpCloseHandle(connection);
	if (session) WinHttpCloseHandle(session);
	connection = session = nullptr;
}

#else

HttpResult HttpRequest(const std::string&, const std::string&, const std::string&, const HttpHeaders&, int)
{
	HttpResult r;
	r.error = "no HTTP client on this system yet";
	return r;
}
bool WebSocketClient::open(const std::string&, std::string& error) { error = "no WebSocket client on this system yet"; return false; }
bool WebSocketClient::send(const std::string&) { return false; }
bool WebSocketClient::receive(std::string&) { return false; }
void WebSocketClient::close() {}

#endif
