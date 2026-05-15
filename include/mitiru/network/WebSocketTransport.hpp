#pragma once

/// @file WebSocketTransport.hpp
/// @brief WebSocketトランスポート実装（RFC 6455）
/// @details 生TCPソケット上にWebSocketフレーミングを実装する。
///          バイナリモードでブラウザ互換のネットワーク通信を行う。

#include <mitiru/network/INetworkTransport.hpp>
#include <mitiru/network/SocketCompat.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace mitiru::network
{

/// @brief WebSocketオペコード（RFC 6455 Section 5.2）
enum class WsOpcode : std::uint8_t
{
	Continuation = 0x0, Text = 0x1, Binary = 0x2,
	Close = 0x8, Ping = 0x9, Pong = 0xA
};

/// @brief WebSocketフレームヘッダー解析結果
struct WsFrameHeader
{
	bool fin = false;
	WsOpcode opcode = WsOpcode::Binary;
	bool masked = false;
	std::uint64_t payloadLen = 0;
	std::array<std::uint8_t, 4> maskKey{};
	std::size_t headerSize = 0;
};

/// @brief WebSocketトランスポート実装
class WebSocketTransport final : public INetworkTransport
{
public:
	WebSocketTransport() = default;
	~WebSocketTransport() override { closeAll(); }

	WebSocketTransport(const WebSocketTransport&) = delete;
	WebSocketTransport& operator=(const WebSocketTransport&) = delete;
	WebSocketTransport(WebSocketTransport&&) = delete;
	WebSocketTransport& operator=(WebSocketTransport&&) = delete;

	bool listen(std::uint16_t port) override
	{
		if (!m_wsaGuard.isInitialized() || m_listenSocket != INVALID_SOCK)
			return false;

		m_listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSocket == INVALID_SOCK) return false;

		int optVal = 1;
		::setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR,
			reinterpret_cast<const char*>(&optVal), sizeof(optVal));

		if (!setNonBlocking(m_listenSocket))
		{ closeSocket(m_listenSocket); m_listenSocket = INVALID_SOCK; return false; }

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons(port);

		if (::bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr),
			sizeof(addr)) == SOCK_ERR ||
			::listen(m_listenSocket, SOMAXCONN) == SOCK_ERR)
		{ closeSocket(m_listenSocket); m_listenSocket = INVALID_SOCK; return false; }

		m_isServer = true;
		m_port = port;
		return true;
	}

	bool connect(std::string_view host, std::uint16_t port) override
	{
		if (!m_wsaGuard.isInitialized()) return false;

		SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCK) return false;

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		const std::string hostStr(host);
		if (inet_pton(AF_INET, hostStr.c_str(), &addr.sin_addr) != 1)
		{ closeSocket(sock); return false; }

		if (::connect(sock, reinterpret_cast<sockaddr*>(&addr),
			sizeof(addr)) == SOCK_ERR)
		{ closeSocket(sock); return false; }

		if (!sendClientHandshake(sock, hostStr, port) ||
			!receiveHandshakeResponse(sock))
		{ closeSocket(sock); return false; }

		if (!setNonBlocking(sock)) { closeSocket(sock); return false; }

		const ConnectionId connId = ++m_nextConnectionId;
		std::scoped_lock lock(m_mutex);
		m_connections.push_back({connId, sock, {}, true});
		enqueueEvent(connId, NetworkEvent::Connected);
		return true;
	}

	void disconnect(ConnectionId id) override
	{
		std::scoped_lock lock(m_mutex);
		for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
			if (it->connectionId == id) {
				sendRaw(it->socket, buildFrame(WsOpcode::Close, {}, !m_isServer));
				closeSocket(it->socket);
				enqueueEvent(id, NetworkEvent::Disconnected);
				m_connections.erase(it); return;
			}
		}
	}

	void send(ConnectionId id, const std::vector<std::uint8_t>& data) override
	{
		std::scoped_lock lock(m_mutex);
		for (const auto& c : m_connections) {
			if (c.connectionId == id) {
				sendRaw(c.socket, buildFrame(WsOpcode::Binary, data, !m_isServer));
				m_stats.bytesSent += data.size(); m_stats.packetsSent++; return;
			}
		}
	}

	[[nodiscard]] std::vector<NetworkMessage> poll() override
	{
		acceptNewConnections(); receiveData(); sendKeepalive();
		std::scoped_lock lock(m_mutex);
		std::vector<NetworkMessage> msgs;
		while (!m_incoming.empty()) { msgs.push_back(std::move(m_incoming.front())); m_incoming.pop(); }
		return msgs;
	}

	[[nodiscard]] bool isConnected(ConnectionId id) const override
	{ std::scoped_lock l(m_mutex); for (const auto& c : m_connections) if (c.connectionId == id) return true; return false; }

	[[nodiscard]] NetworkStats stats() const override { std::scoped_lock l(m_mutex); return m_stats; }

	[[nodiscard]] uint16_t getLocalPort() const noexcept
	{
		if (m_listenSocket == INVALID_SOCK) return 0;
		sockaddr_in a{}; auto al = static_cast<
#ifdef _WIN32
			int
#else
			socklen_t
#endif
		>(sizeof(a));
		if (::getsockname(m_listenSocket, reinterpret_cast<sockaddr*>(&a), &al) == 0)
			return ntohs(a.sin_port);
		return m_port;
	}

