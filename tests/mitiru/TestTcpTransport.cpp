/// @file TestTcpTransport.cpp
/// @brief TcpTransportのユニットテスト

#include <catch2/catch_test_macros.hpp>

#ifdef _WIN32

#include <mitiru/network/TcpTransport.hpp>

#include <chrono>
#include <thread>
#include <vector>

using namespace mitiru::network;

/// @brief ポーリングで指定イベントのメッセージを待つヘルパー
/// @param transport 対象トランスポート
/// @param eventType 待つイベント種別
/// @param timeoutMs タイムアウト（ミリ秒）
/// @return 受信メッセージ（タイムアウト時は空）
static std::vector<NetworkMessage> pollUntilEvent(
	TcpTransport& transport,
	NetworkEvent eventType,
	int timeoutMs = 2000)
{
	std::vector<NetworkMessage> result;
	const auto start = std::chrono::steady_clock::now();
	const auto target = static_cast<uint32_t>(eventType);

	while (true)
	{
		auto msgs = transport.poll();
		for (auto& m : msgs)
		{
			result.push_back(std::move(m));
		}

		for (const auto& r : result)
		{
			if (r.header.type == target) return result;
		}

		const auto elapsed = std::chrono::duration_cast<
			std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start).count();
		if (elapsed > timeoutMs) break;

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return result;
}

/// @brief ポーリングでデータ受信メッセージを待つヘルパー
static std::vector<NetworkMessage> pollUntilData(
	TcpTransport& transport,
	int timeoutMs = 2000)
{
	return pollUntilEvent(
		transport, NetworkEvent::DataReceived, timeoutMs);
}

TEST_CASE("TcpTransport - WSA initialization", "[network][tcp]")
{
	TcpTransport transport;
	REQUIRE(transport.isWsaInitialized());
}

TEST_CASE("TcpTransport - initial state", "[network][tcp]")
{
	TcpTransport transport;
	REQUIRE_FALSE(transport.isListening());
}

TEST_CASE("TcpTransport - listen on ephemeral port", "[network][tcp]")
{
	TcpTransport server;
	REQUIRE(server.listen(0));
	REQUIRE(server.isListening());
	REQUIRE(server.getLocalPort() != 0);
}

TEST_CASE("TcpTransport - client connects to server", "[network][tcp]")
{
	TcpTransport server;
	REQUIRE(server.listen(0));
	const uint16_t port = server.getLocalPort();

	TcpTransport client;
	REQUIRE(client.connect("127.0.0.1", port));

	/// サーバー側でConnectedイベントを待つ
	auto serverMsgs = pollUntilEvent(
		server, NetworkEvent::Connected);
	bool hasConnected = false;
	for (const auto& m : serverMsgs)
	{
		if (m.header.type ==
			static_cast<uint32_t>(NetworkEvent::Connected))
		{
			hasConnected = true;
			break;
		}
	}
	REQUIRE(hasConnected);
}

TEST_CASE("TcpTransport - send and receive roundtrip", "[network][tcp]")
{
	TcpTransport server;
	REQUIRE(server.listen(0));
	const uint16_t port = server.getLocalPort();

	TcpTransport client;
	REQUIRE(client.connect("127.0.0.1", port));

	/// クライアント側のConnectedイベントからconnectionIdを取得
	auto clientMsgs = pollUntilEvent(
		client, NetworkEvent::Connected);
	REQUIRE_FALSE(clientMsgs.empty());
	ConnectionId clientConnId = clientMsgs[0].sender;

	/// サーバー側のConnectedイベントからconnectionIdを取得
	auto serverMsgs = pollUntilEvent(
		server, NetworkEvent::Connected);
	REQUIRE_FALSE(serverMsgs.empty());
	ConnectionId serverConnId = INVALID_CONNECTION;
	for (const auto& m : serverMsgs)
	{
		if (m.header.type ==
			static_cast<uint32_t>(NetworkEvent::Connected))
		{
			serverConnId = m.sender;
			break;
		}
	}
	REQUIRE(serverConnId != INVALID_CONNECTION);

	/// クライアントからサーバーへデータ送信
	std::vector<uint8_t> payload = {10, 20, 30, 40, 50};
	client.send(clientConnId, payload);

	/// サーバーでデータ受信を待つ
	auto recvMsgs = pollUntilData(server);
	bool dataReceived = false;
	for (const auto& m : recvMsgs)
	{
		if (m.payload == payload)
		{
			dataReceived = true;
			break;
		}
	}
	REQUIRE(dataReceived);
}

TEST_CASE("TcpTransport - server sends to client", "[network][tcp]")
{
	TcpTransport server;
	REQUIRE(server.listen(0));
	const uint16_t port = server.getLocalPort();

	TcpTransport client;
	REQUIRE(client.connect("127.0.0.1", port));

	/// サーバー側で接続IDを取得
	auto serverMsgs = pollUntilEvent(
		server, NetworkEvent::Connected);
	ConnectionId serverConnId = INVALID_CONNECTION;
	for (const auto& m : serverMsgs)
	{
		if (m.header.type ==
			static_cast<uint32_t>(NetworkEvent::Connected))
		{
			serverConnId = m.sender;
			break;
		}
	}
	REQUIRE(serverConnId != INVALID_CONNECTION);

	/// クライアント側のConnectedイベントを消化
	pollUntilEvent(client, NetworkEvent::Connected);

	/// サーバーからクライアントへデータ送信
	std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
	server.send(serverConnId, payload);

	/// クライアントでデータ受信を待つ
	auto recvMsgs = pollUntilData(client);
	bool dataReceived = false;
	for (const auto& m : recvMsgs)
	{
		if (m.payload == payload)
		{
			dataReceived = true;
			break;
		}
	}
	REQUIRE(dataReceived);
}

TEST_CASE("TcpTransport - isConnected returns true for active connection", "[network][tcp]")
{
	TcpTransport server;
	REQUIRE(server.listen(0));
	const uint16_t port = server.getLocalPort();

	TcpTransport client;
	REQUIRE(client.connect("127.0.0.1", port));

	/// クライアント側のconnectionId取得
	auto clientMsgs = pollUntilEvent(
		client, NetworkEvent::Connected);
	REQUIRE_FALSE(clientMsgs.empty());
	ConnectionId connId = clientMsgs[0].sender;

	REQUIRE(client.isConnected(connId));
}

TEST_CASE("TcpTransport - stats track bytes", "[network][tcp]")
{
	TcpTransport server;
	REQUIRE(server.listen(0));
	const uint16_t port = server.getLocalPort();

	TcpTransport client;
	REQUIRE(client.connect("127.0.0.1", port));

	auto clientMsgs = pollUntilEvent(
		client, NetworkEvent::Connected);
	ConnectionId connId = clientMsgs[0].sender;

	std::vector<uint8_t> payload = {1, 2, 3};
	client.send(connId, payload);

	auto s = client.stats();
	REQUIRE(s.packetsSent == 1);
	REQUIRE(s.bytesSent > 0);
}

TEST_CASE("TcpTransport - double listen fails", "[network][tcp]")
{
	TcpTransport server;
	REQUIRE(server.listen(0));
	REQUIRE_FALSE(server.listen(0));
}

TEST_CASE("TcpTransport - connect to invalid address fails", "[network][tcp]")
{
	TcpTransport client;
	REQUIRE_FALSE(client.connect("127.0.0.1", 1));
}

#else

TEST_CASE("TcpTransport - skipped on non-Windows", "[network][tcp]")
{
	REQUIRE(true);
}

#endif // _WIN32
