#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/data/SchemaValidator.hpp"
#include "mitiru/data/PrefabSystem.hpp"
#include "mitiru/data/TilemapLoader.hpp"
#include "mitiru/data/ConfigManager.hpp"

using namespace mitiru::data;
using namespace mitiru::scene;

// ============================================================
// SchemaValidator
// ============================================================

TEST_CASE("SchemaField and Schema creation", "[mitiru][data][schema]")
{
	SchemaField field;
	field.name = "health";
	field.type = FieldType::Int;
	field.required = true;
	field.defaultValue = "100";
	field.description = "Entity health";
	field.minValue = 0.0f;
	field.maxValue = 999.0f;

	REQUIRE(field.name == "health");
	REQUIRE(field.type == FieldType::Int);
	REQUIRE(field.required);
	REQUIRE(field.minValue.has_value());
	REQUIRE(*field.minValue == 0.0f);

	Schema schema;
	schema.name = "test_schema";
	schema.version = "1.0";
	schema.fields.push_back(field);

	REQUIRE(schema.name == "test_schema");
	REQUIRE(schema.fields.size() == 1);
}

TEST_CASE("SchemaValidator validates valid JSON", "[mitiru][data][schema]")
{
	SchemaValidator validator;

	Schema schema;
	schema.name = "item";
	schema.version = "1.0";
	schema.fields = {
		{"name", FieldType::String, true, "", "Item name"},
		{"count", FieldType::Int, false, "1", "Stack count"},
	};
	validator.registerSchema(schema);

	auto result = validator.validate("item", R"json({"name":"Sword","count":5})json");
	REQUIRE(result.valid);
	REQUIRE(result.errors.empty());
}

TEST_CASE("SchemaValidator detects missing required field", "[mitiru][data][schema]")
{
	SchemaValidator validator;

	Schema schema;
	schema.name = "npc";
	schema.version = "1.0";
	schema.fields = {
		{"name", FieldType::String, true, "", "NPC name"},
		{"dialogue", FieldType::String, true, "", "Dialogue text"},
	};
	validator.registerSchema(schema);

	/// "dialogue" is missing
	auto result = validator.validate("npc", R"json({"name":"Villager"})json");
	REQUIRE_FALSE(result.valid);
	REQUIRE(result.errors.size() == 1);
}

TEST_CASE("SchemaValidator generateTemplate produces default JSON", "[mitiru][data][schema]")
{
	SchemaValidator validator;

	/// Use the built-in "entity" schema
	auto tmpl = validator.generateTemplate("entity");
	REQUIRE_FALSE(tmpl.empty());

	/// The template should be valid JSON containing "name"
	JsonReader reader;
	REQUIRE(reader.parse(tmpl));
	auto name = reader.getString("name");
	REQUIRE(name.has_value());
}

TEST_CASE("SchemaValidator getSchema returns builtin schemas", "[mitiru][data][schema]")
{
	SchemaValidator validator;

	auto entity = validator.getSchema("entity");
	REQUIRE(entity.has_value());
	REQUIRE(entity->name == "entity");

	auto scene = validator.getSchema("scene");
	REQUIRE(scene.has_value());

	auto prefab = validator.getSchema("prefab");
	REQUIRE(prefab.has_value());

	auto tilemap = validator.getSchema("tilemap");
	REQUIRE(tilemap.has_value());

	auto missing = validator.getSchema("nonexistent");
	REQUIRE_FALSE(missing.has_value());
}

// ============================================================
// PrefabSystem
// ============================================================

TEST_CASE("Prefab registration and instantiation", "[mitiru][data][prefab]")
{
	PrefabLibrary library;
	GameWorld world;

	Prefab prefab;
	prefab.name = "Enemy";
	prefab.componentsJson = R"json({"mesh":"goblin"})json";

	library.registerPrefab(prefab);

	auto id = library.instantiate("Enemy", world);
	REQUIRE(id != INVALID_ENTITY);
	REQUIRE(world.isValid(id));
	REQUIRE(world.entityName(id) == "Enemy");

	auto* mesh = world.getComponent<MeshComponent>(id);
	REQUIRE(mesh != nullptr);
	REQUIRE(mesh->meshId == "goblin");
}

