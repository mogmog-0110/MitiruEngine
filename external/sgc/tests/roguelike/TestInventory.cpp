#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/roguelike/Inventory.hpp"

using namespace sgc::roguelike;

TEST_CASE("Inventory - Empty inventory", "[roguelike][inventory]")
{
	Inventory inv(10);
	REQUIRE(inv.capacity() == 10);
	REQUIRE(inv.usedSlots() == 0);
	REQUIRE(inv.totalWeight() == Catch::Approx(0.0f));
}

TEST_CASE("Inventory - Add non-stackable item", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item sword{"sword", "Iron Sword", false, 1, 3.5f};

	REQUIRE(inv.addItem(sword, 1));
	REQUIRE(inv.usedSlots() == 1);
	REQUIRE(inv.countItem("sword") == 1);
}

TEST_CASE("Inventory - Add stackable items merge into stack", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item potion{"potion", "Health Potion", true, 10, 0.5f};

	REQUIRE(inv.addItem(potion, 3));
	REQUIRE(inv.usedSlots() == 1);
	REQUIRE(inv.countItem("potion") == 3);

	REQUIRE(inv.addItem(potion, 5));
	REQUIRE(inv.usedSlots() == 1);  // Still one slot
	REQUIRE(inv.countItem("potion") == 8);
}

TEST_CASE("Inventory - Stackable overflow creates new slot", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item potion{"potion", "Health Potion", true, 5, 0.5f};

	REQUIRE(inv.addItem(potion, 3));
	REQUIRE(inv.addItem(potion, 4));  // 3+4=7 > maxStack(5), needs 2 slots
	REQUIRE(inv.usedSlots() == 2);
	REQUIRE(inv.countItem("potion") == 7);
}

TEST_CASE("Inventory - Capacity limit prevents adding", "[roguelike][inventory]")
{
	Inventory inv(2);
	Item sword{"sword", "Sword", false, 1, 1.0f};

	REQUIRE(inv.addItem(sword, 1));
	REQUIRE(inv.addItem(sword, 1));
	REQUIRE_FALSE(inv.addItem(sword, 1));  // Full
	REQUIRE(inv.usedSlots() == 2);
}

TEST_CASE("Inventory - Remove item reduces count", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item potion{"potion", "Potion", true, 10, 0.5f};
	inv.addItem(potion, 5);

	REQUIRE(inv.removeItem("potion", 3));
	REQUIRE(inv.countItem("potion") == 2);
}

TEST_CASE("Inventory - Remove all items removes slot", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item potion{"potion", "Potion", true, 10, 0.5f};
	inv.addItem(potion, 5);

	REQUIRE(inv.removeItem("potion", 5));
	REQUIRE(inv.usedSlots() == 0);
	REQUIRE(inv.countItem("potion") == 0);
}

TEST_CASE("Inventory - Remove more than available returns false", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item potion{"potion", "Potion", true, 10, 0.5f};
	inv.addItem(potion, 3);

	REQUIRE_FALSE(inv.removeItem("potion", 5));
	REQUIRE(inv.countItem("potion") == 3);  // Unchanged
}

TEST_CASE("Inventory - Remove nonexistent item returns false", "[roguelike][inventory]")
{
	Inventory inv(10);
	REQUIRE_FALSE(inv.removeItem("nothing", 1));
}

TEST_CASE("Inventory - GetSlot returns correct data", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item sword{"sword", "Iron Sword", false, 1, 3.5f};
	inv.addItem(sword, 1);

	const auto* slot = inv.getSlot(0);
	REQUIRE(slot != nullptr);
	REQUIRE(slot->item.id == "sword");
	REQUIRE(slot->count == 1);
}

TEST_CASE("Inventory - GetSlot out of range returns nullptr", "[roguelike][inventory]")
{
	Inventory inv(10);
	REQUIRE(inv.getSlot(0) == nullptr);
}

TEST_CASE("Inventory - Total weight calculation", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item sword{"sword", "Sword", false, 1, 3.5f};
	Item potion{"potion", "Potion", true, 10, 0.5f};

	inv.addItem(sword, 1);
	inv.addItem(potion, 4);

	// 3.5 * 1 + 0.5 * 4 = 5.5
	REQUIRE(inv.totalWeight() == Catch::Approx(5.5f));
}

TEST_CASE("Inventory - Equip and unequip", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item sword{"sword", "Iron Sword", false, 1, 3.5f};

	inv.equip(EquipSlot::Weapon, sword);
	auto equipped = inv.getEquipped(EquipSlot::Weapon);
	REQUIRE(equipped.has_value());
	REQUIRE(equipped->id == "sword");

	inv.unequip(EquipSlot::Weapon);
	REQUIRE_FALSE(inv.getEquipped(EquipSlot::Weapon).has_value());
}

TEST_CASE("Inventory - GetEquipped empty slot returns nullopt", "[roguelike][inventory]")
{
	Inventory inv(10);
	REQUIRE_FALSE(inv.getEquipped(EquipSlot::Armor).has_value());
}

TEST_CASE("Inventory - Clear empties all slots", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item sword{"sword", "Sword", false, 1, 1.0f};
	inv.addItem(sword, 1);
	inv.addItem(sword, 1);
	inv.clear();
	REQUIRE(inv.usedSlots() == 0);
}

TEST_CASE("Inventory - Multiple equipment slots", "[roguelike][inventory]")
{
	Inventory inv(10);
	Item sword{"sword", "Sword", false, 1, 3.0f};
	Item armor{"armor", "Plate", false, 1, 10.0f};
	Item ring{"ring", "Ring", false, 1, 0.1f};

	inv.equip(EquipSlot::Weapon, sword);
	inv.equip(EquipSlot::Armor, armor);
	inv.equip(EquipSlot::Accessory, ring);

	REQUIRE(inv.getEquipped(EquipSlot::Weapon)->id == "sword");
	REQUIRE(inv.getEquipped(EquipSlot::Armor)->id == "armor");
	REQUIRE(inv.getEquipped(EquipSlot::Accessory)->id == "ring");
}
