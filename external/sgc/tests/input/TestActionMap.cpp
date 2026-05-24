#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include "sgc/input/ActionMap.hpp"
#include "sgc/core/Hash.hpp"

using namespace sgc::literals;

TEST_CASE("ActionMap bind and press detection", "[input][actionmap]")
{
	sgc::ActionMap actions;
	actions.bind("jump"_hash, 32); // space

	std::array<int, 1> keys = {32};
	actions.update(keys);

	REQUIRE(actions.isPressed("jump"_hash));
	REQUIRE(actions.isHeld("jump"_hash));
	REQUIRE_FALSE(actions.isReleased("jump"_hash));
}

TEST_CASE("ActionMap held state on second frame", "[input][actionmap]")
{
	sgc::ActionMap actions;
	actions.bind("jump"_hash, 32);

	std::array<int, 1> keys = {32};
	actions.update(keys); // Pressed
	actions.update(keys); // Held

	REQUIRE_FALSE(actions.isPressed("jump"_hash));
	REQUIRE(actions.isHeld("jump"_hash));
	REQUIRE(actions.state("jump"_hash) == sgc::ActionState::Held);
}

TEST_CASE("ActionMap release detection", "[input][actionmap]")
{
	sgc::ActionMap actions;
	actions.bind("jump"_hash, 32);

	std::array<int, 1> keys = {32};
	actions.update(keys); // Pressed

	std::vector<int> empty;
	actions.update(empty); // Released

	REQUIRE(actions.isReleased("jump"_hash));
	REQUIRE_FALSE(actions.isHeld("jump"_hash));
}

TEST_CASE("ActionMap none state after release", "[input][actionmap]")
{
	sgc::ActionMap actions;
	actions.bind("jump"_hash, 32);

	std::array<int, 1> keys = {32};
	actions.update(keys); // Pressed

	std::vector<int> empty;
	actions.update(empty); // Released
	actions.update(empty); // None

	REQUIRE(actions.state("jump"_hash) == sgc::ActionState::None);
}

TEST_CASE("ActionMap multiple bindings", "[input][actionmap]")
{
	sgc::ActionMap actions;
	actions.bind("jump"_hash, 32);
	actions.bind("jump"_hash, 'W');

	std::array<int, 1> keys = {'W'};
	actions.update(keys);

	REQUIRE(actions.isPressed("jump"_hash));
}

TEST_CASE("ActionMap multiple actions", "[input][actionmap]")
{
	sgc::ActionMap actions;
	actions.bind("jump"_hash, 32);
	actions.bind("shoot"_hash, 'Z');

	std::array<int, 2> keys = {32, 'Z'};
	actions.update(keys);

	REQUIRE(actions.isPressed("jump"_hash));
	REQUIRE(actions.isPressed("shoot"_hash));
}

TEST_CASE("ActionMap unbind removes action", "[input][actionmap]")
{
	sgc::ActionMap actions;
	actions.bind("jump"_hash, 32);
	actions.unbind("jump"_hash);

	std::array<int, 1> keys = {32};
	actions.update(keys);

	REQUIRE(actions.state("jump"_hash) == sgc::ActionState::None);
}

TEST_CASE("ActionMap unbound action returns None", "[input][actionmap]")
{
	sgc::ActionMap actions;
	REQUIRE(actions.state("unknown"_hash) == sgc::ActionState::None);
	REQUIRE_FALSE(actions.isPressed("unknown"_hash));
}

TEST_CASE("ActionMap update with no keys", "[input][actionmap]")
{
	sgc::ActionMap actions;
	actions.bind("jump"_hash, 32);

	std::vector<int> empty;
	actions.update(empty);

	REQUIRE(actions.state("jump"_hash) == sgc::ActionState::None);
}
