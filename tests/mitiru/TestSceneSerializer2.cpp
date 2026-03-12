#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/data/JsonBuilder.hpp"
#include "mitiru/data/ProjectFile.hpp"
#include "mitiru/scene/SceneSerializer2.hpp"

using namespace mitiru::data;
using namespace mitiru::scene;

// ── JsonBuilder tests ──────────────────────────────────────

TEST_CASE("JsonBuilder - build simple object", "[mitiru][data][json]")
{
	JsonBuilder builder;
	auto json = builder.beginObject()
		.key("name").value("test")
		.key("count").value(42)
		.endObject().build();

	REQUIRE(json.find("\"name\":\"test\"") != std::string::npos);
	REQUIRE(json.find("\"count\":42") != std::string::npos);
}

TEST_CASE("JsonBuilder - build array", "[mitiru][data][json]")
{
	JsonBuilder builder;
	auto json = builder.beginArray()
		.value(1)
		.value(2)
		.value(3)
		.endArray().build();

	REQUIRE(json == "[1,2,3]");
}

TEST_CASE("JsonBuilder - build nested object", "[mitiru][data][json]")
{
	JsonBuilder builder;
	auto json = builder.beginObject()
		.key("inner").beginObject()
			.key("x").value(1.0f)
		.endObject()
		.key("flag").value(true)
		.endObject().build();

	REQUIRE(json.find("\"inner\":{\"x\":1.0}") != std::string::npos);
	REQUIRE(json.find("\"flag\":true") != std::string::npos);
}

TEST_CASE("JsonBuilder - float array shorthand", "[mitiru][data][json]")
{
	JsonBuilder builder;
	auto json = builder.beginObject()
		.array("pos", {1.0f, 2.0f, 3.0f})
		.endObject().build();

	REQUIRE(json.find("\"pos\":[1.0,2.0,3.0]") != std::string::npos);
}

// ── JsonReader tests ───────────────────────────────────────

TEST_CASE("JsonReader - parse and get values", "[mitiru][data][json]")
{
	JsonReader reader;
	std::string json = R"json({"name":"hello","score":100,"rate":3.14,"flag":true})json";
	REQUIRE(reader.parse(json));

	REQUIRE(reader.getString("name").value() == "hello");
	REQUIRE(reader.getInt("score").value() == 100);
	REQUIRE(reader.getFloat("rate").value() == Catch::Approx(3.14f).margin(0.01f));
	REQUIRE(reader.getBool("flag").value() == true);
}

TEST_CASE("JsonReader - parse array values", "[mitiru][data][json]")
{
	JsonReader reader;
	std::string json = R"json({"items":["a","b","c"]})json";
	REQUIRE(reader.parse(json));

	auto arr = reader.getArray("items");
	REQUIRE(arr.has_value());
	REQUIRE(arr->size() == 3);
	REQUIRE((*arr)[0] == "a");
	REQUIRE((*arr)[1] == "b");
	REQUIRE((*arr)[2] == "c");
}

TEST_CASE("JsonReader - parse nested object", "[mitiru][data][json]")
{
	JsonReader reader;
	std::string json = R"json({"outer":{"inner":"value"}})json";
	REQUIRE(reader.parse(json));

	auto nested = reader.getObject("outer");
	REQUIRE(nested.has_value());
	REQUIRE(nested->getString("inner").value() == "value");
}

// ── SceneSerializer2 tests ─────────────────────────────────

TEST_CASE("SceneSerializer2 - serialize empty world", "[mitiru][scene][serializer]")
{
	GameWorld world;
	SceneSerializer2 serializer;
	serializer.registerBuiltins();

	auto json = serializer.serializeWorld(world);
	REQUIRE(json.find("\"entities\":[]") != std::string::npos);
}

