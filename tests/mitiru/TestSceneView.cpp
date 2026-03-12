#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <mitiru/editor/SceneView.hpp>

TEST_CASE("SceneViewState default values", "[mitiru][editor]")
{
	mitiru::editor::SceneViewState state;
	REQUIRE(state.cameraPos.x == 0.0f);
	REQUIRE(state.cameraPos.y == 0.0f);
	REQUIRE(state.cameraZoom == 1.0f);
	REQUIRE(state.selectedEntities.empty());
	REQUIRE(state.gridVisible == true);
	REQUIRE(state.gridSize == 32.0f);
	REQUIRE(state.snapToGrid == false);
	REQUIRE(state.currentAction == mitiru::editor::SceneViewAction::None);
}

TEST_CASE("snapToGrid snaps to grid correctly", "[mitiru][editor]")
{
	auto result = mitiru::editor::snapToGrid({17.0f, 33.0f}, 32.0f);
	REQUIRE(result.x == Catch::Approx(32.0f));
	REQUIRE(result.y == Catch::Approx(32.0f));
}

TEST_CASE("snapToGrid with exact grid position", "[mitiru][editor]")
{
	auto result = mitiru::editor::snapToGrid({64.0f, 128.0f}, 32.0f);
	REQUIRE(result.x == Catch::Approx(64.0f));
	REQUIRE(result.y == Catch::Approx(128.0f));
}

TEST_CASE("snapToGrid with zero gridSize returns original", "[mitiru][editor]")
{
	auto result = mitiru::editor::snapToGrid({17.5f, 33.3f}, 0.0f);
	REQUIRE(result.x == Catch::Approx(17.5f));
	REQUIRE(result.y == Catch::Approx(33.3f));
}

TEST_CASE("snapToGrid with negative gridSize returns original", "[mitiru][editor]")
{
	auto result = mitiru::editor::snapToGrid({10.0f, 20.0f}, -5.0f);
	REQUIRE(result.x == Catch::Approx(10.0f));
	REQUIRE(result.y == Catch::Approx(20.0f));
}

TEST_CASE("SelectionRect contains point inside", "[mitiru][editor]")
{
	mitiru::editor::SelectionRect rect;
	rect.start = {10.0f, 10.0f};
	rect.end = {50.0f, 50.0f};
	REQUIRE(rect.contains(30.0f, 30.0f));
}

TEST_CASE("SelectionRect contains point outside", "[mitiru][editor]")
{
	mitiru::editor::SelectionRect rect;
	rect.start = {10.0f, 10.0f};
	rect.end = {50.0f, 50.0f};
	REQUIRE_FALSE(rect.contains(5.0f, 5.0f));
}

TEST_CASE("SelectionRect with reversed start/end still works", "[mitiru][editor]")
{
	mitiru::editor::SelectionRect rect;
	rect.start = {50.0f, 50.0f};
	rect.end = {10.0f, 10.0f};
	REQUIRE(rect.contains(30.0f, 30.0f));
	REQUIRE_FALSE(rect.contains(5.0f, 5.0f));
}

TEST_CASE("SelectionRect min/max accessors", "[mitiru][editor]")
{
	mitiru::editor::SelectionRect rect;
	rect.start = {50.0f, 20.0f};
	rect.end = {10.0f, 80.0f};
	REQUIRE(rect.minX() == Catch::Approx(10.0f));
	REQUIRE(rect.maxX() == Catch::Approx(50.0f));
	REQUIRE(rect.minY() == Catch::Approx(20.0f));
	REQUIRE(rect.maxY() == Catch::Approx(80.0f));
}

TEST_CASE("handleInput Ctrl+leftDown returns Scale", "[mitiru][editor]")
{
	mitiru::editor::SceneViewState state;
	mitiru::editor::MouseState mouse{true, false, false};
	mitiru::editor::KeyModifiers keys{false, true, false};

	auto action = mitiru::editor::handleInput(state, {100.0f, 100.0f}, mouse, keys);
	REQUIRE(action == mitiru::editor::SceneViewAction::Scale);
}

TEST_CASE("handleInput Alt+leftDown returns Rotate", "[mitiru][editor]")
{
	mitiru::editor::SceneViewState state;
	mitiru::editor::MouseState mouse{true, false, false};
	mitiru::editor::KeyModifiers keys{false, false, true};

	auto action = mitiru::editor::handleInput(state, {100.0f, 100.0f}, mouse, keys);
	REQUIRE(action == mitiru::editor::SceneViewAction::Rotate);
}

TEST_CASE("handleInput Shift+leftClicked returns Place", "[mitiru][editor]")
{
	mitiru::editor::SceneViewState state;
	mitiru::editor::MouseState mouse{false, false, true};
	mitiru::editor::KeyModifiers keys{true, false, false};

	auto action = mitiru::editor::handleInput(state, {100.0f, 100.0f}, mouse, keys);
	REQUIRE(action == mitiru::editor::SceneViewAction::Place);
}

TEST_CASE("handleInput leftDown with no selection starts Select", "[mitiru][editor]")
{
	mitiru::editor::SceneViewState state;
	mitiru::editor::MouseState mouse{true, false, false};
	mitiru::editor::KeyModifiers keys{};

	auto action = mitiru::editor::handleInput(state, {100.0f, 100.0f}, mouse, keys);
	REQUIRE(action == mitiru::editor::SceneViewAction::Select);
	REQUIRE(state.selectionRect.active);
}

TEST_CASE("handleInput leftDown with selection returns Move", "[mitiru][editor]")
{
	mitiru::editor::SceneViewState state;
	state.selectedEntities.push_back(1);
	mitiru::editor::MouseState mouse{true, false, false};
	mitiru::editor::KeyModifiers keys{};

	auto action = mitiru::editor::handleInput(state, {100.0f, 100.0f}, mouse, keys);
	REQUIRE(action == mitiru::editor::SceneViewAction::Move);
}

TEST_CASE("handleInput no input returns None", "[mitiru][editor]")
{
	mitiru::editor::SceneViewState state;
	mitiru::editor::MouseState mouse{};
	mitiru::editor::KeyModifiers keys{};

	auto action = mitiru::editor::handleInput(state, {100.0f, 100.0f}, mouse, keys);
	REQUIRE(action == mitiru::editor::SceneViewAction::None);
}
