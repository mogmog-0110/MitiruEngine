#include <catch2/catch_test_macros.hpp>

#include "sgc/network/ITransport.hpp"

using namespace sgc::network;

TEST_CASE("LoopbackTransport - initial state", "[network][ITransport]")
{
	LoopbackTransport transport;
	REQUIRE_FALSE(transport.isListening());
	REQUIRE_FALSE(transport.isConnected());
}

TEST_CASE("LoopbackTransport - listen sets listening flag", "[network][ITransport]")
{
	LoopbackTransport transport;
	REQUIRE(transport.listen(8080));
	REQUIRE(transport.isListening());
}

TEST_CASE("LoopbackTransport - connect without peer fails", "[network][ITransport]")
{
	LoopbackTransport transport;
	REQUIRE_FALSE(transport.connect("localhost", 8080));
}

TEST_CASE("LoopbackTransport - link and connect", "[network][ITransport]")
{
	LoopbackTransport server;
	LoopbackTransport client;
	LoopbackTransport::link(server, client);

	server.listen(8080);
	REQUIRE(client.connect("localhost", 8080));
	REQUIRE(client.isConnected());

	// 両方に接続イベントが発生
	auto serverMsgs = server.poll();
	auto clientMsgs = client.poll();

	REQUIRE(serverMsgs.size() == 1);
	REQUIRE(serverMsgs[0].event == TransportEvent::Connected);

	REQUIRE(clientMsgs.size() == 1);
	REQUIRE(clientMsgs[0].event == TransportEvent::Connected);
}

TEST_CASE("LoopbackTransport - send and receive data", "[network][ITransport]")
{
	LoopbackTransport server;
	LoopbackTransport client;
	LoopbackTransport::link(server, client);

	server.listen(8080);
	client.connect("localhost", 8080);

	// 接続イベントを消化
	server.poll();
	client.poll();

	// クライアントからサーバーへ送信
	std::vector<uint8_t> data = {1, 2, 3, 4, 5};
	REQUIRE(client.send(1, data));

	auto messages = server.poll();
	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0].event == TransportEvent::DataReceived);
	REQUIRE(messages[0].connectionId == 1);
	REQUIRE(messages[0].data == data);
}

TEST_CASE("LoopbackTransport - bidirectional send", "[network][ITransport]")
{
	LoopbackTransport server;
	LoopbackTransport client;
	LoopbackTransport::link(server, client);

	server.listen(8080);
	client.connect("localhost", 8080);
	server.poll();
	client.poll();

	// サーバーからクライアントへ送信
	std::vector<uint8_t> serverData = {10, 20};
	REQUIRE(server.send(1, serverData));

	// クライアントからサーバーへ送信
	std::vector<uint8_t> clientData = {30, 40, 50};
	REQUIRE(client.send(1, clientData));

	auto clientMsgs = client.poll();
	REQUIRE(clientMsgs.size() == 1);
	REQUIRE(clientMsgs[0].data == serverData);

	auto serverMsgs = server.poll();
	REQUIRE(serverMsgs.size() == 1);
	REQUIRE(serverMsgs[0].data == clientData);
}

TEST_CASE("LoopbackTransport - disconnect generates events", "[network][ITransport]")
{
	LoopbackTransport server;
	LoopbackTransport client;
	LoopbackTransport::link(server, client);

	server.listen(8080);
	client.connect("localhost", 8080);
	server.poll();
	client.poll();

	client.disconnect(1);
	REQUIRE_FALSE(client.isConnected());

	auto clientMsgs = client.poll();
	REQUIRE(clientMsgs.size() == 1);
	REQUIRE(clientMsgs[0].event == TransportEvent::Disconnected);

	auto serverMsgs = server.poll();
	REQUIRE(serverMsgs.size() == 1);
	REQUIRE(serverMsgs[0].event == TransportEvent::Disconnected);
}

TEST_CASE("LoopbackTransport - shutdown clears state", "[network][ITransport]")
{
	LoopbackTransport server;
	LoopbackTransport client;
	LoopbackTransport::link(server, client);

	server.listen(8080);
	client.connect("localhost", 8080);

	server.shutdown();
	REQUIRE_FALSE(server.isListening());
	REQUIRE_FALSE(server.isConnected());

	// シャットダウン後のpollは空
	auto msgs = server.poll();
	REQUIRE(msgs.empty());
}

TEST_CASE("LoopbackTransport - poll returns empty when no messages", "[network][ITransport]")
{
	LoopbackTransport transport;
	auto msgs = transport.poll();
	REQUIRE(msgs.empty());
}

TEST_CASE("LoopbackTransport - send without peer fails", "[network][ITransport]")
{
	LoopbackTransport transport;
	std::vector<uint8_t> data = {1, 2, 3};
	REQUIRE_FALSE(transport.send(1, data));
}

TEST_CASE("LoopbackTransport - multiple messages accumulate", "[network][ITransport]")
{
	LoopbackTransport server;
	LoopbackTransport client;
	LoopbackTransport::link(server, client);

	server.listen(8080);
	client.connect("localhost", 8080);
	server.poll();
	client.poll();

	// 複数メッセージを送信
	std::vector<uint8_t> msg1 = {1};
	std::vector<uint8_t> msg2 = {2};
	std::vector<uint8_t> msg3 = {3};
	client.send(1, msg1);
	client.send(1, msg2);
	client.send(1, msg3);

	auto messages = server.poll();
	REQUIRE(messages.size() == 3);
	REQUIRE(messages[0].data == msg1);
	REQUIRE(messages[1].data == msg2);
	REQUIRE(messages[2].data == msg3);

	// 2回目のpollは空
	auto empty = server.poll();
	REQUIRE(empty.empty());
}

TEST_CASE("LoopbackTransport - ITransport interface usage", "[network][ITransport]")
{
	LoopbackTransport server;
	LoopbackTransport client;
	LoopbackTransport::link(server, client);

	ITransport& iServer = server;
	ITransport& iClient = client;

	REQUIRE(iServer.listen(9090));
	REQUIRE(iServer.isListening());
	REQUIRE(iClient.connect("localhost", 9090));

	// 接続イベントを消化
	iServer.poll();
	iClient.poll();

	std::vector<uint8_t> data = {42};
	REQUIRE(iClient.send(1, data));

	auto msgs = iServer.poll();
	REQUIRE(msgs.size() == 1);
	REQUIRE(msgs[0].data[0] == 42);

	iServer.shutdown();
	REQUIRE_FALSE(iServer.isListening());
}

TEST_CASE("LoopbackTransport - empty data send", "[network][ITransport]")
{
	LoopbackTransport server;
	LoopbackTransport client;
	LoopbackTransport::link(server, client);

	server.listen(8080);
	client.connect("localhost", 8080);
	server.poll();
	client.poll();

	std::vector<uint8_t> empty;
	REQUIRE(client.send(1, empty));

	auto msgs = server.poll();
	REQUIRE(msgs.size() == 1);
	REQUIRE(msgs[0].data.empty());
}
