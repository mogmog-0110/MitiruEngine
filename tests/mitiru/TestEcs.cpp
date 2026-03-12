#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>
#include <vector>
#include <algorithm>

#include <mitiru/ecs/MitiruWorld.hpp>
#include <mitiru/ecs/ComponentRegistry.hpp>
#include <mitiru/ecs/Prefab.hpp>
#include <mitiru/ecs/SystemScheduler.hpp>

// ── Test component types ────────────────────────────────────────

struct Position
{
	float x = 0.0f;
	float y = 0.0f;
};

struct Velocity
{
	float dx = 0.0f;
	float dy = 0.0f;
};

struct Health
{
	int hp = 100;
};

// ============================================================
// MitiruWorld
// ============================================================

TEST_CASE("MitiruWorld createEntity returns valid entity", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	auto entity = world.world().createEntity();
	REQUIRE(entity.isValid());
	REQUIRE(world.entityCount() == 1);
}

TEST_CASE("MitiruWorld entityCount tracks creation", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	REQUIRE(world.entityCount() == 0);

	auto e1 = world.world().createEntity();
	auto e2 = world.world().createEntity();
	auto e3 = world.world().createEntity();
	REQUIRE(world.entityCount() == 3);

	static_cast<void>(e1);
	static_cast<void>(e2);
	static_cast<void>(e3);
}

TEST_CASE("MitiruWorld destroyEntity removes metadata", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	auto entity = world.world().createEntity();

	world.setTag(entity, "player");
	world.addLabel(entity, "controllable");
	REQUIRE(world.getTag(entity) == "player");
	REQUIRE(world.hasLabel(entity, "controllable"));

	world.destroyEntity(entity);
	REQUIRE(world.entityCount() == 0);

	// After destroy the entity is dead, so getTag/hasLabel return defaults
	REQUIRE(world.getTag(entity).empty());
	REQUIRE_FALSE(world.hasLabel(entity, "controllable"));
}

TEST_CASE("MitiruWorld setTag and getTag", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	auto entity = world.world().createEntity();

	// Initially empty
	REQUIRE(world.getTag(entity).empty());

	world.setTag(entity, "enemy");
	REQUIRE(world.getTag(entity) == "enemy");

	// Overwrite
	world.setTag(entity, "boss");
	REQUIRE(world.getTag(entity) == "boss");
}

TEST_CASE("MitiruWorld setTag on dead entity is no-op", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	auto entity = world.world().createEntity();
	world.destroyEntity(entity);

	// Should not crash
	world.setTag(entity, "ghost");
	REQUIRE(world.getTag(entity).empty());
}

TEST_CASE("MitiruWorld addLabel and hasLabel", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	auto entity = world.world().createEntity();

	REQUIRE_FALSE(world.hasLabel(entity, "visible"));

	world.addLabel(entity, "visible");
	REQUIRE(world.hasLabel(entity, "visible"));

	world.addLabel(entity, "collidable");
	REQUIRE(world.hasLabel(entity, "visible"));
	REQUIRE(world.hasLabel(entity, "collidable"));
	REQUIRE_FALSE(world.hasLabel(entity, "invisible"));
}

TEST_CASE("MitiruWorld removeLabel", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	auto entity = world.world().createEntity();

	world.addLabel(entity, "tagged");
	REQUIRE(world.hasLabel(entity, "tagged"));

	world.removeLabel(entity, "tagged");
	REQUIRE_FALSE(world.hasLabel(entity, "tagged"));
}

TEST_CASE("MitiruWorld addLabel on dead entity is no-op", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	auto entity = world.world().createEntity();
	world.destroyEntity(entity);

	world.addLabel(entity, "zombie");
	REQUIRE_FALSE(world.hasLabel(entity, "zombie"));
}

TEST_CASE("MitiruWorld snapshot returns JSON with entityCount", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	auto e1 = world.world().createEntity();
	world.setTag(e1, "hero");
	world.addLabel(e1, "active");

	const auto json = world.snapshot();
	REQUIRE(json.find("\"entityCount\":1") != std::string::npos);
	REQUIRE(json.find("\"tags\":{") != std::string::npos);
	REQUIRE(json.find("\"hero\"") != std::string::npos);
	REQUIRE(json.find("\"labels\":{") != std::string::npos);
}

TEST_CASE("MitiruWorld snapshot empty world", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld world;
	const auto json = world.snapshot();
	REQUIRE(json.find("\"entityCount\":0") != std::string::npos);
	REQUIRE(json.find("\"tags\":{}") != std::string::npos);
	REQUIRE(json.find("\"labels\":{}") != std::string::npos);
}

