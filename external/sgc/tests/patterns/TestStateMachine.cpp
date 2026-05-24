/// @file TestStateMachine.cpp
/// @brief StateMachine.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/patterns/StateMachine.hpp"

#include <optional>
#include <string>

namespace
{
struct Idle {};
struct Walking { float speed; };
struct Jumping { float velocity; };

using PlayerFSM = sgc::StateMachine<Idle, Walking, Jumping>;
} // namespace

TEST_CASE("StateMachine starts in initial state", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};
	REQUIRE(fsm.isIn<Idle>());
	REQUIRE_FALSE(fsm.isIn<Walking>());
}

TEST_CASE("StateMachine transitions via update", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};

	fsm.update([](auto& state) -> std::optional<PlayerFSM::StateVariant>
	{
		using T = std::decay_t<decltype(state)>;
		if constexpr (std::is_same_v<T, Idle>)
		{
			return Walking{5.0f};
		}
		return std::nullopt;
	});

	REQUIRE(fsm.isIn<Walking>());
	REQUIRE(fsm.getState<Walking>().speed == 5.0f);
}

TEST_CASE("StateMachine stays if nullopt returned", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};

	fsm.update([](auto&) -> std::optional<PlayerFSM::StateVariant>
	{
		return std::nullopt;
	});

	REQUIRE(fsm.isIn<Idle>());
}

TEST_CASE("StateMachine visit reads current state", "[patterns][fsm]")
{
	PlayerFSM fsm{Walking{3.5f}};

	auto result = fsm.visit([](const auto& state) -> std::string
	{
		using T = std::decay_t<decltype(state)>;
		if constexpr (std::is_same_v<T, Walking>) return "walking";
		else if constexpr (std::is_same_v<T, Idle>) return "idle";
		else return "jumping";
	});

	REQUIRE(result == "walking");
}

TEST_CASE("StateMachine setState forces transition", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};
	fsm.setState(Jumping{10.0f});
	REQUIRE(fsm.isIn<Jumping>());
	REQUIRE(fsm.getState<Jumping>().velocity == 10.0f);
}

TEST_CASE("StateMachine guard blocks transition", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};
	// Jumping への遷移を禁止するガード
	fsm.addGuard([](const auto&, const auto& next) -> bool
	{
		return !std::holds_alternative<Jumping>(next);
	});

	fsm.update([](auto& state) -> std::optional<PlayerFSM::StateVariant>
	{
		using T = std::decay_t<decltype(state)>;
		if constexpr (std::is_same_v<T, Idle>)
		{
			return Jumping{10.0f};
		}
		return std::nullopt;
	});

	// ガードにより遷移がブロックされる
	REQUIRE(fsm.isIn<Idle>());
}

TEST_CASE("StateMachine guard allows transition", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};
	fsm.addGuard([](const auto&, const auto&) -> bool { return true; });

	fsm.update([](auto& state) -> std::optional<PlayerFSM::StateVariant>
	{
		using T = std::decay_t<decltype(state)>;
		if constexpr (std::is_same_v<T, Idle>)
		{
			return Walking{3.0f};
		}
		return std::nullopt;
	});

	REQUIRE(fsm.isIn<Walking>());
}

TEST_CASE("StateMachine onTransition callback receives from and to", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};
	bool callbackCalled = false;

	fsm.setOnTransition([&](const auto& from, const auto& to)
	{
		callbackCalled = true;
		REQUIRE(std::holds_alternative<Idle>(from));
		REQUIRE(std::holds_alternative<Walking>(to));
	});

	fsm.update([](auto& state) -> std::optional<PlayerFSM::StateVariant>
	{
		using T = std::decay_t<decltype(state)>;
		if constexpr (std::is_same_v<T, Idle>)
		{
			return Walking{5.0f};
		}
		return std::nullopt;
	});

	REQUIRE(callbackCalled);
}

TEST_CASE("StateMachine previousState tracks last state", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};
	REQUIRE_FALSE(fsm.previousState().has_value());

	fsm.setState(Walking{5.0f});
	REQUIRE(fsm.previousState().has_value());
	REQUIRE(std::holds_alternative<Idle>(*fsm.previousState()));

	fsm.setState(Jumping{10.0f});
	REQUIRE(std::holds_alternative<Walking>(*fsm.previousState()));
}

TEST_CASE("StateMachine previousState updated via update()", "[patterns][fsm]")
{
	PlayerFSM fsm{Idle{}};

	fsm.update([](auto& state) -> std::optional<PlayerFSM::StateVariant>
	{
		using T = std::decay_t<decltype(state)>;
		if constexpr (std::is_same_v<T, Idle>)
		{
			return Walking{3.0f};
		}
		return std::nullopt;
	});

	REQUIRE(fsm.previousState().has_value());
	REQUIRE(std::holds_alternative<Idle>(*fsm.previousState()));
}
