#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/graphwalker/GraphArea.hpp"
#include "mitiru/graphwalker/FormulaSystem.hpp"
#include "mitiru/graphwalker/WorldMap.hpp"
#include "mitiru/graphwalker/InputManager.hpp"

using namespace mitiru::gw;
using Catch::Approx;

// ============================================================================
// GraphArea
// ============================================================================

TEST_CASE("GraphArea logicToWorld converts correctly", "[graphwalker][grapharea]")
{
	GraphArea area{60.0f, {400.0f, 300.0f}};

	// (0,0) -> origin
	auto w0 = area.logicToWorld({0.0f, 0.0f});
	REQUIRE(w0.x == Approx(400.0f));
	REQUIRE(w0.y == Approx(300.0f));

	// (1,0) -> origin.x + 60
	auto w1 = area.logicToWorld({1.0f, 0.0f});
	REQUIRE(w1.x == Approx(460.0f));
	REQUIRE(w1.y == Approx(300.0f));

	// (0,1) -> origin.y - 60 (Y flipped)
	auto w2 = area.logicToWorld({0.0f, 1.0f});
	REQUIRE(w2.x == Approx(400.0f));
	REQUIRE(w2.y == Approx(240.0f));
}

TEST_CASE("GraphArea worldToLogic is inverse of logicToWorld", "[graphwalker][grapharea]")
{
	GraphArea area{80.0f, {320.0f, 240.0f}};
	sgc::Vec2f logicPt{2.5f, -1.0f};

	auto world = area.logicToWorld(logicPt);
	auto back = area.worldToLogic(world);

	REQUIRE(back.x == Approx(logicPt.x));
	REQUIRE(back.y == Approx(logicPt.y));
}

TEST_CASE("GraphArea scalar logicToWorld conversions", "[graphwalker][grapharea]")
{
	GraphArea area{50.0f, {100.0f, 200.0f}};

	REQUIRE(area.logicToWorld(2.0f) == Approx(200.0f));
	REQUIRE(area.logicToWorldY(3.0f) == Approx(50.0f));
}

TEST_CASE("GraphArea getGridLines returns lines", "[graphwalker][grapharea]")
{
	GraphArea area{60.0f, {300.0f, 300.0f}};

	// view from (0,0) to (600,600) in world coords
	auto lines = area.getGridLines({0.0f, 0.0f}, {600.0f, 600.0f}, 1.0f);

	// Should have both vertical and horizontal lines
	REQUIRE(lines.size() > 0);

	// Each line should have two endpoints
	for (const auto& [p1, p2] : lines)
	{
		// At least one coordinate should differ
		REQUIRE((p1.x != p2.x || p1.y != p2.y));
	}
}

TEST_CASE("GraphArea setPixelsPerUnit works", "[graphwalker][grapharea]")
{
	GraphArea area{60.0f};
	REQUIRE(area.getPixelsPerUnit() == Approx(60.0f));

	area.setPixelsPerUnit(120.0f);
	REQUIRE(area.getPixelsPerUnit() == Approx(120.0f));

	// Negative value should be rejected
	area.setPixelsPerUnit(-10.0f);
	REQUIRE(area.getPixelsPerUnit() == Approx(120.0f));
}

// ============================================================================
// FormulaSystem
// ============================================================================

TEST_CASE("FormulaSystem evaluate x produces linear curve", "[graphwalker][formula]")
{
	FormulaSystem system;
	system.setGraphArea(GraphArea{60.0f, {0.0f, 0.0f}});

	auto result = system.evaluate("x", -2.0f, 2.0f, 5);

	REQUIRE(result.valid);
	REQUIRE(result.curvePoints.size() == 5);
	REQUIRE(result.error.empty());
}

