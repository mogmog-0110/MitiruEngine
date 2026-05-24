#include <catch2/catch_test_macros.hpp>
#include <sgc/save/SaveSystem.hpp>

using namespace sgc::save;

TEST_CASE("SaveData - set and get int", "[save]")
{
	SaveData data;
	data.set("level", 42);
	auto val = data.getAs<int>("level");
	REQUIRE(val.has_value());
	REQUIRE(val.value() == 42);
}

TEST_CASE("SaveData - set and get float", "[save]")
{
	SaveData data;
	data.set("hp", 85.5f);
	auto val = data.getAs<float>("hp");
	REQUIRE(val.has_value());
	REQUIRE(val.value() == 85.5f);
}

TEST_CASE("SaveData - set and get bool", "[save]")
{
	SaveData data;
	data.set("completed", true);
	REQUIRE(data.getAs<bool>("completed").value() == true);
}

TEST_CASE("SaveData - set and get string", "[save]")
{
	SaveData data;
	data.set("name", std::string("Hero"));
	REQUIRE(data.getAs<std::string>("name").value() == "Hero");
}

TEST_CASE("SaveData - set and get binary", "[save]")
{
	SaveData data;
	std::vector<uint8_t> blob = {0x01, 0x02, 0xFF};
	data.set("binary", blob);
	auto val = data.getAs<std::vector<uint8_t>>("binary");
	REQUIRE(val.has_value());
	REQUIRE(val.value().size() == 3);
	REQUIRE(val.value()[2] == 0xFF);
}

TEST_CASE("SaveData - getAs wrong type returns nullopt", "[save]")
{
	SaveData data;
	data.set("level", 42);
	REQUIRE_FALSE(data.getAs<float>("level").has_value());
}

TEST_CASE("SaveData - missing key returns nullopt", "[save]")
{
	SaveData data;
	REQUIRE_FALSE(data.get("missing").has_value());
	REQUIRE_FALSE(data.getAs<int>("missing").has_value());
}

TEST_CASE("SaveData - has and remove", "[save]")
{
	SaveData data;
	data.set("key", 1);
	REQUIRE(data.has("key"));
	REQUIRE(data.remove("key"));
	REQUIRE_FALSE(data.has("key"));
	REQUIRE_FALSE(data.remove("key"));
}

TEST_CASE("SaveData - size and clear", "[save]")
{
	SaveData data;
	REQUIRE(data.empty());
	data.set("a", 1);
	data.set("b", 2);
	REQUIRE(data.size() == 2);
	data.clear();
	REQUIRE(data.empty());
}

TEST_CASE("SaveData - version management", "[save]")
{
	SaveData data;
	REQUIRE(data.version() == 1); // default
	data.setVersion(5);
	REQUIRE(data.version() == 5);
}

TEST_CASE("MemorySaveStorage - save and load", "[save]")
{
	MemorySaveStorage storage;
	SaveSlot slot{"s1", "Slot 1", 1000, 1};
	SaveData data;
	data.set("score", 500);

	REQUIRE(storage.save(slot, data));
	auto loaded = storage.load(slot);
	REQUIRE(loaded.has_value());
	REQUIRE(loaded->getAs<int>("score").value() == 500);
}

TEST_CASE("MemorySaveStorage - load missing returns nullopt", "[save]")
{
	MemorySaveStorage storage;
	SaveSlot slot{"missing", "X", 0, 1};
	REQUIRE_FALSE(storage.load(slot).has_value());
}

TEST_CASE("MemorySaveStorage - listSlots", "[save]")
{
	MemorySaveStorage storage;
	SaveData data;
	storage.save(SaveSlot{"s1", "A", 100, 1}, data);
	storage.save(SaveSlot{"s2", "B", 200, 1}, data);

	auto slots = storage.listSlots();
	REQUIRE(slots.size() == 2);
}

TEST_CASE("MemorySaveStorage - deleteSlot", "[save]")
{
	MemorySaveStorage storage;
	SaveSlot slot{"s1", "A", 100, 1};
	storage.save(slot, SaveData{});

	REQUIRE(storage.deleteSlot(slot));
	REQUIRE(storage.slotCount() == 0);
	REQUIRE_FALSE(storage.deleteSlot(slot)); // already deleted
}

TEST_CASE("MemorySaveStorage - overwrite existing slot", "[save]")
{
	MemorySaveStorage storage;
	SaveSlot slot{"s1", "A", 100, 1};

	SaveData data1;
	data1.set("val", 10);
	storage.save(slot, data1);

	SaveData data2;
	data2.set("val", 20);
	storage.save(slot, data2);

	REQUIRE(storage.slotCount() == 1);
	auto loaded = storage.load(slot);
	REQUIRE(loaded->getAs<int>("val").value() == 20);
}
