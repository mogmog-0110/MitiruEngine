#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/network/NetworkTypes.hpp>
#include <mitiru/network/INetworkTransport.hpp>
#include <mitiru/network/LocalTransport.hpp>
#include <mitiru/network/Lobby.hpp>
#include <mitiru/network/StateSync.hpp>

using namespace mitiru::network;

// --- NetworkTypes ---

TEST_CASE("NetworkMessage default construction", "[mitiru][network]")
{
	NetworkMessage msg;
	REQUIRE(msg.sender == INVALID_CONNECTION);
	REQUIRE(msg.header.size == 0);
	REQUIRE(msg.header.type == 0);
	REQUIRE(msg.header.sequence == 0);
	REQUIRE(msg.payload.empty());
}

TEST_CASE("PacketHeader defaults to zero", "[mitiru][network]")
{
	PacketHeader hdr;
	REQUIRE(hdr.size == 0);
	REQUIRE(hdr.type == 0);
	REQUIRE(hdr.sequence == 0);
}

TEST_CASE("NetworkStats defaults to zero", "[mitiru][network]")
{
	NetworkStats st;
	REQUIRE(st.bytesSent == 0);
	REQUIRE(st.bytesReceived == 0);
	REQUIRE(st.packetsSent == 0);
	REQUIRE(st.packetsReceived == 0);
	REQUIRE(st.latencyMs == Catch::Approx(0.0f));
	REQUIRE(st.packetLoss == Catch::Approx(0.0f));
}

// --- Lobby ---

TEST_CASE("Lobby add and remove players", "[mitiru][network]")
{
	Lobby lobby;
	REQUIRE(lobby.playerCount() == 0);

	lobby.addPlayer(1, "Alice");
	lobby.addPlayer(2, "Bob");
	REQUIRE(lobby.playerCount() == 2);

	lobby.removePlayer(1);
	REQUIRE(lobby.playerCount() == 1);
	REQUIRE(lobby.players()[0].name == "Bob");
}

TEST_CASE("Lobby allReady returns false when empty", "[mitiru][network]")
{
	Lobby lobby;
	REQUIRE_FALSE(lobby.allReady());
}

TEST_CASE("Lobby allReady logic", "[mitiru][network]")
{
	Lobby lobby;
	lobby.addPlayer(1, "Alice");
	lobby.addPlayer(2, "Bob");

	REQUIRE_FALSE(lobby.allReady());

	lobby.setReady(1, true);
	REQUIRE_FALSE(lobby.allReady());

	lobby.setReady(2, true);
	REQUIRE(lobby.allReady());

	lobby.setReady(1, false);
	REQUIRE_FALSE(lobby.allReady());
}

TEST_CASE("Lobby toJson produces valid JSON", "[mitiru][network]")
{
	Lobby lobby;
	lobby.addPlayer(1, "Alice");
	lobby.setReady(1, true);

	const auto json = lobby.toJson();
	REQUIRE(json.find("\"players\"") != std::string::npos);
	REQUIRE(json.find("\"Alice\"") != std::string::npos);
	REQUIRE(json.find("\"ready\":true") != std::string::npos);
}

// --- LocalTransport ---

TEST_CASE("LocalTransport paired send and receive", "[mitiru][network]")
{
	LocalTransport server;
	LocalTransport client;
	LocalTransport::pair(server, client);

	REQUIRE(server.isConnected(1));
	REQUIRE(client.isConnected(1));

	std::vector<std::uint8_t> data = {0x01, 0x02, 0x03};
	server.send(1, data);

	auto messages = client.poll();
	REQUIRE(messages.size() == 1);
	REQUIRE(messages[0].payload == data);
	REQUIRE(messages[0].sender == 1);
}

TEST_CASE("LocalTransport disconnect", "[mitiru][network]")
{
	LocalTransport a;
	LocalTransport b;
	LocalTransport::pair(a, b);

	REQUIRE(a.isConnected(1));
	REQUIRE(b.isConnected(1));

	a.disconnect(1);
	REQUIRE_FALSE(a.isConnected(1));
	REQUIRE_FALSE(b.isConnected(1));
}

TEST_CASE("LocalTransport send after disconnect is ignored", "[mitiru][network]")
{
	LocalTransport a;
	LocalTransport b;
	LocalTransport::pair(a, b);
	a.disconnect(1);

	std::vector<std::uint8_t> data = {0xFF};
	a.send(1, data);

	auto messages = b.poll();
	REQUIRE(messages.empty());
}

TEST_CASE("LocalTransport stats track sent bytes", "[mitiru][network]")
{
	LocalTransport a;
	LocalTransport b;
	LocalTransport::pair(a, b);

	std::vector<std::uint8_t> data = {0x01, 0x02, 0x03, 0x04};
	a.send(1, data);

	auto st = a.stats();
	REQUIRE(st.packetsSent == 1);
	REQUIRE(st.bytesSent == 4);
}

// --- StateSync ---

TEST_CASE("StateSync push and get state", "[mitiru][network]")
{
	StateSync sync;
	REQUIRE(sync.latestState().empty());

	sync.pushState(R"({"hp":100})");
	REQUIRE(sync.latestState() == R"({"hp":100})");
}

TEST_CASE("StateSync version tracking", "[mitiru][network]")
{
	StateSync sync;
	REQUIRE(sync.stateVersion() == 0);

	sync.pushState("state1");
	REQUIRE(sync.stateVersion() == 1);

	sync.pushState("state2");
	REQUIRE(sync.stateVersion() == 2);
}

TEST_CASE("StateSync acknowledge removes old states", "[mitiru][network]")
{
	StateSync sync;
	sync.pushState("s1");
	sync.pushState("s2");
	sync.pushState("s3");
	REQUIRE(sync.pendingCount() == 3);

	sync.acknowledgeVersion(2);
	REQUIRE(sync.pendingCount() == 1);
	REQUIRE(sync.latestState() == "s3");
}

TEST_CASE("StateSync authority mode", "[mitiru][network]")
{
	StateSync sync;
	sync.setAuthority(true);
	sync.pushState("server_state");
	REQUIRE(sync.latestState() == "server_state");
}
