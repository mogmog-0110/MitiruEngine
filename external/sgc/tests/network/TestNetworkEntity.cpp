#include <catch2/catch_test_macros.hpp>
#include "sgc/network/NetworkEntity.hpp"

using namespace sgc::network;

TEST_CASE("NetworkEntityManager registers entities", "[network][entity]")
{
	NetworkEntityManager manager;
	auto id1 = manager.registerEntity(1, Authority::Local);
	auto id2 = manager.registerEntity(2, Authority::Remote);

	REQUIRE(id1 != id2);
	REQUIRE(manager.entityCount() == 2);
}

TEST_CASE("NetworkEntityManager finds entities", "[network][entity]")
{
	NetworkEntityManager manager;
	auto id = manager.registerEntity(10, Authority::Server);

	auto* entity = manager.find(id);
	REQUIRE(entity != nullptr);
	REQUIRE(entity->networkId == id);
	REQUIRE(entity->ownerId == 10);
	REQUIRE(entity->authority == Authority::Server);
	REQUIRE_FALSE(entity->isLocallyControlled());
	REQUIRE_FALSE(entity->isRemotelyControlled());

	REQUIRE(manager.find(9999) == nullptr);

	const auto& constManager = manager;
	REQUIRE(constManager.find(id) != nullptr);
	REQUIRE(constManager.find(9999) == nullptr);
}

TEST_CASE("NetworkEntityManager transfers authority", "[network][entity]")
{
	NetworkEntityManager manager;
	auto id = manager.registerEntity(1, Authority::Local);

	auto* entity = manager.find(id);
	REQUIRE(entity->isLocallyControlled());

	manager.transferAuthority(id, 2, Authority::Remote);
	REQUIRE(entity->ownerId == 2);
	REQUIRE(entity->isRemotelyControlled());

	// 存在しないIDに対しては何も起きない
	manager.transferAuthority(9999, 3, Authority::Server);
	REQUIRE(manager.entityCount() == 1);
}

TEST_CASE("NetworkEntityManager gets entities owned by peer", "[network][entity]")
{
	NetworkEntityManager manager;
	manager.registerEntity(1, Authority::Local);
	manager.registerEntity(1, Authority::Local);
	manager.registerEntity(2, Authority::Remote);

	auto owned = manager.getEntitiesOwnedBy(1);
	REQUIRE(owned.size() == 2);

	auto owned2 = manager.getEntitiesOwnedBy(2);
	REQUIRE(owned2.size() == 1);

	auto owned3 = manager.getEntitiesOwnedBy(99);
	REQUIRE(owned3.empty());
}

TEST_CASE("NetworkEntityManager unregisters entities", "[network][entity]")
{
	NetworkEntityManager manager;
	auto id = manager.registerEntity(1, Authority::Local);
	REQUIRE(manager.entityCount() == 1);

	manager.unregisterEntity(id);
	REQUIRE(manager.entityCount() == 0);
	REQUIRE(manager.find(id) == nullptr);

	// 存在しないIDの解除は安全
	manager.unregisterEntity(9999);
}

TEST_CASE("NetworkEntityManager clears all entities", "[network][entity]")
{
	NetworkEntityManager manager;
	manager.registerEntity(1, Authority::Local);
	manager.registerEntity(2, Authority::Remote);
	manager.registerEntity(3, Authority::Server);
	REQUIRE(manager.entityCount() == 3);

	manager.clear();
	REQUIRE(manager.entityCount() == 0);
}
