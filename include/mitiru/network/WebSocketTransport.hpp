#pragma once

/// @file WebSocketTransport.hpp
/// @brief WebSocket (RFC 6455) の INetworkTransport (cpp-httplib、実装は src/websocket_transport_impl.cpp)
/// @details 待ち受けは 127.0.0.1 だけで TLS は使わない。送受信は接続ごとのスレッドが行い、
///          届いたものは poll() で取り出す。

#include <mitiru/network/INetworkTransport.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace mitiru::network
{

class WebSocketTransport final : public INetworkTransport
{
public:
	WebSocketTransport();
	~WebSocketTransport() override;

	WebSocketTransport(const WebSocketTransport&) = delete;
	WebSocketTransport& operator=(const WebSocketTransport&) = delete;
	WebSocketTransport(WebSocketTransport&&) = delete;
	WebSocketTransport& operator=(WebSocketTransport&&) = delete;

	/// @param port 0 なら空いているポートを使う (実際の番号は getLocalPort() で読む)
	bool listen(std::uint16_t port) override;
	bool connect(std::string_view host, std::uint16_t port) override;
	void disconnect(ConnectionId id) override;
	void send(ConnectionId id, const std::vector<std::uint8_t>& data) override;
	[[nodiscard]] std::vector<NetworkMessage> poll() override;
	[[nodiscard]] bool isConnected(ConnectionId id) const override;
	[[nodiscard]] NetworkStats stats() const override;
	[[nodiscard]] std::uint16_t getLocalPort() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace mitiru::network