TEST_CASE("MitiruWorld underlying world component operations", "[mitiru][ecs]")
{
	mitiru::ecs::MitiruWorld mw;
	auto entity = mw.world().createEntity();

	mw.world().addComponent(entity, Position{5.0f, 10.0f});
	REQUIRE(mw.world().hasComponent<Position>(entity));

	const auto* pos = mw.world().getComponent<Position>(entity);
	REQUIRE(pos != nullptr);
	REQUIRE(pos->x == Catch::Approx(5.0f));
	REQUIRE(pos->y == Catch::Approx(10.0f));
}

// ============================================================
// ComponentRegistry
// ============================================================

TEST_CASE("ComponentRegistry registerType and isRegistered", "[mitiru][ecs]")
{
	mitiru::ecs::ComponentRegistry registry;

	REQUIRE_FALSE(registry.isRegistered<Position>());

	registry.registerType<Position>("Position", [](const void* p)
	{
		const auto& pos = *static_cast<const Position*>(p);
		return "{\"x\":" + std::to_string(pos.x) + ",\"y\":" + std::to_string(pos.y) + "}";
	});

	REQUIRE(registry.isRegistered<Position>());
	REQUIRE_FALSE(registry.isRegistered<Velocity>());
	REQUIRE(registry.size() == 1);
}

TEST_CASE("ComponentRegistry serialize produces output", "[mitiru][ecs]")
{
	mitiru::ecs::ComponentRegistry registry;

	registry.registerType<Position>("Position", [](const void* p)
	{
		const auto& pos = *static_cast<const Position*>(p);
		return "{\"x\":" + std::to_string(pos.x) + "}";
	});

	Position pos{42.0f, 0.0f};
	const auto typeId = sgc::typeId<Position>();
	const auto result = registry.serialize(typeId, &pos);
	REQUIRE(result.find("42") != std::string::npos);
}

TEST_CASE("ComponentRegistry serialize returns empty for unknown type", "[mitiru][ecs]")
{
	mitiru::ecs::ComponentRegistry registry;
	Velocity vel{1.0f, 2.0f};
	const auto typeId = sgc::typeId<Velocity>();
	REQUIRE(registry.serialize(typeId, &vel).empty());
}

TEST_CASE("ComponentRegistry serialize returns empty for null data", "[mitiru][ecs]")
{
	mitiru::ecs::ComponentRegistry registry;
	registry.registerType<Position>("Position", [](const void*)
	{
		return std::string("data");
	});
	const auto typeId = sgc::typeId<Position>();
	REQUIRE(registry.serialize(typeId, nullptr).empty());
}

TEST_CASE("ComponentRegistry registeredNames lists all types", "[mitiru][ecs]")
{
	mitiru::ecs::ComponentRegistry registry;
	registry.registerType<Position>("Position", [](const void*) { return std::string{}; });
	registry.registerType<Velocity>("Velocity", [](const void*) { return std::string{}; });
	registry.registerType<Health>("Health", [](const void*) { return std::string{}; });

	const auto names = registry.registeredNames();
	REQUIRE(names.size() == 3);

	// Check all names are present (order may vary due to unordered_map)
	REQUIRE(std::find(names.begin(), names.end(), "Position") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "Velocity") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "Health") != names.end());
}

TEST_CASE("ComponentRegistry toJson produces JSON array", "[mitiru][ecs]")
{
	mitiru::ecs::ComponentRegistry registry;
	registry.registerType<Position>("Position", [](const void*) { return std::string{}; });

	const auto json = registry.toJson();
	REQUIRE(json.front() == '[');
	REQUIRE(json.back() == ']');
	REQUIRE(json.find("\"Position\"") != std::string::npos);
}

TEST_CASE("ComponentRegistry empty toJson returns empty array", "[mitiru][ecs]")
{
	mitiru::ecs::ComponentRegistry registry;
	REQUIRE(registry.toJson() == "[]");
}

// ============================================================
// Prefab / PrefabComponent / PrefabLibrary
// ============================================================

TEST_CASE("PrefabComponent construction", "[mitiru][ecs]")
{
	mitiru::ecs::PrefabComponent comp{"Position", "{\"x\":0,\"y\":0}"};
	REQUIRE(comp.name == "Position");
	REQUIRE(comp.jsonData == "{\"x\":0,\"y\":0}");
}

TEST_CASE("Prefab construction with components", "[mitiru][ecs]")
{
	mitiru::ecs::Prefab prefab;
	prefab.name = "player";
	prefab.components.push_back({"Position", "{\"x\":0}"});
	prefab.components.push_back({"Health", "{\"hp\":100}"});

	REQUIRE(prefab.name == "player");
	REQUIRE(prefab.components.size() == 2);
	REQUIRE(prefab.components[0].name == "Position");
	REQUIRE(prefab.components[1].name == "Health");
}

