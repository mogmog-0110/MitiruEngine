#include <catch2/catch_test_macros.hpp>

#include "mitiru/graphwalker/Common.hpp"
#include "mitiru/graphwalker/SaveData.hpp"
#include "mitiru/graphwalker/Zone.hpp"

using namespace mitiru::gw;

TEST_CASE("SharedData has correct defaults", "[gw][core]")
{
	SharedData data;
	REQUIRE(data.state == GameState::Title);
	REQUIRE(data.currentSlot == 0);
	REQUIRE(data.playerSpawnPos.x == GameConstants::DEFAULT_SPAWN_X);
	REQUIRE(data.playerSpawnPos.y == GameConstants::DEFAULT_SPAWN_Y);
	REQUIRE(data.visitedCheckpoints.empty());
	REQUIRE(data.collectedItems.empty());
	REQUIRE(data.unlockedButtons.empty());
	REQUIRE(data.unlockedGates.empty());
	REQUIRE(data.achievements.empty());
	REQUIRE(data.currentCheckpointId.empty());
	REQUIRE(data.currentZone == ZoneId::Tutorial);
	REQUIRE(data.playTime == 0.0f);
	REQUIRE(data.ngPlusLevel == 0);
	REQUIRE(data.highContrast == false);
	REQUIRE(data.colorblindMode == 0);
}

TEST_CASE("GameState enum values are distinct", "[gw][core]")
{
	REQUIRE(static_cast<int>(GameState::Title) == 0);
	REQUIRE(static_cast<int>(GameState::Playing) == 1);
	REQUIRE(static_cast<int>(GameState::Paused) == 2);
	REQUIRE(static_cast<int>(GameState::Clear) == 3);
	REQUIRE(static_cast<int>(GameState::GameOver) == 4);
}

TEST_CASE("ObjectType enum covers all types", "[gw][core]")
{
	REQUIRE(static_cast<int>(ObjectType::Player) == 0);
	REQUIRE(static_cast<int>(ObjectType::StaticPlatform) == 1);
	REQUIRE(static_cast<int>(ObjectType::MovingPlatform) == 2);
	REQUIRE(static_cast<int>(ObjectType::CrumblingPlatform) == 3);
	REQUIRE(static_cast<int>(ObjectType::SpringPlatform) == 4);
	REQUIRE(static_cast<int>(ObjectType::SpikeHazard) == 5);
	REQUIRE(static_cast<int>(ObjectType::LaserBarrier) == 6);
	REQUIRE(static_cast<int>(ObjectType::CollectibleBlock) == 7);
	REQUIRE(static_cast<int>(ObjectType::Checkpoint) == 8);
	REQUIRE(static_cast<int>(ObjectType::NPC) == 9);
	REQUIRE(static_cast<int>(ObjectType::PatrolEnemy) == 10);
	REQUIRE(static_cast<int>(ObjectType::Gate) == 11);
	REQUIRE(static_cast<int>(ObjectType::Goal) == 12);
	REQUIRE(static_cast<int>(ObjectType::StaticLineString) == 13);
	REQUIRE(static_cast<int>(ObjectType::FormulaGraph) == 14);
}

TEST_CASE("NeonColor zone presets are non-zero", "[gw][core]")
{
	/// 全ゾーンのプライマリ/セカンダリカラーが非ゼロであること
	for (int i = 0; i < ZONE_COUNT; ++i)
	{
		auto zid = static_cast<ZoneId>(i);
		auto primary = NeonColor::zonePrimary(zid);
		auto secondary = NeonColor::zoneSecondary(zid);

		REQUIRE((primary.r > 0.0f || primary.g > 0.0f || primary.b > 0.0f));
		REQUIRE((secondary.r > 0.0f || secondary.g > 0.0f || secondary.b > 0.0f));
		REQUIRE(primary.a == 1.0f);
		REQUIRE(secondary.a == 1.0f);
	}
}

TEST_CASE("NeonColor special presets", "[gw][core]")
{
	auto bg = NeonColor::background();
	REQUIRE(bg.r > 0.0f);
	REQUIRE(bg.a == 1.0f);

	auto player = NeonColor::player();
	REQUIRE(player.b == 1.0f);

	auto goal = NeonColor::goal();
	REQUIRE(goal.r == 1.0f);
}

TEST_CASE("SaveData save produces valid JSON", "[gw][save]")
{
	SharedData data;
	data.state = GameState::Playing;
	data.currentZone = ZoneId::Linear;
	data.playerSpawnPos = { 10.0f, 20.0f };
	data.playTime = 123.5f;
	data.currentCheckpointId = "cp_01";

	auto json = SaveData::save(0, data);

	REQUIRE(json.find("\"version\":\"v5\"") != std::string::npos);
	REQUIRE(json.find("\"slot\":0") != std::string::npos);
	REQUIRE(json.find("\"checkpointId\":\"cp_01\"") != std::string::npos);
	REQUIRE(json.find("\"currentZone\":1") != std::string::npos);
}