TEST_CASE("FormulaSystem evaluate x^2 produces parabola", "[graphwalker][formula]")
{
	FormulaSystem system;
	GraphArea area{1.0f, {0.0f, 0.0f}}; // 1:1 scale, origin at (0,0)
	system.setGraphArea(area);

	auto result = system.evaluate("x^2", -2.0f, 2.0f, 5);

	REQUIRE(result.valid);
	REQUIRE(result.curvePoints.size() == 5);

	// x=-2 -> y=4, worldY = 0 - 4*1 = -4
	// x=0  -> y=0, worldY = 0
	// x=2  -> y=4, worldY = -4
	// First and last should have same Y (parabola symmetry)
	REQUIRE(result.curvePoints.front().y == Approx(result.curvePoints.back().y).margin(0.01f));
}

TEST_CASE("FormulaSystem evaluate sin(x) produces sine wave", "[graphwalker][formula]")
{
	FormulaSystem system;
	GraphArea area{1.0f, {0.0f, 0.0f}};
	system.setGraphArea(area);

	auto result = system.evaluate("sin(x)", 0.0f, 6.28318f, 100);

	REQUIRE(result.valid);
	REQUIRE(result.curvePoints.size() == 100);

	// sin(0) should map near world y=0
	REQUIRE(result.curvePoints.front().y == Approx(0.0f).margin(0.1f));
}

TEST_CASE("FormulaSystem evaluate invalid expression returns error", "[graphwalker][formula]")
{
	FormulaSystem system;
	auto result = system.evaluate("+++", -1.0f, 1.0f);

	REQUIRE_FALSE(result.valid);
	REQUIRE_FALSE(result.error.empty());
	REQUIRE(result.curvePoints.empty());
}

TEST_CASE("FormulaSystem isValid checks syntax", "[graphwalker][formula]")
{
	FormulaSystem system;

	REQUIRE(system.isValid("x+1"));
	REQUIRE(system.isValid("sin(x)"));
	REQUIRE_FALSE(system.isValid(""));
	REQUIRE_FALSE(system.isValid("***"));
}

TEST_CASE("FormulaSystem generatePlatformSegments produces rects", "[graphwalker][formula]")
{
	FormulaSystem system;
	system.setGraphArea(GraphArea{1.0f, {0.0f, 0.0f}});

	auto result = system.evaluate("x", 0.0f, 4.0f, 5);
	REQUIRE(result.valid);

	auto segments = FormulaSystem::generatePlatformSegments(result, 10.0f);

	// 5 points -> 4 segments
	REQUIRE(segments.size() == 4);

	// Each segment should have positive dimensions
	for (const auto& seg : segments)
	{
		REQUIRE(seg.size.x > 0.0f);
		REQUIRE(seg.size.y > 0.0f);
	}
}

// ============================================================================
// WorldMap
// ============================================================================

TEST_CASE("WorldMapLoader createDefaultMap is valid", "[graphwalker][worldmap]")
{
	WorldMapLoader loader;
	auto map = loader.createDefaultMap();

	REQUIRE(map.platforms.size() >= 3);
	REQUIRE(map.collectibles.size() >= 1);
	REQUIRE(map.checkpoints.size() >= 1);
	REQUIRE(map.startPos.x > 0.0f);
	REQUIRE(map.startPos.y > 0.0f);
}

TEST_CASE("WorldMapLoader save and load roundtrip", "[graphwalker][worldmap]")
{
	WorldMapLoader loader;
	auto original = loader.createDefaultMap();

	auto json = loader.saveToJson(original);
	REQUIRE_FALSE(json.empty());

	auto loaded = loader.loadFromJson(json);
	REQUIRE(loaded.has_value());

	auto& map = *loaded;
	REQUIRE(map.startPos.x == Approx(original.startPos.x));
	REQUIRE(map.startPos.y == Approx(original.startPos.y));
	REQUIRE(map.platforms.size() == original.platforms.size());
	REQUIRE(map.collectibles.size() == original.collectibles.size());
	REQUIRE(map.checkpoints.size() == original.checkpoints.size());

	// 内容の検証
	REQUIRE(map.platforms[0].type == "static");
	REQUIRE(map.collectibles[0].buttonId == "*");
	REQUIRE(map.checkpoints[0].id == "cp1");
}

