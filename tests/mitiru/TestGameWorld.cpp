#include <catch2/catch_test_macros.hpp>

#include "mitiru/scene/GameWorld.hpp"

using namespace mitiru::scene;

TEST_CASE("GameWorld - create and remove entities", "[mitiru][scene]")
{
	GameWorld world;

	auto e1 = world.createEntity("Player");
	auto e2 = world.createEntity("Enemy");

	REQUIRE(world.isValid(e1));
	REQUIRE(world.isValid(e2));

	world.removeEntity(e1);
	REQUIRE_FALSE(world.isValid(e1));
	REQUIRE(world.isValid(e2));
}

TEST_CASE("GameWorld - add and get TransformComponent", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Object");

	TransformComponent tc;
	tc.position = {1.0f, 2.0f, 3.0f};
	tc.scale = {2.0f, 2.0f, 2.0f};
	world.addComponent(e, tc);

	REQUIRE(world.hasComponent<TransformComponent>(e));

	auto* comp = world.getComponent<TransformComponent>(e);
	REQUIRE(comp != nullptr);
	REQUIRE(comp->position.x == 1.0f);
	REQUIRE(comp->position.y == 2.0f);
	REQUIRE(comp->position.z == 3.0f);
	REQUIRE(comp->scale.x == 2.0f);
}

TEST_CASE("GameWorld - add and get MeshComponent", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Renderable");

	world.addComponent(e, MeshComponent{"player_mesh"});

	REQUIRE(world.hasComponent<MeshComponent>(e));
	auto* mesh = world.getComponent<MeshComponent>(e);
	REQUIRE(mesh->meshId == "player_mesh");
}

TEST_CASE("GameWorld - add and get LightComponent", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Light");

	LightComponent lc;
	lc.type = LightType::Spot;
	lc.intensity = 2.5f;
	lc.range = 20.0f;
	world.addComponent(e, lc);

	auto* light = world.getComponent<LightComponent>(e);
	REQUIRE(light != nullptr);
	REQUIRE(light->type == LightType::Spot);
	REQUIRE(light->intensity == 2.5f);
	REQUIRE(light->range == 20.0f);
}

TEST_CASE("GameWorld - add and get CameraComponent", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Camera");

	CameraComponent cc;
	cc.fov = 90.0f;
	cc.active = true;
	world.addComponent(e, cc);

	auto* cam = world.getComponent<CameraComponent>(e);
	REQUIRE(cam != nullptr);
	REQUIRE(cam->fov == 90.0f);
	REQUIRE(cam->active);
}

TEST_CASE("GameWorld - add and get AudioSourceComponent", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Sound");

	AudioSourceComponent asc;
	asc.soundId = "bgm_01";
	asc.volume = 0.8f;
	asc.loop = true;
	world.addComponent(e, asc);

	auto* audio = world.getComponent<AudioSourceComponent>(e);
	REQUIRE(audio != nullptr);
	REQUIRE(audio->soundId == "bgm_01");
	REQUIRE(audio->volume == 0.8f);
	REQUIRE(audio->loop);
}

TEST_CASE("GameWorld - hasComponent returns false for missing", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Empty");

	REQUIRE_FALSE(world.hasComponent<TransformComponent>(e));
	REQUIRE_FALSE(world.hasComponent<MeshComponent>(e));
}

TEST_CASE("GameWorld - getComponent returns nullptr for missing", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Empty");

	REQUIRE(world.getComponent<TransformComponent>(e) == nullptr);
}

TEST_CASE("GameWorld - removeEntity cleans up components", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("ToRemove");
	world.addComponent(e, TransformComponent{});
	world.addComponent(e, MeshComponent{"mesh"});

	world.removeEntity(e);

	REQUIRE_FALSE(world.isValid(e));
}

TEST_CASE("GameWorld - forEach iterates matching entities", "[mitiru][scene]")
{
	GameWorld world;

	auto e1 = world.createEntity("A");
	auto e2 = world.createEntity("B");
	auto e3 = world.createEntity("C");

	world.addComponent(e1, TransformComponent{{1,0,0},{},{1,1,1}});
	world.addComponent(e2, TransformComponent{{2,0,0},{},{1,1,1}});
	// e3にはTransformComponentを追加しない
	world.addComponent(e3, MeshComponent{"mesh"});

	int count = 0;
	float sumX = 0.0f;
	world.forEach<TransformComponent>([&](EntityId, TransformComponent& tc)
	{
		sumX += tc.position.x;
		++count;
	});

	REQUIRE(count == 2);
	REQUIRE(sumX == 3.0f);
}

TEST_CASE("GameWorld - multiple components on same entity", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Multi");

	world.addComponent(e, TransformComponent{{0,0,0},{},{1,1,1}});
	world.addComponent(e, MeshComponent{"model"});
	world.addComponent(e, LightComponent{LightType::Point, {1,1,1}, 1.0f, 5.0f});

	REQUIRE(world.hasComponent<TransformComponent>(e));
	REQUIRE(world.hasComponent<MeshComponent>(e));
	REQUIRE(world.hasComponent<LightComponent>(e));
	REQUIRE_FALSE(world.hasComponent<CameraComponent>(e));
}

TEST_CASE("GameWorld - entityName returns name", "[mitiru][scene]")
{
	GameWorld world;
	auto e = world.createEntity("Player");
	REQUIRE(world.entityName(e) == "Player");

	auto e2 = world.createEntity();
	REQUIRE(world.entityName(e2).empty());
}

TEST_CASE("GameWorld - entityCount tracks entities", "[mitiru][scene]")
{
	GameWorld world;
	REQUIRE(world.entityCount() == 0);

	auto e1 = world.createEntity("A");
	auto e2 = world.createEntity("B");
	REQUIRE(world.entityCount() == 2);

	world.removeEntity(e1);
	REQUIRE(world.entityCount() == 1);
}