private:
	struct WsConnection
	{
		ConnectionId connectionId{INVALID_CONNECTION};
		SocketHandle socket{INVALID_SOCK};
		std::vector<uint8_t> recvBuffer;
		bool handshakeComplete = false;
	};

	static constexpr int kPingIntervalSeconds = 30;

	void enqueueEvent(ConnectionId id, NetworkEvent evt)
	{
		NetworkMessage msg;
		msg.sender = id;
		msg.header.type = static_cast<uint32_t>(evt);
		m_incoming.push(std::move(msg));
	}

	[[nodiscard]] static std::vector<uint8_t> buildFrame(
		WsOpcode opcode, const std::vector<uint8_t>& payload, bool mask)
	{
		std::vector<uint8_t> f;
		f.push_back(0x80 | static_cast<uint8_t>(opcode));
		const auto len = payload.size();
		uint8_t mb = mask ? 0x80 : 0x00;
		if (len < 126) f.push_back(mb | static_cast<uint8_t>(len));
		else if (len <= 0xFFFF)
		{ f.push_back(mb | 126);
		  f.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
		  f.push_back(static_cast<uint8_t>(len & 0xFF)); }
		else
		{ f.push_back(mb | 127);
		  for (int i = 7; i >= 0; --i)
			f.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF)); }
		if (mask)
		{ auto mk = generateMaskKey();
		  f.insert(f.end(), mk.begin(), mk.end());
		  for (std::size_t i = 0; i < payload.size(); ++i)
			f.push_back(payload[i] ^ mk[i % 4]); }
		else f.insert(f.end(), payload.begin(), payload.end());
		return f;
	}

	[[nodiscard]] static std::array<uint8_t, 4> generateMaskKey()
	{
		static thread_local std::mt19937 rng(static_cast<unsigned>(
			std::chrono::steady_clock::now().time_since_epoch().count()));
		std::uniform_int_distribution<int> dist(0, 255);
		return {static_cast<uint8_t>(dist(rng)), static_cast<uint8_t>(dist(rng)),
			static_cast<uint8_t>(dist(rng)), static_cast<uint8_t>(dist(rng))};
	}

	[[nodiscard]] static bool parseFrameHeader(
		const std::vector<uint8_t>& d, WsFrameHeader& h)
	{
		if (d.size() < 2) return false;
		h.fin = (d[0] & 0x80) != 0;
		h.opcode = static_cast<WsOpcode>(d[0] & 0x0F);
		h.masked = (d[1] & 0x80) != 0;
		std::size_t off = 2;
		uint8_t lb = d[1] & 0x7F;
		if (lb < 126) { h.payloadLen = lb; }
		else if (lb == 126)
		{ if (d.size() < 4) return false;
		  h.payloadLen = (static_cast<uint64_t>(d[2]) << 8) | d[3]; off = 4; }
		else
		{ if (d.size() < 10) return false;
		  h.payloadLen = 0;
		  for (int i = 0; i < 8; ++i) h.payloadLen = (h.payloadLen << 8) | d[2+i];
		  off = 10; }
		if (h.masked)
		{ if (d.size() < off + 4) return false;
		  std::copy_n(d.begin() + off, 4, h.maskKey.begin()); off += 4; }
		h.headerSize = off;
		return d.size() >= off + h.payloadLen;
	}

	[[nodiscard]] bool sendClientHandshake(
		SocketHandle sock, const std::string& host, uint16_t port) const
	{
		std::string req = "GET / HTTP/1.1\r\nHost: " + host + ":"
			+ std::to_string(port) + "\r\nUpgrade: websocket\r\n"
			"Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
			"Sec-WebSocket-Version: 13\r\n\r\n";
		return ::send(sock, req.c_str(), static_cast<int>(req.size()), 0)
			== static_cast<int>(req.size());
	}

	[[nodiscard]] static bool receiveHandshakeResponse(SocketHandle sock)
	{
		char buf[1024];
		int n = ::recv(sock, buf, sizeof(buf) - 1, 0);
		if (n <= 0) return false;
		buf[n] = '\0';
		return std::string(buf).find("101") != std::string::npos;
	}

	[[nodiscard]] static bool handleServerHandshake(SocketHandle sock)
	{
		char buf[2048];
		int n = ::recv(sock, buf, sizeof(buf) - 1, 0);
		if (n <= 0) return false;
		buf[n] = '\0';
		std::string req(buf);
		if (req.find("Upgrade: websocket") == std::string::npos &&
			req.find("Upgrade: WebSocket") == std::string::npos)
			return false;
		std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\nConnection: Upgrade\r\n"
			"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
		return ::send(sock, resp.c_str(), static_cast<int>(resp.size()), 0)
			== static_cast<int>(resp.size());
	}

	void acceptNewConnections()
	{
		if (m_listenSocket == INVALID_SOCK) return;
		while (true) {
			sockaddr_in ca{};
#ifdef _WIN32
			int al = sizeof(ca);
#else
			socklen_t al = sizeof(ca);
#endif
			SocketHandle cs = ::accept(m_listenSocket,
				reinterpret_cast<sockaddr*>(&ca), &al);
			if (cs == INVALID_SOCK) break;
			if (!handleServerHandshake(cs)) { closeSocket(cs); continue; }
			(void)setNonBlocking(cs);
			const ConnectionId cid = ++m_nextConnectionId;
			std::scoped_lock lock(m_mutex);
			m_connections.push_back({cid, cs, {}, true});
			enqueueEvent(cid, NetworkEvent::Connected);
		}
	}

	void receiveData()
	{
		std::scoped_lock lock(m_mutex);
		for (auto it = m_connections.begin(); it != m_connections.end(); )
		{
			char tmp[4096];
			const int n = ::recv(it->socket, tmp, sizeof(tmp), 0);
			if (n > 0) {
				it->recvBuffer.insert(it->recvBuffer.end(), tmp, tmp + n);
				if (!processWsFrames(*it))
				{ closeSocket(it->socket); it = m_connections.erase(it); continue; }
				++it;
			} else if (n == 0) {
				enqueueEvent(it->connectionId, NetworkEvent::Disconnected);
				closeSocket(it->socket); it = m_connections.erase(it);
			} else if (isWouldBlock(lastSocketError())) { ++it; }
			else {
				enqueueEvent(it->connectionId, NetworkEvent::Error);
				closeSocket(it->socket); it = m_connections.erase(it);
			}
		}
	}

	[[nodiscard]] bool processWsFrames(WsConnection& c)
	{
		while (true)
		{
			WsFrameHeader h;
			if (!parseFrameHeader(c.recvBuffer, h)) break;
			const auto tot = h.headerSize + h.payloadLen;
			std::vector<uint8_t> pl(c.recvBuffer.begin() + h.headerSize,
				c.recvBuffer.begin() + tot);
			if (h.masked)
				for (std::size_t i = 0; i < pl.size(); ++i) pl[i] ^= h.maskKey[i%4];
			c.recvBuffer.erase(c.recvBuffer.begin(), c.recvBuffer.begin() + tot);
			switch (h.opcode) {
			case WsOpcode::Binary: case WsOpcode::Text: {
				NetworkMessage m; m.sender = c.connectionId;
				m.header.size = static_cast<uint32_t>(pl.size());
				m.payload = std::move(pl); m_incoming.push(std::move(m));
				m_stats.bytesReceived += tot; m_stats.packetsReceived++; break; }
			case WsOpcode::Ping:
				sendRaw(c.socket, buildFrame(WsOpcode::Pong, pl, !m_isServer)); break;
			case WsOpcode::Pong: break;
			case WsOpcode::Close:
				sendRaw(c.socket, buildFrame(WsOpcode::Close, {}, !m_isServer));
				enqueueEvent(c.connectionId, NetworkEvent::Disconnected); return false;
			default: break; }
		}
		return true;
	}

	void sendKeepalive()
	{
		auto now = std::chrono::steady_clock::now();
		if (now - m_lastPingTime < std::chrono::seconds(kPingIntervalSeconds)) return;
		m_lastPingTime = now;
		std::scoped_lock lock(m_mutex);
		for (const auto& c : m_connections)
			sendRaw(c.socket, buildFrame(WsOpcode::Ping, {}, !m_isServer));
	}

	static void sendRaw(SocketHandle sock, const std::vector<uint8_t>& d)
	{
		int sent = 0; const int sz = static_cast<int>(d.size());
		while (sent < sz) {
			int s = ::send(sock, reinterpret_cast<const char*>(d.data()+sent), sz-sent, 0);
			if (s == SOCK_ERR) break; sent += s;
		}
	}

	void closeAll()
	{
		std::scoped_lock lock(m_mutex);
		for (auto& c : m_connections) closeSocket(c.socket);
		m_connections.clear();
		if (m_listenSocket != INVALID_SOCK) { closeSocket(m_listenSocket); m_listenSocket = INVALID_SOCK; }
		while (!m_incoming.empty()) m_incoming.pop();
	}

	WsaGuard m_wsaGuard;
	SocketHandle m_listenSocket{INVALID_SOCK};
	std::vector<WsConnection> m_connections;
	std::queue<NetworkMessage> m_incoming;
	mutable std::mutex m_mutex;
	std::atomic<uint32_t> m_nextConnectionId{0};
	uint16_t m_port{0};
	bool m_isServer{false};
	NetworkStats m_stats;
	std::chrono::steady_clock::time_point m_lastPingTime{
		std::chrono::steady_clock::now()};
};

} // namespace mitiru::network