TEST_CASE("Prefab instantiateWithOverrides", "[mitiru][data][prefab]")
{
	PrefabLibrary library;
	GameWorld world;

	Prefab prefab;
	prefab.name = "Player";
	prefab.componentsJson = R"json({"mesh":"hero","position":{"x":0,"y":0,"z":0}})json";
	library.registerPrefab(prefab);

	auto id = library.instantiateWithOverrides(
		"Player",
		R"json({"position":{"x":10,"y":5,"z":0}})json",
		world);

	REQUIRE(id != INVALID_ENTITY);
	auto* tc = world.getComponent<TransformComponent>(id);
	REQUIRE(tc != nullptr);
	REQUIRE(tc->position.x == Catch::Approx(10.0f));
	REQUIRE(tc->position.y == Catch::Approx(5.0f));
}

TEST_CASE("PrefabLibrary JSON roundtrip", "[mitiru][data][prefab]")
{
	PrefabLibrary library;

	Prefab p1;
	p1.name = "Tree";
	p1.componentsJson = R"json({"mesh":"oak_tree"})json";
	p1.childPrefabs = {"Leaf", "Branch"};
	library.registerPrefab(p1);

	auto json = library.toJson();
	REQUIRE_FALSE(json.empty());

	/// Re-import into new library
	PrefabLibrary library2;
	REQUIRE(library2.loadFromJson(json));

	auto loaded = library2.getPrefab("Tree");
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->name == "Tree");
	REQUIRE(loaded->childPrefabs.size() == 2);
}

TEST_CASE("PrefabLibrary allPrefabNames", "[mitiru][data][prefab]")
{
	PrefabLibrary library;
	library.registerPrefab({"A", "{}", {}});
	library.registerPrefab({"B", "{}", {}});

	auto names = library.allPrefabNames();
	REQUIRE(names.size() == 2);
}

// ============================================================
// TilemapLoader
// ============================================================

TEST_CASE("Tilemap createEmpty", "[mitiru][data][tilemap]")
{
	TilemapLoader loader;
	auto tilemap = loader.createEmpty("level1", 20, 15, 32, 32);

	REQUIRE(tilemap.name == "level1");
	REQUIRE(tilemap.tileWidth == 32);
	REQUIRE(tilemap.tileHeight == 32);
	REQUIRE(tilemap.layers.size() == 1);
	REQUIRE(tilemap.layers[0].name == "default");
	REQUIRE(tilemap.layers[0].width == 20);
	REQUIRE(tilemap.layers[0].height == 15);
	REQUIRE(tilemap.layers[0].tiles.empty());
}

TEST_CASE("Tilemap load/save roundtrip", "[mitiru][data][tilemap]")
{
	TilemapLoader loader;
	auto original = loader.createEmpty("dungeon", 10, 10, 16, 16);

	/// Add a tile
	loader.setTile(original.layers[0], 3, 5, {42, 3, 5, 90.0f, true, false});

	auto json = loader.saveToJson(original);
	REQUIRE_FALSE(json.empty());

	auto loaded = loader.loadFromJson(json);
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->name == "dungeon");
	REQUIRE(loaded->tileWidth == 16);
	REQUIRE(loaded->layers.size() == 1);
	REQUIRE(loaded->layers[0].tiles.size() == 1);
	REQUIRE(loaded->layers[0].tiles[0].tileId == 42);
}

