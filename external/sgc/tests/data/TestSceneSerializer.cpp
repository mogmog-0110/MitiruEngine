#include <catch2/catch_test_macros.hpp>
#include <sgc/data/SceneSerializer.hpp>

using namespace sgc::data;

TEST_CASE("SceneSerializer - serialize empty scene", "[data]")
{
	SerializedScene scene;
	const auto text = serializeScene(scene);
	REQUIRE(text.find("[scene]") != std::string::npos);
}

TEST_CASE("SceneSerializer - serialize metadata", "[data]")
{
	SerializedScene scene;
	scene.metadata["name"] = "TestLevel";
	scene.metadata["version"] = "1";

	const auto text = serializeScene(scene);
	REQUIRE(text.find("name = \"TestLevel\"") != std::string::npos);
	REQUIRE(text.find("version = \"1\"") != std::string::npos);
}

TEST_CASE("SceneSerializer - serialize entity with components", "[data]")
{
	SerializedScene scene;
	SerializedEntity entity;
	entity.name = "Player";
	entity.components["Transform"] = {{"x", "100"}, {"y", "200"}};
	entity.components["Sprite"] = {{"texture", "player.png"}};
	scene.entities.push_back(std::move(entity));

	const auto text = serializeScene(scene);
	REQUIRE(text.find("[entity \"Player\"]") != std::string::npos);
	REQUIRE(text.find("[Transform]") != std::string::npos);
	REQUIRE(text.find("x = \"100\"") != std::string::npos);
	REQUIRE(text.find("[Sprite]") != std::string::npos);
	REQUIRE(text.find("texture = \"player.png\"") != std::string::npos);
}

TEST_CASE("SceneSerializer - roundtrip serialize/deserialize", "[data]")
{
	SerializedScene original;
	original.metadata["name"] = "Level1";
	original.metadata["version"] = "2";

	SerializedEntity e1;
	e1.name = "Player";
	e1.components["Transform"] = {{"x", "10"}, {"y", "20"}};
	e1.components["Health"] = {{"hp", "100"}, {"max", "100"}};
	original.entities.push_back(std::move(e1));

	SerializedEntity e2;
	e2.name = "Enemy";
	e2.components["Transform"] = {{"x", "50"}, {"y", "60"}};
	original.entities.push_back(std::move(e2));

	const auto text = serializeScene(original);
	const auto restored = deserializeScene(text);

	REQUIRE(restored.metadata.at("name") == "Level1");
	REQUIRE(restored.metadata.at("version") == "2");
	REQUIRE(restored.entities.size() == 2);
	REQUIRE(restored.entities[0].name == "Player");
	REQUIRE(restored.entities[0].components.at("Transform").at("x") == "10");
	REQUIRE(restored.entities[0].components.at("Health").at("hp") == "100");
	REQUIRE(restored.entities[1].name == "Enemy");
	REQUIRE(restored.entities[1].components.at("Transform").at("x") == "50");
}

TEST_CASE("SceneSerializer - escape special characters", "[data]")
{
	REQUIRE(escapeString("hello\"world") == "hello\\\"world");
	REQUIRE(escapeString("line\nnew") == "line\\nnew");
	REQUIRE(escapeString("tab\there") == "tab\\there");
	REQUIRE(escapeString("back\\slash") == "back\\\\slash");
}

TEST_CASE("SceneSerializer - unescape special characters", "[data]")
{
	REQUIRE(unescapeString("hello\\\"world") == "hello\"world");
	REQUIRE(unescapeString("line\\nnew") == "line\nnew");
	REQUIRE(unescapeString("tab\\there") == "tab\there");
	REQUIRE(unescapeString("back\\\\slash") == "back\\slash");
}

TEST_CASE("SceneSerializer - trim whitespace", "[data]")
{
	REQUIRE(trim("  hello  ") == "hello");
	REQUIRE(trim("\t\n test \r\n") == "test");
	REQUIRE(trim("") == "");
	REQUIRE(trim("   ") == "");
}

TEST_CASE("SceneSerializer - deserialize empty input", "[data]")
{
	const auto scene = deserializeScene("");
	REQUIRE(scene.metadata.empty());
	REQUIRE(scene.entities.empty());
}

TEST_CASE("SceneSerializer - multiple entities roundtrip", "[data]")
{
	SerializedScene original;
	for (int i = 0; i < 5; ++i)
	{
		SerializedEntity e;
		e.name = "Entity" + std::to_string(i);
		e.components["Comp"] = {{"id", std::to_string(i)}};
		original.entities.push_back(std::move(e));
	}

	const auto text = serializeScene(original);
	const auto restored = deserializeScene(text);

	REQUIRE(restored.entities.size() == 5);
	for (int i = 0; i < 5; ++i)
	{
		REQUIRE(restored.entities[static_cast<size_t>(i)].name == "Entity" + std::to_string(i));
		REQUIRE(restored.entities[static_cast<size_t>(i)].components.at("Comp").at("id") == std::to_string(i));
	}
}

TEST_CASE("SceneSerializer - entity name with special chars", "[data]")
{
	SerializedScene original;
	SerializedEntity e;
	e.name = "Player \"Hero\"";
	e.components["Tag"] = {{"label", "test"}};
	original.entities.push_back(std::move(e));

	const auto text = serializeScene(original);
	const auto restored = deserializeScene(text);

	REQUIRE(restored.entities.size() == 1);
	REQUIRE(restored.entities[0].name == "Player \"Hero\"");
}