TEST_CASE("SceneSerializer2 - serialize world with entities", "[mitiru][scene][serializer]")
{
	GameWorld world;
	auto player = world.createEntity("Player");
	world.addComponent(player, TransformComponent{{1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
	world.addComponent(player, MeshComponent{"player_mesh"});

	auto light = world.createEntity("Light");
	world.addComponent(light, LightComponent{LightType::Spot, {1.0f, 0.8f, 0.6f}, 2.5f, 15.0f});

	SceneSerializer2 serializer;
	serializer.registerBuiltins();

	auto json = serializer.serializeWorld(world);

	REQUIRE(json.find("\"name\":\"Player\"") != std::string::npos);
	REQUIRE(json.find("\"name\":\"Light\"") != std::string::npos);
	REQUIRE(json.find("\"meshId\":\"player_mesh\"") != std::string::npos);
	REQUIRE(json.find("\"type\":\"Spot\"") != std::string::npos);
}

TEST_CASE("SceneSerializer2 - deserialize world roundtrip", "[mitiru][scene][serializer]")
{
	GameWorld original;
	auto e = original.createEntity("Hero");
	original.addComponent(e, TransformComponent{{5.0f, 10.0f, 15.0f}, {0.1f, 0.2f, 0.3f}, {2.0f, 2.0f, 2.0f}});
	original.addComponent(e, MeshComponent{"hero_mesh"});
	original.addComponent(e, CameraComponent{90.0f, 0.5f, 500.0f, true});

	SceneSerializer2 serializer;
	serializer.registerBuiltins();

	auto json = serializer.serializeWorld(original);

	GameWorld restored;
	REQUIRE(serializer.deserializeWorld(json, restored));

	/// 復元されたワールドにエンティティが1つ存在する
	REQUIRE(restored.entityCount() == 1);

	/// 名前で検索（IDは変わるので全エンティティをチェック）
	EntityId restoredId = 0;
	for (EntityId id = 1; id <= 16; ++id)
	{
		if (restored.isValid(id) && restored.entityName(id) == "Hero")
		{
			restoredId = id;
			break;
		}
	}
	REQUIRE(restoredId != 0);

	auto* tc = restored.getComponent<TransformComponent>(restoredId);
	REQUIRE(tc != nullptr);
	REQUIRE(tc->position.x == Catch::Approx(5.0f));
	REQUIRE(tc->position.y == Catch::Approx(10.0f));
	REQUIRE(tc->position.z == Catch::Approx(15.0f));
	REQUIRE(tc->scale.x == Catch::Approx(2.0f));

	auto* mc = restored.getComponent<MeshComponent>(restoredId);
	REQUIRE(mc != nullptr);
	REQUIRE(mc->meshId == "hero_mesh");

	auto* cc = restored.getComponent<CameraComponent>(restoredId);
	REQUIRE(cc != nullptr);
	REQUIRE(cc->fov == Catch::Approx(90.0f));
	REQUIRE(cc->active == true);
}

TEST_CASE("SceneSerializer2 - serialize single entity", "[mitiru][scene][serializer]")
{
	GameWorld world;
	auto e = world.createEntity("Solo");
	world.addComponent(e, AudioSourceComponent{"bgm_01", 0.8f, true});

	SceneSerializer2 serializer;
	serializer.registerBuiltins();

	auto json = serializer.serializeEntity(world, e);

	REQUIRE(json.find("\"name\":\"Solo\"") != std::string::npos);
	REQUIRE(json.find("\"soundId\":\"bgm_01\"") != std::string::npos);
	REQUIRE(json.find("\"loop\":true") != std::string::npos);
}

TEST_CASE("SceneSerializer2 - serialize SceneGraph", "[mitiru][scene][serializer]")
{
	SceneGraph graph;
	auto root = graph.createNode("Root");
	auto child = graph.createNode("Child");
	graph.reparent(child, root);

	SceneSerializer2 serializer;
	auto json = serializer.serializeSceneGraph(graph);

	REQUIRE(json.find("\"name\":\"Root\"") != std::string::npos);
	REQUIRE(json.find("\"name\":\"Child\"") != std::string::npos);
	REQUIRE(json.find("\"nodes\"") != std::string::npos);
}

TEST_CASE("SceneSerializer2 - deserialize SceneGraph roundtrip", "[mitiru][scene][serializer]")
{
	SceneGraph original;
	auto root = original.createNode("Root");
	auto child = original.createNode("Child");
	original.reparent(child, root);

	SceneSerializer2 serializer;
	auto json = serializer.serializeSceneGraph(original);

	SceneGraph restored;
	REQUIRE(serializer.deserializeSceneGraph(json, restored));

	REQUIRE(restored.nodeCount() == 2);

	auto rootOpt = restored.findByName("Root");
	auto childOpt = restored.findByName("Child");
	REQUIRE(rootOpt.has_value());
	REQUIRE(childOpt.has_value());

	auto* childNode = restored.getNode(*childOpt);
	REQUIRE(childNode != nullptr);
	REQUIRE(childNode->parent == *rootOpt);
}

// ── ProjectFile tests ──────────────────────────────────────

TEST_CASE("ProjectFile - save and load roundtrip", "[mitiru][data][project]")
{
	ProjectConfig config;
	config.projectName = "TestGame";
	config.version = "2.1";
	config.startScene = "title";
	config.assetPaths = {"assets/textures", "assets/sounds", "assets/models"};

	auto json = ProjectFile::saveToString(config);

	auto loaded = ProjectFile::loadFromString(json);
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->projectName == "TestGame");
	REQUIRE(loaded->version == "2.1");
	REQUIRE(loaded->startScene == "title");
	REQUIRE(loaded->assetPaths.size() == 3);
	REQUIRE(loaded->assetPaths[0] == "assets/textures");
	REQUIRE(loaded->assetPaths[1] == "assets/sounds");
	REQUIRE(loaded->assetPaths[2] == "assets/models");
}

// ── Custom component serializer ────────────────────────────

TEST_CASE("SceneSerializer2 - custom component serializer", "[mitiru][scene][serializer]")
{
	/// カスタムコンポーネント（テスト用にTagとしてstringを使う）
	struct TagComponent
	{
		std::string tag;
	};

	GameWorld world;
	auto e = world.createEntity("Tagged");
	world.addComponent(e, TagComponent{"enemy"});
	world.addComponent(e, TransformComponent{});

	SceneSerializer2 serializer;
	serializer.registerBuiltins();

	/// カスタムシリアライザを登録
	serializer.registerComponentSerializer("Tag",
		[](const GameWorld& w, EntityId id) -> std::optional<std::string>
		{
			const auto* c = w.getComponent<TagComponent>(id);
			if (!c) return std::nullopt;
			JsonBuilder b;
			b.beginObject().key("tag").value(c->tag).endObject();
			return b.build();
		},
		[](const std::string& json, GameWorld& w, EntityId id) -> bool
		{
			JsonReader r;
			if (!r.parse(json)) return false;
			TagComponent tc;
			if (auto v = r.getString("tag")) tc.tag = *v;
			w.addComponent(id, tc);
			return true;
		});

	auto json = serializer.serializeEntity(world, e);
	REQUIRE(json.find("\"tag\":\"enemy\"") != std::string::npos);

	/// ラウンドトリップ
	auto worldJson = serializer.serializeWorld(world);
	GameWorld restored;
	REQUIRE(serializer.deserializeWorld(worldJson, restored));

	/// カスタムコンポーネントが復元されていることを確認
	EntityId restoredId = 0;
	for (EntityId id = 1; id <= 16; ++id)
	{
		if (restored.isValid(id))
		{
			restoredId = id;
			break;
		}
	}
	REQUIRE(restoredId != 0);

	auto* tag = restored.getComponent<TagComponent>(restoredId);
	REQUIRE(tag != nullptr);
	REQUIRE(tag->tag == "enemy");
}
