#include <catch2/catch_test_macros.hpp>

#include <mitiru/editor/EntityInspector.hpp>

namespace
{

/// テスト用のエンティティソース
class MockEntitySource : public mitiru::editor::IEntitySource
{
public:
	std::vector<mitiru::editor::EntityInfo> entities;

	[[nodiscard]] std::size_t getEntityCount() const override
	{
		return entities.size();
	}

	[[nodiscard]] mitiru::editor::EntityInfo getEntityInfo(std::size_t index) const override
	{
		return entities.at(index);
	}
};

} // anonymous namespace

TEST_CASE("EntityInspector getDisplayData with no source returns empty", "[mitiru][editor]")
{
	mitiru::editor::EntityInspector inspector;
	auto data = inspector.getDisplayData();
	REQUIRE(data.empty());
}

TEST_CASE("EntityInspector getDisplayData returns all entities", "[mitiru][editor]")
{
	MockEntitySource source;
	source.entities.push_back({1, "Player", {}, true});
	source.entities.push_back({2, "Enemy", {}, true});
	source.entities.push_back({3, "Item", {}, false});

	mitiru::editor::EntityInspector inspector;
	inspector.setSource(&source);

	auto data = inspector.getDisplayData();
	REQUIRE(data.size() == 3);
	REQUIRE(data[0].name == "Player");
	REQUIRE(data[1].name == "Enemy");
	REQUIRE(data[2].name == "Item");
}

TEST_CASE("EntityInspector name filter", "[mitiru][editor]")
{
	MockEntitySource source;
	source.entities.push_back({1, "Player", {}, true});
	source.entities.push_back({2, "PlayerBullet", {}, true});
	source.entities.push_back({3, "Enemy", {}, true});

	mitiru::editor::EntityInspector inspector;
	inspector.setSource(&source);
	inspector.setNameFilter("Player");

	auto data = inspector.getDisplayData();
	REQUIRE(data.size() == 2);
	REQUIRE(data[0].name == "Player");
	REQUIRE(data[1].name == "PlayerBullet");
}

TEST_CASE("EntityInspector component filter", "[mitiru][editor]")
{
	MockEntitySource source;

	mitiru::editor::EntityInfo e1;
	e1.id = 1;
	e1.name = "Player";
	e1.components.push_back({"Transform", {{"x", "0"}, {"y", "0"}}});
	e1.components.push_back({"Health", {{"hp", "100"}}});

	mitiru::editor::EntityInfo e2;
	e2.id = 2;
	e2.name = "Wall";
	e2.components.push_back({"Transform", {{"x", "10"}, {"y", "20"}}});

	source.entities.push_back(e1);
	source.entities.push_back(e2);

	mitiru::editor::EntityInspector inspector;
	inspector.setSource(&source);
	inspector.setComponentFilter("Health");

	auto data = inspector.getDisplayData();
	REQUIRE(data.size() == 1);
	REQUIRE(data[0].name == "Player");
}

TEST_CASE("EntityInspector selection", "[mitiru][editor]")
{
	mitiru::editor::EntityInspector inspector;

	REQUIRE_FALSE(inspector.selectedEntity().has_value());

	inspector.selectEntity(42);
	REQUIRE(inspector.selectedEntity().has_value());
	REQUIRE(*inspector.selectedEntity() == 42);

	inspector.clearSelection();
	REQUIRE_FALSE(inspector.selectedEntity().has_value());
}

TEST_CASE("EntityInspector getSelectedEntityInfo", "[mitiru][editor]")
{
	MockEntitySource source;
	source.entities.push_back({10, "Hero", {}, true});
	source.entities.push_back({20, "Villain", {}, true});

	mitiru::editor::EntityInspector inspector;
	inspector.setSource(&source);
	inspector.selectEntity(20);

	auto info = inspector.getSelectedEntityInfo();
	REQUIRE(info.has_value());
	REQUIRE(info->name == "Villain");
}

TEST_CASE("EntityInspector getSelectedEntityInfo returns nullopt for missing id", "[mitiru][editor]")
{
	MockEntitySource source;
	source.entities.push_back({10, "Hero", {}, true});

	mitiru::editor::EntityInspector inspector;
	inspector.setSource(&source);
	inspector.selectEntity(999);

	auto info = inspector.getSelectedEntityInfo();
	REQUIRE_FALSE(info.has_value());
}

TEST_CASE("EntityInspector filter accessors", "[mitiru][editor]")
{
	mitiru::editor::EntityInspector inspector;
	REQUIRE(inspector.nameFilter().empty());
	REQUIRE(inspector.componentFilter().empty());

	inspector.setNameFilter("test");
	inspector.setComponentFilter("Sprite");
	REQUIRE(inspector.nameFilter() == "test");
	REQUIRE(inspector.componentFilter() == "Sprite");
}

TEST_CASE("ComponentInfo stores fields correctly", "[mitiru][editor]")
{
	mitiru::editor::ComponentInfo comp;
	comp.name = "Transform";
	comp.fields.push_back({"x", "1.0"});
	comp.fields.push_back({"y", "2.0"});

	REQUIRE(comp.name == "Transform");
	REQUIRE(comp.fields.size() == 2);
	REQUIRE(comp.fields[0].first == "x");
	REQUIRE(comp.fields[1].second == "2.0");
}
