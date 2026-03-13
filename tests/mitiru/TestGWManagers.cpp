#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "mitiru/graphwalker/CalculatorHUD.hpp"
#include "mitiru/graphwalker/MinimapData.hpp"
#include "mitiru/graphwalker/WorldManager.hpp"
#include "mitiru/graphwalker/FormulaManager.hpp"
#include "mitiru/graphwalker/AchievementSystem.hpp"
#include "mitiru/graphwalker/TutorialManager.hpp"

using namespace mitiru::gw;

// ============================================================
// CalculatorHUD
// ============================================================

TEST_CASE("CalculatorHUD open/close/toggle", "[gw][calculator]")
{
	CalculatorHUD hud;
	REQUIRE_FALSE(hud.isOpen());

	hud.open();
	REQUIRE(hud.isOpen());

	hud.close();
	REQUIRE_FALSE(hud.isOpen());

	hud.toggle();
	REQUIRE(hud.isOpen());

	hud.toggle();
	REQUIRE_FALSE(hud.isOpen());
}

TEST_CASE("CalculatorHUD pressButton builds expression", "[gw][calculator]")
{
	CalculatorHUD hud;
	hud.pressButton("3");
	hud.pressButton("+");
	hud.pressButton("x");
	REQUIRE(hud.getExpression() == "3+x");
}

TEST_CASE("CalculatorHUD submit returns expression and adds to history", "[gw][calculator]")
{
	CalculatorHUD hud;
	hud.pressButton("x");
	hud.pressButton("^");
	hud.pressButton("2");

	const auto result = hud.submit();
	REQUIRE(result == "x^2");
	REQUIRE(hud.getExpression().empty());
	REQUIRE(hud.getHistory().size() == 1);
	REQUIRE(hud.getHistory()[0] == "x^2");
}

TEST_CASE("CalculatorHUD submit empty does not add to history", "[gw][calculator]")
{
	CalculatorHUD hud;
	const auto result = hud.submit();
	REQUIRE(result.empty());
	REQUIRE(hud.getHistory().empty());
}

TEST_CASE("CalculatorHUD backspace removes last char", "[gw][calculator]")
{
	CalculatorHUD hud;
	hud.pressButton("1");
	hud.pressButton("2");
	hud.pressButton("3");
	hud.backspace();
	REQUIRE(hud.getExpression() == "12");

	// backspace on empty is safe
	hud.clear();
	hud.backspace();
	REQUIRE(hud.getExpression().empty());
}

TEST_CASE("CalculatorHUD getButtons returns layout with unlock state", "[gw][calculator]")
{
	CalculatorHUD hud;

	// empty unlocked set -> all buttons unlocked
	UnlockedButtons empty;
	auto allButtons = hud.getButtons(empty);
	REQUIRE(allButtons.size() == 25); // 5x5 grid
	REQUIRE(allButtons[0].isUnlocked);

	// limited unlock set
	UnlockedButtons limited = {"1", "2", "3", "+", "="};
	auto limitedButtons = hud.getButtons(limited);
	bool foundLocked = false;
	bool foundUnlocked = false;
	for (const auto& btn : limitedButtons)
	{
		if (btn.value == "1") foundUnlocked = true;
		if (btn.value == "sin(" && !btn.isUnlocked) foundLocked = true;
	}
	REQUIRE(foundUnlocked);
	REQUIRE(foundLocked);
}

TEST_CASE("CalculatorHUD position set/get", "[gw][calculator]")
{
	CalculatorHUD hud;
	hud.setPosition({200.0f, 300.0f});
	const auto pos = hud.getPosition();
	REQUIRE(pos.x == Catch::Approx(200.0f));
	REQUIRE(pos.y == Catch::Approx(300.0f));
}

// ============================================================
// MinimapData
// ============================================================

TEST_CASE("MinimapData visitChunk marks visited", "[gw][minimap]")
{
	MinimapData minimap;
	minimap.visitChunk({150.0f, 320.0f});

	// chunk size 200 -> (150/200, 320/200) = (0, 1)
	REQUIRE(minimap.isVisited({0, 1}));
	REQUIRE_FALSE(minimap.isVisited({1, 0}));
}