TEST_CASE("PrefabLibrary toJson produces valid JSON", "[mitiru][ecs]")
{
	mitiru::ecs::Prefab prefab;
	prefab.name = "enemy";
	prefab.components.push_back({"Position", "{\"x\":10}"});
	prefab.components.push_back({"Velocity", "{\"dx\":1}"});

	const auto json = mitiru::ecs::PrefabLibrary::toJson(prefab);
	REQUIRE(json.find("\"name\":\"enemy\"") != std::string::npos);
	REQUIRE(json.find("\"components\":[") != std::string::npos);
	REQUIRE(json.find("\"Position\"") != std::string::npos);
	REQUIRE(json.find("\"Velocity\"") != std::string::npos);
}

TEST_CASE("PrefabLibrary toJson with empty components", "[mitiru][ecs]")
{
	mitiru::ecs::Prefab prefab;
	prefab.name = "empty";

	const auto json = mitiru::ecs::PrefabLibrary::toJson(prefab);
	REQUIRE(json.find("\"components\":[]") != std::string::npos);
}

TEST_CASE("PrefabLibrary fromJson parses name", "[mitiru][ecs]")
{
	const auto json = mitiru::ecs::PrefabLibrary::toJson(
		mitiru::ecs::Prefab{"hero", {{"Pos", "{}"}}});

	const auto parsed = mitiru::ecs::PrefabLibrary::fromJson(json);
	REQUIRE(parsed.name == "hero");
}

TEST_CASE("PrefabLibrary toJson/fromJson roundtrip preserves name", "[mitiru][ecs]")
{
	mitiru::ecs::Prefab original;
	original.name = "projectile";
	original.components.push_back({"Position", "{\"x\":5}"});

	const auto json = mitiru::ecs::PrefabLibrary::toJson(original);
	const auto restored = mitiru::ecs::PrefabLibrary::fromJson(json);

	REQUIRE(restored.name == original.name);
}

TEST_CASE("PrefabLibrary add and get", "[mitiru][ecs]")
{
	mitiru::ecs::PrefabLibrary lib;
	lib.add(mitiru::ecs::Prefab{"player", {{"Pos", "{}"}}});

	const auto result = lib.get("player");
	REQUIRE(result.has_value());
	REQUIRE(result->name == "player");
	REQUIRE(result->components.size() == 1);
}

TEST_CASE("PrefabLibrary get missing returns nullopt", "[mitiru][ecs]")
{
	mitiru::ecs::PrefabLibrary lib;
	REQUIRE_FALSE(lib.get("nonexistent").has_value());
}

TEST_CASE("PrefabLibrary has checks existence", "[mitiru][ecs]")
{
	mitiru::ecs::PrefabLibrary lib;
	REQUIRE_FALSE(lib.has("x"));

	lib.add(mitiru::ecs::Prefab{"x", {}});
	REQUIRE(lib.has("x"));
}

TEST_CASE("PrefabLibrary remove erases entry", "[mitiru][ecs]")
{
	mitiru::ecs::PrefabLibrary lib;
	lib.add(mitiru::ecs::Prefab{"temp", {}});
	REQUIRE(lib.has("temp"));

	lib.remove("temp");
	REQUIRE_FALSE(lib.has("temp"));
	REQUIRE(lib.size() == 0);
}

TEST_CASE("PrefabLibrary listNames returns all names", "[mitiru][ecs]")
{
	mitiru::ecs::PrefabLibrary lib;
	lib.add(mitiru::ecs::Prefab{"a", {}});
	lib.add(mitiru::ecs::Prefab{"b", {}});
	lib.add(mitiru::ecs::Prefab{"c", {}});

	const auto names = lib.listNames();
	REQUIRE(names.size() == 3);
	REQUIRE(std::find(names.begin(), names.end(), "a") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "b") != names.end());
	REQUIRE(std::find(names.begin(), names.end(), "c") != names.end());
}

TEST_CASE("PrefabLibrary size tracks entries", "[mitiru][ecs]")
{
	mitiru::ecs::PrefabLibrary lib;
	REQUIRE(lib.size() == 0);

	lib.add(mitiru::ecs::Prefab{"one", {}});
	REQUIRE(lib.size() == 1);

	lib.add(mitiru::ecs::Prefab{"two", {}});
	REQUIRE(lib.size() == 2);
}

TEST_CASE("PrefabLibrary add overwrites duplicate name", "[mitiru][ecs]")
{
	mitiru::ecs::PrefabLibrary lib;
	lib.add(mitiru::ecs::Prefab{"dup", {{"A", "{}"}}});
	lib.add(mitiru::ecs::Prefab{"dup", {{"B", "{}"}, {"C", "{}"}}});

	REQUIRE(lib.size() == 1);
	const auto result = lib.get("dup");
	REQUIRE(result.has_value());
	REQUIRE(result->components.size() == 2);
}

