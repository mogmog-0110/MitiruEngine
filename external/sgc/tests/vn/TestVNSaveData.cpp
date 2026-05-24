#include <catch2/catch_test_macros.hpp>

#include "sgc/vn/VNSaveData.hpp"

using namespace sgc::vn;

TEST_CASE("VNSaveManager - Empty manager has zero slots", "[vn][save]")
{
	VNSaveManager manager;
	REQUIRE(manager.slotCount() == 0);
	REQUIRE(manager.listSlots().empty());
}

TEST_CASE("VNSaveManager - Save and load slot", "[vn][save]")
{
	VNSaveManager manager;
	VNSaveSlot slot;
	slot.slotId = 1;
	slot.timestamp = "2026-03-12T10:00:00";
	slot.scriptPosition = 42;
	slot.variables["flag"] = "true";

	manager.save(slot);
	REQUIRE(manager.slotCount() == 1);

	const auto loaded = manager.load(1);
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->slotId == 1);
	REQUIRE(loaded->timestamp == "2026-03-12T10:00:00");
	REQUIRE(loaded->scriptPosition == 42);
	REQUIRE(loaded->variables.at("flag") == "true");
}

TEST_CASE("VNSaveManager - Load nonexistent slot returns nullopt", "[vn][save]")
{
	VNSaveManager manager;
	REQUIRE_FALSE(manager.load(99).has_value());
}

TEST_CASE("VNSaveManager - Overwrite existing slot", "[vn][save]")
{
	VNSaveManager manager;
	VNSaveSlot slot1;
	slot1.slotId = 1;
	slot1.timestamp = "old";
	manager.save(slot1);

	VNSaveSlot slot2;
	slot2.slotId = 1;
	slot2.timestamp = "new";
	manager.save(slot2);

	REQUIRE(manager.slotCount() == 1);
	REQUIRE(manager.load(1)->timestamp == "new");
}

TEST_CASE("VNSaveManager - Delete slot", "[vn][save]")
{
	VNSaveManager manager;
	VNSaveSlot slot;
	slot.slotId = 1;
	manager.save(slot);

	REQUIRE(manager.deleteSlot(1));
	REQUIRE(manager.slotCount() == 0);
	REQUIRE_FALSE(manager.load(1).has_value());
}

TEST_CASE("VNSaveManager - Delete nonexistent slot returns false", "[vn][save]")
{
	VNSaveManager manager;
	REQUIRE_FALSE(manager.deleteSlot(99));
}

TEST_CASE("VNSaveSlot - Serialize and deserialize round trip", "[vn][save]")
{
	VNSaveSlot original;
	original.slotId = 3;
	original.timestamp = "2026-03-12T15:30:00";
	original.scriptPosition = 100;
	original.variables["name"] = "hero";
	original.variables["level"] = "5";

	const std::string data = serializeSaveSlot(original);
	const auto restored = deserializeSaveSlot(data);

	REQUIRE(restored.has_value());
	REQUIRE(restored->slotId == 3);
	REQUIRE(restored->timestamp == "2026-03-12T15:30:00");
	REQUIRE(restored->scriptPosition == 100);
	REQUIRE(restored->variables.at("name") == "hero");
	REQUIRE(restored->variables.at("level") == "5");
}

TEST_CASE("VNSaveSlot - Deserialize empty string returns nullopt", "[vn][save]")
{
	REQUIRE_FALSE(deserializeSaveSlot("").has_value());
}

TEST_CASE("VNSaveManager - Multiple slots listed", "[vn][save]")
{
	VNSaveManager manager;
	for (int i = 0; i < 5; ++i)
	{
		VNSaveSlot slot;
		slot.slotId = i;
		manager.save(slot);
	}

	REQUIRE(manager.slotCount() == 5);
	REQUIRE(manager.listSlots().size() == 5);
}
