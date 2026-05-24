#include <catch2/catch_test_macros.hpp>

#include "sgc/roguelike/TurnManager.hpp"

using namespace sgc::roguelike;

TEST_CASE("TurnManager - Empty manager", "[roguelike][turn]")
{
	TurnManager manager;
	REQUIRE(manager.actorCount() == 0);
	REQUIRE(manager.currentActorId() == -1);
}

TEST_CASE("TurnManager - Add and remove actor", "[roguelike][turn]")
{
	TurnManager manager;
	manager.addActor(1, 100, true);
	REQUIRE(manager.actorCount() == 1);

	REQUIRE(manager.removeActor(1));
	REQUIRE(manager.actorCount() == 0);
}

TEST_CASE("TurnManager - Remove nonexistent actor returns false", "[roguelike][turn]")
{
	TurnManager manager;
	REQUIRE_FALSE(manager.removeActor(99));
}

TEST_CASE("TurnManager - Advance selects actor with energy", "[roguelike][turn]")
{
	TurnManager manager;
	manager.addActor(1, 100, true);
	manager.addActor(2, 50, false);

	bool advanced = manager.advance();
	REQUIRE(advanced);

	// Actor 1 has speed 100, Actor 2 has speed 50
	// Both get energy: Actor 1 = 100, Actor 2 = 50
	// Actor 1 reaches threshold first
	REQUIRE(manager.currentActorId() == 1);
}

TEST_CASE("TurnManager - Faster actors act more frequently", "[roguelike][turn]")
{
	TurnManager manager;
	manager.addActor(1, 200, true);   // Fast actor
	manager.addActor(2, 100, false);  // Normal actor

	int actorOneTurns = 0;
	int actorTwoTurns = 0;

	for (int i = 0; i < 20; ++i)
	{
		if (!manager.advance()) break;

		if (manager.currentActorId() == 1)
		{
			++actorOneTurns;
		}
		else
		{
			++actorTwoTurns;
		}
		manager.consumeAction();
	}

	// Actor 1 (speed 200) should act roughly twice as often as Actor 2 (speed 100)
	REQUIRE(actorOneTurns > actorTwoTurns);
}

TEST_CASE("TurnManager - ConsumeAction sets EndOfTurn phase", "[roguelike][turn]")
{
	TurnManager manager;
	manager.addActor(1, 100, true);
	manager.advance();
	manager.consumeAction();
	REQUIRE(manager.currentPhase() == TurnPhase::EndOfTurn);
}

TEST_CASE("TurnManager - Player actor sets PlayerInput phase", "[roguelike][turn]")
{
	TurnManager manager;
	manager.addActor(1, 100, true);
	manager.advance();
	REQUIRE(manager.currentPhase() == TurnPhase::PlayerInput);
	REQUIRE(manager.isCurrentPlayer());
}

TEST_CASE("TurnManager - Enemy actor sets EnemyAction phase", "[roguelike][turn]")
{
	TurnManager manager;
	manager.addActor(1, 100, false);  // Not a player
	manager.advance();
	REQUIRE(manager.currentPhase() == TurnPhase::EnemyAction);
	REQUIRE_FALSE(manager.isCurrentPlayer());
}

TEST_CASE("TurnManager - Advance on empty manager returns false", "[roguelike][turn]")
{
	TurnManager manager;
	REQUIRE_FALSE(manager.advance());
}

TEST_CASE("TurnManager - SetPhase changes phase", "[roguelike][turn]")
{
	TurnManager manager;
	manager.setPhase(TurnPhase::PlayerAction);
	REQUIRE(manager.currentPhase() == TurnPhase::PlayerAction);
}

TEST_CASE("TurnManager - Multiple advances maintain fairness", "[roguelike][turn]")
{
	TurnManager manager;
	manager.addActor(1, 100, true);
	manager.addActor(2, 100, false);

	int actor1 = 0;
	int actor2 = 0;

	for (int i = 0; i < 10; ++i)
	{
		manager.advance();
		if (manager.currentActorId() == 1) ++actor1;
		else ++actor2;
		manager.consumeAction();
	}

	// Equal speed should give roughly equal turns
	REQUIRE(actor1 > 0);
	REQUIRE(actor2 > 0);
}
