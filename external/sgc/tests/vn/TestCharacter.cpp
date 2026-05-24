#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "sgc/vn/CharacterManager.hpp"

using namespace sgc::vn;
using Catch::Approx;

namespace
{

CharacterDef makeTestCharacter(const std::string& id, const std::string& name)
{
	CharacterDef def;
	def.id = id;
	def.displayName = name;
	def.expressions["happy"] = 1;
	def.expressions["sad"] = 2;
	return def;
}

} // namespace

TEST_CASE("CharacterManager - Register character increases count", "[vn][character]")
{
	CharacterManager mgr;
	REQUIRE(mgr.registeredCount() == 0);

	mgr.registerCharacter(makeTestCharacter("sakura", "Sakura"));
	REQUIRE(mgr.registeredCount() == 1);

	mgr.registerCharacter(makeTestCharacter("taro", "Taro"));
	REQUIRE(mgr.registeredCount() == 2);
}

TEST_CASE("CharacterManager - Show character makes it visible", "[vn][character]")
{
	CharacterManager mgr;
	mgr.registerCharacter(makeTestCharacter("sakura", "Sakura"));

	REQUIRE(mgr.visibleCount() == 0);
	mgr.show("sakura", CharacterPosition::Left, "happy");
	REQUIRE(mgr.visibleCount() == 1);

	auto state = mgr.getState("sakura");
	REQUIRE(state.has_value());
	REQUIRE(state->visible);
	REQUIRE(state->position == CharacterPosition::Left);
	REQUIRE(state->expression == "happy");
}

TEST_CASE("CharacterManager - Hide character removes from visible", "[vn][character]")
{
	CharacterManager mgr;
	mgr.registerCharacter(makeTestCharacter("sakura", "Sakura"));
	mgr.show("sakura", CharacterPosition::Center);

	REQUIRE(mgr.visibleCount() == 1);
	mgr.hide("sakura");
	REQUIRE(mgr.visibleCount() == 0);

	auto state = mgr.getState("sakura");
	REQUIRE(state.has_value());
	REQUIRE_FALSE(state->visible);
}

TEST_CASE("CharacterManager - Set expression changes state", "[vn][character]")
{
	CharacterManager mgr;
	mgr.registerCharacter(makeTestCharacter("sakura", "Sakura"));
	mgr.show("sakura", CharacterPosition::Center);

	mgr.setExpression("sakura", "sad");

	auto state = mgr.getState("sakura");
	REQUIRE(state.has_value());
	REQUIRE(state->expression == "sad");
}

TEST_CASE("CharacterManager - Set position changes state", "[vn][character]")
{
	CharacterManager mgr;
	mgr.registerCharacter(makeTestCharacter("sakura", "Sakura"));
	mgr.show("sakura", CharacterPosition::Center);

	mgr.setPosition("sakura", CharacterPosition::Right);

	auto state = mgr.getState("sakura");
	REQUIRE(state.has_value());
	REQUIRE(state->position == CharacterPosition::Right);
}

TEST_CASE("CharacterManager - Set alpha changes state", "[vn][character]")
{
	CharacterManager mgr;
	mgr.registerCharacter(makeTestCharacter("sakura", "Sakura"));
	mgr.show("sakura", CharacterPosition::Center);

	mgr.setAlpha("sakura", 0.5f);

	auto state = mgr.getState("sakura");
	REQUIRE(state.has_value());
	REQUIRE(state->alpha == Approx(0.5f));
}

TEST_CASE("CharacterManager - HideAll clears all visible", "[vn][character]")
{
	CharacterManager mgr;
	mgr.registerCharacter(makeTestCharacter("sakura", "Sakura"));
	mgr.registerCharacter(makeTestCharacter("taro", "Taro"));
	mgr.show("sakura", CharacterPosition::Left);
	mgr.show("taro", CharacterPosition::Right);

	REQUIRE(mgr.visibleCount() == 2);
	mgr.hideAll();
	REQUIRE(mgr.visibleCount() == 0);
}

TEST_CASE("CharacterManager - GetState for unknown returns nullopt", "[vn][character]")
{
	CharacterManager mgr;
	auto state = mgr.getState("nonexistent");
	REQUIRE_FALSE(state.has_value());
}

TEST_CASE("CharacterManager - Show unregistered character is ignored", "[vn][character]")
{
	CharacterManager mgr;
	mgr.show("unknown", CharacterPosition::Center);
	REQUIRE(mgr.visibleCount() == 0);
}

TEST_CASE("CharacterManager - visibleCharacters returns correct list", "[vn][character]")
{
	CharacterManager mgr;
	mgr.registerCharacter(makeTestCharacter("sakura", "Sakura"));
	mgr.registerCharacter(makeTestCharacter("taro", "Taro"));
	mgr.show("sakura", CharacterPosition::Left);
	mgr.show("taro", CharacterPosition::Right);

	auto visible = mgr.visibleCharacters();
	REQUIRE(visible.size() == 2);
}