TEST_CASE("Tilemap getTile and setTile", "[mitiru][data][tilemap]")
{
	TilemapLoader loader;
	auto tilemap = loader.createEmpty("test", 5, 5, 32, 32);
	auto& layer = tilemap.layers[0];

	/// Initially empty
	auto noTile = loader.getTile(layer, 2, 3);
	REQUIRE_FALSE(noTile.has_value());

	/// Set a tile
	loader.setTile(layer, 2, 3, {7, 2, 3, 0.0f, false, true});

	auto tile = loader.getTile(layer, 2, 3);
	REQUIRE(tile.has_value());
	REQUIRE(tile->tileId == 7);
	REQUIRE(tile->flipY);
	REQUIRE_FALSE(tile->flipX);

	/// Overwrite
	loader.setTile(layer, 2, 3, {99, 2, 3, 45.0f, true, true});
	auto updated = loader.getTile(layer, 2, 3);
	REQUIRE(updated.has_value());
	REQUIRE(updated->tileId == 99);
	REQUIRE(updated->rotation == Catch::Approx(45.0f));
}

// ============================================================
// ConfigManager
// ============================================================

TEST_CASE("ConfigManager set/get roundtrip", "[mitiru][data][config]")
{
	ConfigManager config;

	config.set("name", std::string("TestGame"));
	config.set("width", 1920);
	config.set("volume", 0.8f);
	config.set("fullscreen", true);

	REQUIRE(config.getString("name") == "TestGame");
	REQUIRE(config.getInt("width") == 1920);
	REQUIRE(config.getFloat("volume").has_value());
	REQUIRE(*config.getFloat("volume") == Catch::Approx(0.8f));
	REQUIRE(config.getBool("fullscreen") == true);

	/// Missing key
	REQUIRE_FALSE(config.getString("missing").has_value());
	REQUIRE(config.hasKey("name"));
	REQUIRE_FALSE(config.hasKey("missing"));
}

TEST_CASE("ConfigManager loadFromJson", "[mitiru][data][config]")
{
	ConfigManager config;

	bool ok = config.loadFromJson(
		R"json({"musicVolume":0.7,"sfxVolume":0.9,"difficulty":"hard","vsync":true})json");
	REQUIRE(ok);

	REQUIRE(config.hasKey("musicVolume"));
	REQUIRE(config.getFloat("musicVolume").has_value());
	REQUIRE(*config.getFloat("musicVolume") == Catch::Approx(0.7f));
	REQUIRE(config.getString("difficulty") == "hard");
	REQUIRE(config.getBool("vsync") == true);
}

TEST_CASE("ConfigManager onChange callback", "[mitiru][data][config]")
{
	ConfigManager config;

	int callCount = 0;
	float lastVolume = 0.0f;

	config.onChange("volume", [&callCount, &lastVolume](const ConfigValue& val)
	{
		++callCount;
		if (auto* f = std::get_if<float>(&val))
		{
			lastVolume = *f;
		}
	});

	config.set("volume", 0.5f);
	REQUIRE(callCount == 1);
	REQUIRE(lastVolume == Catch::Approx(0.5f));

	config.set("volume", 0.9f);
	REQUIRE(callCount == 2);
	REQUIRE(lastVolume == Catch::Approx(0.9f));

	/// Setting other keys should not trigger the callback
	config.set("brightness", 1.0f);
	REQUIRE(callCount == 2);
}

TEST_CASE("ConfigManager setDefault does not overwrite", "[mitiru][data][config]")
{
	ConfigManager config;

	config.set("lang", std::string("en"));
	config.setDefault("lang", std::string("ja"));

	REQUIRE(config.getString("lang") == "en");

	/// setDefault on missing key sets the value
	config.setDefault("newKey", 42);
	REQUIRE(config.getInt("newKey") == 42);
}

TEST_CASE("ConfigManager keys returns all keys", "[mitiru][data][config]")
{
	ConfigManager config;
	config.set("a", 1);
	config.set("b", 2);
	config.set("c", 3);

	auto allKeys = config.keys();
	REQUIRE(allKeys.size() == 3);
}