// ============================================================================
// InputManager
// ============================================================================

TEST_CASE("InputManager pressed detection", "[graphwalker][input]")
{
	InputManager input;

	// フレーム1: キーなし
	RawInputState raw1;
	input.update(raw1);
	REQUIRE_FALSE(input.getState().isPressed(GameAction::Jump));
	REQUIRE_FALSE(input.getState().isHeld(GameAction::Jump));

	// フレーム2: Spaceを押す
	RawInputState raw2;
	raw2.keysDown.insert(0x20); // Space
	input.update(raw2);
	REQUIRE(input.getState().isPressed(GameAction::Jump));
	REQUIRE(input.getState().isHeld(GameAction::Jump));
	REQUIRE_FALSE(input.getState().isReleased(GameAction::Jump));
}

TEST_CASE("InputManager held detection", "[graphwalker][input]")
{
	InputManager input;

	// フレーム1: Spaceを押す
	RawInputState raw1;
	raw1.keysDown.insert(0x20);
	input.update(raw1);
	REQUIRE(input.getState().isPressed(GameAction::Jump));

	// フレーム2: Spaceを押し続ける
	RawInputState raw2;
	raw2.keysDown.insert(0x20);
	input.update(raw2);
	REQUIRE_FALSE(input.getState().isPressed(GameAction::Jump)); // もうpressedではない
	REQUIRE(input.getState().isHeld(GameAction::Jump));

	// フレーム3: Spaceを離す
	RawInputState raw3;
	input.update(raw3);
	REQUIRE_FALSE(input.getState().isHeld(GameAction::Jump));
	REQUIRE(input.getState().isReleased(GameAction::Jump));
}

TEST_CASE("InputManager rebind works", "[graphwalker][input]")
{
	InputManager input;

	// Jumpを'W'(0x57)にリバインド
	input.rebind(GameAction::Jump, 0x57);
	REQUIRE(input.getBinding(GameAction::Jump) == 0x57);

	// 旧キー(Space)ではジャンプしない
	RawInputState raw1;
	raw1.keysDown.insert(0x20);
	input.update(raw1);
	REQUIRE_FALSE(input.getState().isPressed(GameAction::Jump));

	// 新キー(W)でジャンプする
	RawInputState raw2;
	raw2.keysDown.insert(0x57);
	input.update(raw2);
	REQUIRE(input.getState().isPressed(GameAction::Jump));
}

TEST_CASE("InputManager secondary key binding", "[graphwalker][input]")
{
	InputManager input;

	// Left arrowはMoveLeftのセカンダリバインド
	RawInputState raw;
	raw.keysDown.insert(0x25); // Left arrow
	input.update(raw);
	REQUIRE(input.getState().isPressed(GameAction::MoveLeft));
}

// ============================================================================
// UnlockedButtons
// ============================================================================

TEST_CASE("UnlockedButtons tracks available operators", "[graphwalker][formula]")
{
	UnlockedButtons buttons;

	// 初期状態: 何もアンロックされていない
	REQUIRE_FALSE(buttons.isUnlocked("*"));
	REQUIRE_FALSE(buttons.isUnlocked("sin"));

	// アンロック
	buttons.unlock("*");
	buttons.unlock("sin");

	REQUIRE(buttons.isUnlocked("*"));
	REQUIRE(buttons.isUnlocked("sin"));
	REQUIRE_FALSE(buttons.isUnlocked("/"));

	// getAvailableOperatorsで反映される
	auto ops = FormulaSystem::getAvailableOperators(buttons);

	// デフォルト15個 + アンロック2個 = 17個
	REQUIRE(ops.size() == 17);

	// "*"と"sin"が含まれる
	bool hasMul = false, hasSin = false;
	for (const auto& op : ops)
	{
		if (op == "*") hasMul = true;
		if (op == "sin") hasSin = true;
	}
	REQUIRE(hasMul);
	REQUIRE(hasSin);
}