TEST_CASE("MinimapData exploration percent", "[gw][minimap]")
{
	MinimapData minimap;
	minimap.visitChunk({0.0f, 0.0f});
	minimap.visitChunk({200.0f, 0.0f});
	minimap.visitChunk({400.0f, 0.0f});

	REQUIRE(minimap.getExplorationPercent(10) == Catch::Approx(0.3f));
	REQUIRE(minimap.getExplorationPercent(0) == Catch::Approx(0.0f));
}

TEST_CASE("MinimapData getBounds returns correct area", "[gw][minimap]")
{
	MinimapData minimap;
	minimap.visitChunk({100.0f, 100.0f}); // chunk (0,0)
	minimap.visitChunk({500.0f, 500.0f}); // chunk (2,2)

	auto [minPt, maxPt] = minimap.getBounds();
	REQUIRE(minPt.x == Catch::Approx(0.0f));
	REQUIRE(minPt.y == Catch::Approx(0.0f));
	REQUIRE(maxPt.x == Catch::Approx(3.0f));
	REQUIRE(maxPt.y == Catch::Approx(3.0f));
}

TEST_CASE("MinimapData clear resets all data", "[gw][minimap]")
{
	MinimapData minimap;
	minimap.visitChunk({0.0f, 0.0f});
	minimap.clear();
	REQUIRE(minimap.getVisitedChunks().empty());
}

// ============================================================
// WorldManager
// ============================================================

TEST_CASE("WorldManager buildWorld creates objects", "[gw][world]")
{
	WorldManager wm;
	SimpleMapData map;
	map.playerSpawn = {100.0f, 200.0f};

	SharedData shared;

	wm.buildWorld(map, shared);
	REQUIRE(wm.objectCount() == 1); // only player
	REQUIRE(wm.getPlayer() != nullptr);
	REQUIRE(wm.getPlayer()->getPosition().x == Catch::Approx(100.0f));
	wm.clearWorld();
	REQUIRE(wm.objectCount() == 0);
}

TEST_CASE("WorldManager respawnPlayer resets position", "[gw][world]")
{
	WorldManager wm;
	SimpleMapData map;
	map.playerSpawn = {50.0f, 50.0f};
	SharedData shared;
	wm.buildWorld(map, shared);

	// move player away
	wm.getPlayer()->setPosition({999.0f, 999.0f});

	// respawn with no checkpoint -> returns to initial spawn
	wm.respawnPlayer(shared);
	REQUIRE(wm.getPlayer()->getPosition().x == Catch::Approx(50.0f));
	REQUIRE(wm.getPlayer()->getPosition().y == Catch::Approx(50.0f));

	// set checkpoint -> respawns there
	shared.currentCheckpointId = "cp1";
	shared.playerSpawnPos = {200.0f, 100.0f};
	wm.respawnPlayer(shared);
	REQUIRE(wm.getPlayer()->getPosition().x == Catch::Approx(200.0f));
	wm.clearWorld();
}

TEST_CASE("WorldManager collectItem updates SharedData", "[gw][world]")
{
	WorldManager wm;
	SimpleMapData map;
	map.playerSpawn = {0.0f, 0.0f};
	SharedData shared;
	wm.buildWorld(map, shared);

	wm.collectItem("btn_plus", shared);
	REQUIRE(shared.collectedItems.size() == 1);
	REQUIRE(shared.collectedItems.count("btn_plus") == 1);
	wm.clearWorld();
}

TEST_CASE("WorldManager findNearbyNpc returns closest", "[gw][world]")
{
	WorldManager wm;
	SimpleMapData map;
	map.playerSpawn = {0.0f, 0.0f};
	map.npcPositions.push_back({50.0f, 0.0f});
	map.npcPositions.push_back({500.0f, 0.0f});
	SharedData shared;
	wm.buildWorld(map, shared);

	auto* npc = wm.findNearbyNpc({40.0f, 0.0f}, 100.0f);
	REQUIRE(npc != nullptr);
	REQUIRE(npc->getPosition().x == Catch::Approx(50.0f));

	// out of range
	auto* farNpc = wm.findNearbyNpc({40.0f, 0.0f}, 5.0f);
	REQUIRE(farNpc == nullptr);
	wm.clearWorld();
}

// ============================================================
// FormulaManager
// ============================================================