// ============================================================
// SystemScheduler
// ============================================================

TEST_CASE("SystemScheduler addSystem and size", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;
	REQUIRE(scheduler.size() == 0);

	scheduler.addSystem({"physics", 10, [](float) {}});
	REQUIRE(scheduler.size() == 1);

	scheduler.addSystem({"render", 100, [](float) {}});
	REQUIRE(scheduler.size() == 2);
}

TEST_CASE("SystemScheduler update executes systems in priority order", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;

	std::vector<std::string> executionOrder;

	scheduler.addSystem({"render", 100, [&](float)
	{
		executionOrder.push_back("render");
	}});
	scheduler.addSystem({"physics", 10, [&](float)
	{
		executionOrder.push_back("physics");
	}});
	scheduler.addSystem({"input", 1, [&](float)
	{
		executionOrder.push_back("input");
	}});

	scheduler.update(0.016f);

	REQUIRE(executionOrder.size() == 3);
	REQUIRE(executionOrder[0] == "input");
	REQUIRE(executionOrder[1] == "physics");
	REQUIRE(executionOrder[2] == "render");
}

TEST_CASE("SystemScheduler update passes dt to systems", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;
	float receivedDt = 0.0f;

	scheduler.addSystem({"test", 0, [&](float dt)
	{
		receivedDt = dt;
	}});

	scheduler.update(0.033f);
	REQUIRE(receivedDt == Catch::Approx(0.033f));
}

TEST_CASE("SystemScheduler removeSystem by name", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;

	scheduler.addSystem({"a", 0, [](float) {}});
	scheduler.addSystem({"b", 1, [](float) {}});
	REQUIRE(scheduler.size() == 2);

	scheduler.removeSystem("a");
	REQUIRE(scheduler.size() == 1);

	const auto names = scheduler.systemNames();
	REQUIRE(names.size() == 1);
	REQUIRE(names[0] == "b");
}

TEST_CASE("SystemScheduler removeSystem nonexistent is no-op", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;
	scheduler.addSystem({"x", 0, [](float) {}});

	scheduler.removeSystem("nonexistent");
	REQUIRE(scheduler.size() == 1);
}

TEST_CASE("SystemScheduler systemNames returns names in priority order", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;
	scheduler.addSystem({"gamma", 30, [](float) {}});
	scheduler.addSystem({"alpha", 10, [](float) {}});
	scheduler.addSystem({"beta", 20, [](float) {}});

	const auto names = scheduler.systemNames();
	REQUIRE(names.size() == 3);
	REQUIRE(names[0] == "alpha");
	REQUIRE(names[1] == "beta");
	REQUIRE(names[2] == "gamma");
}

TEST_CASE("SystemScheduler toJson produces JSON array", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;
	scheduler.addSystem({"physics", 10, [](float) {}});
	scheduler.addSystem({"render", 20, [](float) {}});

	const auto json = scheduler.toJson();
	REQUIRE(json.front() == '[');
	REQUIRE(json.back() == ']');
	REQUIRE(json.find("\"physics\"") != std::string::npos);
	REQUIRE(json.find("\"render\"") != std::string::npos);
}

TEST_CASE("SystemScheduler clear removes all systems", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;
	scheduler.addSystem({"a", 0, [](float) {}});
	scheduler.addSystem({"b", 1, [](float) {}});

	scheduler.clear();
	REQUIRE(scheduler.size() == 0);
	REQUIRE(scheduler.systemNames().empty());
}

TEST_CASE("SystemScheduler update with null updateFn does not crash", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;
	scheduler.addSystem({"nullsys", 0, nullptr});

	// Should not throw or crash
	scheduler.update(0.016f);
	REQUIRE(scheduler.size() == 1);
}

TEST_CASE("SystemScheduler multiple updates maintain sorted order", "[mitiru][ecs]")
{
	mitiru::ecs::SystemScheduler scheduler;

	int counter = 0;
	int firstOrder = -1;
	int secondOrder = -1;

	scheduler.addSystem({"second", 20, [&](float)
	{
		secondOrder = counter++;
	}});
	scheduler.addSystem({"first", 10, [&](float)
	{
		firstOrder = counter++;
	}});

	scheduler.update(0.016f);
	REQUIRE(firstOrder == 0);
	REQUIRE(secondOrder == 1);

	// Second update should maintain order
	counter = 0;
	firstOrder = -1;
	secondOrder = -1;
	scheduler.update(0.016f);
	REQUIRE(firstOrder == 0);
	REQUIRE(secondOrder == 1);
}
