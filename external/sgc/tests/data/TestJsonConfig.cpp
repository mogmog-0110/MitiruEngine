#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sgc/data/JsonConfig.hpp>
#include <cstdio>

using namespace sgc::data;
using Catch::Matchers::WithinAbs;

namespace
{

/// @brief テスト用の設定構造体
struct TestConfig
{
	double speed = 100.0;
	int lives = 3;
	std::string name = "default";

	DataNode toDataNode() const
	{
		auto node = DataNode::object();
		node["speed"] = DataNode(speed);
		node["lives"] = DataNode(static_cast<int64_t>(lives));
		node["name"] = DataNode(name);
		return node;
	}

	void fromDataNode(const DataNode& node)
	{
		speed = node["speed"].asFloat(speed);
		lives = static_cast<int>(node["lives"].asInt(lives));
		name = node["name"].asString(name);
	}
};

/// @brief テスト用一時ファイル管理
struct TempFile
{
	std::string path;
	explicit TempFile(const std::string& name) : path{name} {}
	~TempFile() { std::remove(path.c_str()); }
};

} // anonymous namespace

TEST_CASE("JsonConfig - save and load round trip", "[data][json-config]")
{
	TempFile tmp("test_config_roundtrip.json");

	{
		JsonConfig<TestConfig> config(tmp.path);
		config.data().speed = 250.0;
		config.data().lives = 5;
		config.data().name = "hero";
		REQUIRE(config.save());
	}

	{
		JsonConfig<TestConfig> config(tmp.path);
		REQUIRE(config.load());
		REQUIRE_THAT(config.data().speed, WithinAbs(250.0, 0.01));
		REQUIRE(config.data().lives == 5);
		REQUIRE(config.data().name == "hero");
	}
}

TEST_CASE("JsonConfig - load missing file returns false", "[data][json-config]")
{
	JsonConfig<TestConfig> config("nonexistent_file_xyz.json");
	REQUIRE_FALSE(config.load());
	// デフォルト値が維持される
	REQUIRE_THAT(config.data().speed, WithinAbs(100.0, 0.01));
}

TEST_CASE("JsonConfig - resetToDefault restores defaults", "[data][json-config]")
{
	JsonConfig<TestConfig> config("unused.json");
	config.data().speed = 999.0;
	config.data().lives = 99;
	config.resetToDefault();
	REQUIRE_THAT(config.data().speed, WithinAbs(100.0, 0.01));
	REQUIRE(config.data().lives == 3);
}

TEST_CASE("JsonConfig - filePath accessors", "[data][json-config]")
{
	JsonConfig<TestConfig> config("original.json");
	REQUIRE(config.filePath() == "original.json");

	config.setFilePath("updated.json");
	REQUIRE(config.filePath() == "updated.json");
}

TEST_CASE("DataNode - loadFromFile with valid JSON", "[data][datanode-file]")
{
	TempFile tmp("test_datanode_load.json");

	// ファイルを書き出す
	{
		auto node = DataNode::object();
		node["x"] = DataNode(static_cast<int64_t>(42));
		node["name"] = DataNode("test");
		REQUIRE(node.saveToFile(tmp.path));
	}

	// ファイルから読み込む
	auto loaded = DataNode::loadFromFile(tmp.path);
	REQUIRE(loaded.isObject());
	REQUIRE(loaded["x"].asInt() == 42);
	REQUIRE(loaded["name"].asString() == "test");
}

TEST_CASE("DataNode - loadFromFile missing file returns null", "[data][datanode-file]")
{
	auto node = DataNode::loadFromFile("no_such_file.json");
	REQUIRE(node.isNull());
}

TEST_CASE("DataNode - saveToFile creates valid JSON", "[data][datanode-file]")
{
	TempFile tmp("test_datanode_save.json");

	auto node = DataNode::object();
	node["arr"] = DataNode::array({DataNode(static_cast<int64_t>(1)), DataNode(static_cast<int64_t>(2)), DataNode(static_cast<int64_t>(3))});
	node["nested"] = DataNode::object();
	node["nested"]["val"] = DataNode(3.14);

	REQUIRE(node.saveToFile(tmp.path, true));

	// 再読み込みで検証
	auto loaded = DataNode::loadFromFile(tmp.path);
	REQUIRE(loaded["arr"].size() == 3);
	REQUIRE(loaded["arr"][static_cast<size_t>(0)].asInt() == 1);
	REQUIRE_THAT(loaded["nested"]["val"].asFloat(), WithinAbs(3.14, 0.001));
}