TEST_CASE("SaveData load roundtrip", "[gw][save]")
{
	SharedData original;
	original.state = GameState::Playing;
	original.currentZone = ZoneId::Trig;
	original.playerSpawnPos = { 15.0f, -5.0f };
	original.playTime = 300.0f;
	original.ngPlusLevel = 2;
	original.highContrast = true;
	original.colorblindMode = 1;
	original.currentCheckpointId = "cp_trig_03";
	original.currentSlot = 1;

	auto json = SaveData::save(1, original);
	auto loaded = SaveData::load(json);

	REQUIRE(loaded.has_value());
	REQUIRE(loaded->state == GameState::Playing);
	REQUIRE(loaded->currentZone == ZoneId::Trig);
	REQUIRE(loaded->playerSpawnPos.x == 15.0f);
	REQUIRE(loaded->playerSpawnPos.y == -5.0f);
	REQUIRE(loaded->playTime == 300.0f);
	REQUIRE(loaded->ngPlusLevel == 2);
	REQUIRE(loaded->highContrast == true);
	REQUIRE(loaded->colorblindMode == 1);
	REQUIRE(loaded->currentCheckpointId == "cp_trig_03");
	REQUIRE(loaded->currentSlot == 1);
}

TEST_CASE("SaveData createNewGame defaults", "[gw][save]")
{
	auto data = SaveData::createNewGame();

	REQUIRE(data.state == GameState::Playing);
	REQUIRE(data.currentZone == ZoneId::Tutorial);
	REQUIRE(data.playerSpawnPos.x == GameConstants::DEFAULT_SPAWN_X);
	REQUIRE(data.playerSpawnPos.y == GameConstants::DEFAULT_SPAWN_Y);
	REQUIRE(data.playTime == 0.0f);
	REQUIRE(data.ngPlusLevel == 0);
	REQUIRE(data.highContrast == false);
	REQUIRE(data.colorblindMode == 0);
	REQUIRE(data.visitedCheckpoints.empty());
	REQUIRE(data.collectedItems.empty());
}

TEST_CASE("SaveData load returns nullopt for invalid JSON", "[gw][save]")
{
	auto result = SaveData::load("not valid json");
	REQUIRE_FALSE(result.has_value());
}

TEST_CASE("SaveSlot isEmpty detection", "[gw][save]")
{
	SaveSlot empty;
	REQUIRE(empty.isEmpty == true);

	SharedData data = SaveData::createNewGame();
	auto slot = SaveData::toSlot(data);
	REQUIRE(slot.isEmpty == false);
	REQUIRE(slot.version == "v5");
}

TEST_CASE("Collected items persist through save/load", "[gw][save]")
{
	SharedData data = SaveData::createNewGame();
	data.collectedItems.insert("block_001");
	data.collectedItems.insert("block_042");
	data.collectedItems.insert("block_099");
	data.unlockedButtons.insert("sin");
	data.unlockedButtons.insert("cos");

	auto json = SaveData::save(0, data);
	auto loaded = SaveData::load(json);

	REQUIRE(loaded.has_value());
	REQUIRE(loaded->collectedItems.size() == 3);
	REQUIRE(loaded->collectedItems.count("block_001") == 1);
	REQUIRE(loaded->collectedItems.count("block_042") == 1);
	REQUIRE(loaded->collectedItems.count("block_099") == 1);
	REQUIRE(loaded->unlockedButtons.size() == 2);
	REQUIRE(loaded->unlockedButtons.count("sin") == 1);
	REQUIRE(loaded->unlockedButtons.count("cos") == 1);
}

TEST_CASE("Achievements persist through save/load", "[gw][save]")
{
	SharedData data = SaveData::createNewGame();
	data.achievements["first_jump"] = true;
	data.achievements["speed_run"] = false;
	data.achievements["all_blocks"] = true;

	auto json = SaveData::save(0, data);
	auto loaded = SaveData::load(json);

	REQUIRE(loaded.has_value());
	REQUIRE(loaded->achievements.size() == 3);
	REQUIRE(loaded->achievements.at("first_jump") == true);
	REQUIRE(loaded->achievements.at("speed_run") == false);
	REQUIRE(loaded->achievements.at("all_blocks") == true);
}

TEST_CASE("ZoneInfo for each zone has correct colors", "[gw][zone]")
{
	auto tutorial = getZoneInfo(ZoneId::Tutorial);
	REQUIRE(tutorial.name == "Tutorial");
	REQUIRE(tutorial.primary == NeonColor::zonePrimary(ZoneId::Tutorial));
	REQUIRE(tutorial.secondary == NeonColor::zoneSecondary(ZoneId::Tutorial));

	auto chaos = getZoneInfo(ZoneId::Chaos);
	REQUIRE(chaos.name == "Chaos Sanctum");
	REQUIRE(chaos.bgmTempo == 1.5f);
}

TEST_CASE("getAllZones returns 8 zones", "[gw][zone]")
{
	auto zones = getAllZones();
	REQUIRE(zones.size() == static_cast<size_t>(ZONE_COUNT));
}

TEST_CASE("getZoneAt returns correct zone", "[gw][zone]")
{
	/// チュートリアルゾーン内の座標
	auto zone = getZoneAt({ -40.0f, 5.0f });
	REQUIRE(zone.has_value());
	REQUIRE(zone.value() == ZoneId::Tutorial);

	/// 指数の塔内の座標
	auto expZone = getZoneAt({ 45.0f, 20.0f });
	REQUIRE(expZone.has_value());
	REQUIRE(expZone.value() == ZoneId::Exponential);

	/// どのゾーンにも属さない遠方の座標
	auto none = getZoneAt({ -1000.0f, 1000.0f });
	REQUIRE_FALSE(none.has_value());
}
