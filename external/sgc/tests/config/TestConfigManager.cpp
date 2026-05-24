/// @file TestConfigManager.cpp
/// @brief ConfigManager.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/config/ConfigManager.hpp"

using Catch::Approx;

TEST_CASE("ConfigManager set and get int", "[config]")
{
	sgc::ConfigManager config;
	config.set("width", 1920);

	auto val = config.getInt("width");
	REQUIRE(val.has_value());
	REQUIRE(val.value() == 1920);
}

TEST_CASE("ConfigManager set and get float", "[config]")
{
	sgc::ConfigManager config;
	config.set("volume", 0.75f);

	auto val = config.getFloat("volume");
	REQUIRE(val.has_value());
	REQUIRE(val.value() == Approx(0.75f));
}

TEST_CASE("ConfigManager set and get bool", "[config]")
{
	sgc::ConfigManager config;
	config.set("fullscreen", true);

	auto val = config.getBool("fullscreen");
	REQUIRE(val.has_value());
	REQUIRE(val.value() == true);
}

TEST_CASE("ConfigManager set and get string", "[config]")
{
	sgc::ConfigManager config;
	config.set("playerName", std::string("TestPlayer"));

	auto val = config.getString("playerName");
	REQUIRE(val.has_value());
	REQUIRE(val.value() == "TestPlayer");
}

TEST_CASE("ConfigManager get returns nullopt for missing key", "[config]")
{
	sgc::ConfigManager config;

	REQUIRE_FALSE(config.getInt("missing").has_value());
	REQUIRE_FALSE(config.getFloat("missing").has_value());
	REQUIRE_FALSE(config.getBool("missing").has_value());
	REQUIRE_FALSE(config.getString("missing").has_value());
}

TEST_CASE("ConfigManager get returns nullopt for wrong type", "[config]")
{
	sgc::ConfigManager config;
	config.set("value", 42);

	// 型が違えばnullopt
	REQUIRE_FALSE(config.getFloat("value").has_value());
	REQUIRE_FALSE(config.getBool("value").has_value());
	REQUIRE_FALSE(config.getString("value").has_value());
}

TEST_CASE("ConfigManager getXxxOr with defaults", "[config]")
{
	sgc::ConfigManager config;
	config.set("existing", 100);

	REQUIRE(config.getIntOr("existing", 0) == 100);
	REQUIRE(config.getIntOr("missing", 42) == 42);
	REQUIRE(config.getFloatOr("missing", 1.5f) == Approx(1.5f));
	REQUIRE(config.getBoolOr("missing", true) == true);
	REQUIRE(config.getStringOr("missing", "default") == "default");
}

TEST_CASE("ConfigManager hasKey", "[config]")
{
	sgc::ConfigManager config;
	config.set("key1", 1);

	REQUIRE(config.hasKey("key1"));
	REQUIRE_FALSE(config.hasKey("key2"));
}

TEST_CASE("ConfigManager remove", "[config]")
{
	sgc::ConfigManager config;
	config.set("key1", 1);
	config.set("key2", 2);

	REQUIRE(config.size() == 2);
	config.remove("key1");
	REQUIRE(config.size() == 1);
	REQUIRE_FALSE(config.hasKey("key1"));
	REQUIRE(config.hasKey("key2"));
}

TEST_CASE("ConfigManager clear", "[config]")
{
	sgc::ConfigManager config;
	config.set("a", 1);
	config.set("b", 2.0f);
	config.set("c", true);

	REQUIRE(config.size() == 3);
	config.clear();
	REQUIRE(config.size() == 0);
}

TEST_CASE("ConfigManager toJson serialization", "[config]")
{
	sgc::ConfigManager config;
	config.set("score", 100);

	std::string json = config.toJson();
	// JSON出力は{"score":100}の形式
	REQUIRE(json.find("\"score\"") != std::string::npos);
	REQUIRE(json.find("100") != std::string::npos);
}

TEST_CASE("ConfigManager toJson with multiple types", "[config]")
{
	sgc::ConfigManager config;
	config.set("name", std::string("test"));
	config.set("active", true);

	std::string json = config.toJson();
	REQUIRE(json.find("\"name\"") != std::string::npos);
	REQUIRE(json.find("\"test\"") != std::string::npos);
	REQUIRE(json.find("\"active\"") != std::string::npos);
	REQUIRE(json.find("true") != std::string::npos);
}

TEST_CASE("ConfigManager overwrite existing key", "[config]")
{
	sgc::ConfigManager config;
	config.set("key", 1);
	REQUIRE(config.getInt("key").value() == 1);

	config.set("key", 2);
	REQUIRE(config.getInt("key").value() == 2);
	REQUIRE(config.size() == 1);
}
