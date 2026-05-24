#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <sgc/data/DataNode.hpp>
#include <sgc/data/MapData.hpp>

using namespace sgc::data;

TEST_CASE("DataNode default is null", "[data]")
{
	DataNode n;
	REQUIRE(n.isNull());
	REQUIRE(n.type() == NodeType::Null);
}

TEST_CASE("DataNode primitive types", "[data]")
{
	REQUIRE(DataNode(true).asBool() == true);
	REQUIRE(DataNode(false).asBool() == false);
	REQUIRE(DataNode(42).asInt() == 42);
	REQUIRE(DataNode(3.14).asFloat() == Catch::Approx(3.14));
	REQUIRE(DataNode("hello").asString() == "hello");
}

TEST_CASE("DataNode defaults for wrong type access", "[data]")
{
	DataNode n(42);
	REQUIRE(n.asBool(true) == true);  // int -> default bool
	REQUIRE(n.asString() == "");      // int -> empty string
	REQUIRE(n.asFloat() == 42.0);     // int -> float conversion
}

TEST_CASE("DataNode array operations", "[data]")
{
	auto arr = DataNode::array({DataNode(1), DataNode(2), DataNode(3)});
	REQUIRE(arr.isArray());
	REQUIRE(arr.size() == 3);
	REQUIRE(arr[static_cast<size_t>(0)].asInt() == 1);
	REQUIRE(arr[static_cast<size_t>(1)].asInt() == 2);
	REQUIRE(arr[static_cast<size_t>(2)].asInt() == 3);
	REQUIRE(arr[static_cast<size_t>(99)].isNull());  // out of bounds
}

TEST_CASE("DataNode object operations", "[data]")
{
	auto obj = DataNode::object();
	obj["name"] = DataNode("test");
	obj["value"] = DataNode(42);

	REQUIRE(obj.isObject());
	REQUIRE(obj.size() == 2);
	REQUIRE(obj["name"].asString() == "test");
	REQUIRE(obj["value"].asInt() == 42);
	REQUIRE(obj.hasKey("name"));
	REQUIRE_FALSE(obj.hasKey("missing"));
	REQUIRE(obj["missing"].isNull());
}

TEST_CASE("DataNode dot-path access", "[data]")
{
	auto root = DataNode::object();
	auto player = DataNode::object();
	player["speed"] = DataNode(5200.0);
	player["jumpForce"] = DataNode(12.5);
	root["player"] = std::move(player);

	REQUIRE(root.at("player.speed").asFloat() == Catch::Approx(5200.0));
	REQUIRE(root.at("player.jumpForce").asFloat() == Catch::Approx(12.5));
	REQUIRE(root.at("player.missing").isNull());
	REQUIRE(root.at("nonexistent.path").isNull());
}

TEST_CASE("DataNode JSON parse basic", "[data]")
{
	auto n = DataNode::parse(R"({"name": "test", "value": 42, "pi": 3.14, "ok": true, "nil": null})");
	REQUIRE(n.isObject());
	REQUIRE(n["name"].asString() == "test");
	REQUIRE(n["value"].asInt() == 42);
	REQUIRE(n["pi"].asFloat() == Catch::Approx(3.14));
	REQUIRE(n["ok"].asBool() == true);
	REQUIRE(n["nil"].isNull());
}

TEST_CASE("DataNode JSON parse nested", "[data]")
{
	auto n = DataNode::parse(R"({
		"player": {
			"speed": 5200.0,
			"pos": [10.0, 20.0]
		}
	})");
	REQUIRE(n.at("player.speed").asFloat() == Catch::Approx(5200.0));
	REQUIRE(n.at("player.pos")[static_cast<size_t>(0)].asFloat() == Catch::Approx(10.0));
	REQUIRE(n.at("player.pos")[static_cast<size_t>(1)].asFloat() == Catch::Approx(20.0));
}

TEST_CASE("DataNode JSON roundtrip", "[data]")
{
	auto original = DataNode::object();
	original["name"] = DataNode("Graph Walker");
	original["version"] = DataNode(3);
	original["gravity"] = DataNode(980.0);
	auto arr = DataNode::array({DataNode(1.0), DataNode(2.0)});
	original["pos"] = std::move(arr);

	std::string json = original.toJson(false);
	auto parsed = DataNode::parse(json);

	REQUIRE(parsed["name"].asString() == "Graph Walker");
	REQUIRE(parsed["version"].asInt() == 3);
	REQUIRE(parsed["gravity"].asFloat() == Catch::Approx(980.0));
	REQUIRE(parsed["pos"].size() == 2);
}

TEST_CASE("MapEntity serialization roundtrip", "[data]")
{
	MapEntity entity;
	entity.type = "platform";
	entity.id = "plat_0";
	entity.position = {10.0f, -1.0f};
	entity.size = {28.0f, 2.0f};
	entity.properties = DataNode::object();
	entity.properties["color"] = DataNode("blue");

	auto node = entity.toNode();
	auto restored = MapEntity::fromNode(node);

	REQUIRE(restored.type == "platform");
	REQUIRE(restored.id == "plat_0");
	REQUIRE(restored.position.x == Catch::Approx(10.0f));
	REQUIRE(restored.position.y == Catch::Approx(-1.0f));
	REQUIRE(restored.size.x == Catch::Approx(28.0f));
	REQUIRE(restored.properties["color"].asString() == "blue");
}

TEST_CASE("MapFile JSON roundtrip", "[data]")
{
	MapFile map;
	map.metadata = DataNode::object();
	map.metadata["name"] = DataNode("Test Map");

	MapEntity e;
	e.type = "checkpoint";
	e.position = {5.0f, 3.0f};
	map.entities.push_back(e);

	std::string json = map.toJson(true);
	auto loaded = MapFile::fromJson(json);

	REQUIRE(loaded.metadata["name"].asString() == "Test Map");
	REQUIRE(loaded.entities.size() == 1);
	REQUIRE(loaded.entities[0].type == "checkpoint");
	REQUIRE(loaded.entities[0].position.x == Catch::Approx(5.0f));
}

TEST_CASE("ParamTable typed access", "[data]")
{
	auto params = ParamTable::fromJson(R"({
		"player": {
			"speed": 5200.0,
			"hp": 100,
			"name": "Hero",
			"invincible": false,
			"pos": [10.0, 20.0]
		}
	})");

	REQUIRE(params.getFloat("player.speed") == Catch::Approx(5200.0f));
	REQUIRE(params.getInt("player.hp") == 100);
	REQUIRE(params.getString("player.name") == "Hero");
	REQUIRE(params.getBool("player.invincible") == false);

	auto pos = params.getVec2("player.pos");
	REQUIRE(pos.x == Catch::Approx(10.0f));
	REQUIRE(pos.y == Catch::Approx(20.0f));

	// defaults
	REQUIRE(params.getFloat("missing.key", 99.0f) == Catch::Approx(99.0f));
	REQUIRE(params.getInt("missing.key", -1) == -1);
}