TEST_CASE("FormulaManager submitFormula validates", "[gw][formula]")
{
	FormulaManager fm;

	// empty expression is error
	REQUIRE_FALSE(fm.submitFormula(""));
	REQUIRE(fm.getLastError() == "Empty expression");

	// without FormulaSystem, expression is still stored
	REQUIRE(fm.submitFormula("x^2"));
	REQUIRE(fm.getActiveFormulas().size() == 1);
	REQUIRE(fm.getActiveFormulas()[0].expression == "x^2");
}

TEST_CASE("FormulaManager active formula count limited", "[gw][formula]")
{
	FormulaManager fm;
	fm.setMaxActiveFormulas(2);

	REQUIRE(fm.submitFormula("x"));
	REQUIRE(fm.submitFormula("x+1"));
	REQUIRE_FALSE(fm.submitFormula("x+2")); // limit reached
	REQUIRE(fm.getActiveFormulas().size() == 2);
}

TEST_CASE("FormulaManager update expires formulas", "[gw][formula]")
{
	FormulaManager fm;
	fm.setFormulaLifetime(5.0f);
	fm.submitFormula("x");

	fm.update(3.0f);
	REQUIRE(fm.getActiveFormulas().size() == 1);

	fm.update(3.0f); // total 6s > lifetime 5s
	REQUIRE(fm.getActiveFormulas().empty());
}

// ============================================================
// AchievementSystem
// ============================================================

TEST_CASE("AchievementSystem define and unlock", "[gw][achievement]")
{
	AchievementSystem ach;
	SharedData shared;

	// builtin achievements are registered
	auto all = ach.getAll();
	REQUIRE(all.size() >= 5);

	// before unlock
	REQUIRE(ach.getUnlocked().empty());

	// unlock
	REQUIRE(ach.unlock("first_formula", shared));
	REQUIRE(ach.getUnlocked().size() == 1);

	// duplicate unlock returns false
	REQUIRE_FALSE(ach.unlock("first_formula", shared));
}

TEST_CASE("AchievementSystem getRecentUnlock", "[gw][achievement]")
{
	AchievementSystem ach;
	SharedData shared;

	REQUIRE_FALSE(ach.getRecentUnlock().has_value());

	ach.unlock("explorer_10", shared);
	auto recent = ach.getRecentUnlock();
	REQUIRE(recent.has_value());
	REQUIRE(recent->id == "explorer_10");

	ach.clearRecent();
	REQUIRE_FALSE(ach.getRecentUnlock().has_value());
}

TEST_CASE("AchievementSystem custom achievement", "[gw][achievement]")
{
	AchievementSystem ach;
	SharedData shared;

	ach.define({"custom_1", "Custom", "A custom achievement", false});
	REQUIRE(ach.unlock("custom_1", shared));

	auto unlocked = ach.getUnlocked();
	bool found = false;
	for (const auto& a : unlocked)
	{
		if (a.id == "custom_1") found = true;
	}
	REQUIRE(found);
}

// ============================================================
// TutorialManager
// ============================================================

TEST_CASE("TutorialManager step progression", "[gw][tutorial]")
{
	TutorialManager tut;

	REQUIRE_FALSE(tut.isComplete());
	REQUIRE(tut.stepCount() == 5); // 5 default steps

	auto hint = tut.getCurrentHint();
	REQUIRE(hint.has_value());
	REQUIRE(hint->find("WASD") != std::string::npos);
}

TEST_CASE("TutorialManager dismissCurrent advances", "[gw][tutorial]")
{
	TutorialManager tut;

	REQUIRE(tut.getCurrentIndex() == 0);
	tut.dismissCurrent();
	REQUIRE(tut.getCurrentIndex() == 1);

	// skip all steps
	while (!tut.isComplete())
	{
		tut.dismissCurrent();
	}
	REQUIRE(tut.isComplete());
	REQUIRE_FALSE(tut.getCurrentHint().has_value());

	// dismiss after complete is safe
	tut.dismissCurrent();
	REQUIRE(tut.isComplete());
}

TEST_CASE("TutorialManager reset restarts", "[gw][tutorial]")
{
	TutorialManager tut;
	tut.dismissCurrent();
	tut.dismissCurrent();
	REQUIRE(tut.getCurrentIndex() == 2);

	tut.reset();
	REQUIRE(tut.getCurrentIndex() == 0);
	REQUIRE_FALSE(tut.isComplete());
}
