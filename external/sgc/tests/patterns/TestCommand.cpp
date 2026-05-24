/// @file TestCommand.cpp
/// @brief Command.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/patterns/Command.hpp"

TEST_CASE("CommandHistory execute runs command", "[patterns][command]")
{
	sgc::CommandHistory history;
	int value = 0;

	history.execute(sgc::makeLambdaCommand(
		[&]{ value += 10; },
		[&]{ value -= 10; }
	));

	REQUIRE(value == 10);
	REQUIRE(history.undoSize() == 1);
}

TEST_CASE("CommandHistory undo reverses command", "[patterns][command]")
{
	sgc::CommandHistory history;
	int value = 0;

	history.execute(sgc::makeLambdaCommand(
		[&]{ value += 10; },
		[&]{ value -= 10; }
	));

	REQUIRE(history.undo());
	REQUIRE(value == 0);
	REQUIRE(history.undoSize() == 0);
	REQUIRE(history.redoSize() == 1);
}

TEST_CASE("CommandHistory redo re-executes command", "[patterns][command]")
{
	sgc::CommandHistory history;
	int value = 0;

	history.execute(sgc::makeLambdaCommand(
		[&]{ value += 10; },
		[&]{ value -= 10; }
	));

	history.undo();
	REQUIRE(history.redo());
	REQUIRE(value == 10);
}

TEST_CASE("CommandHistory new execute clears redo stack", "[patterns][command]")
{
	sgc::CommandHistory history;
	int value = 0;

	history.execute(sgc::makeLambdaCommand(
		[&]{ value += 10; },
		[&]{ value -= 10; }
	));
	history.undo();

	history.execute(sgc::makeLambdaCommand(
		[&]{ value += 20; },
		[&]{ value -= 20; }
	));

	REQUIRE_FALSE(history.canRedo());
	REQUIRE(value == 20);
}

TEST_CASE("CommandHistory multiple undo/redo", "[patterns][command]")
{
	sgc::CommandHistory history;
	int value = 0;

	history.execute(sgc::makeLambdaCommand([&]{ value += 1; }, [&]{ value -= 1; }));
	history.execute(sgc::makeLambdaCommand([&]{ value += 2; }, [&]{ value -= 2; }));
	history.execute(sgc::makeLambdaCommand([&]{ value += 3; }, [&]{ value -= 3; }));

	REQUIRE(value == 6);

	history.undo();
	REQUIRE(value == 3);

	history.undo();
	REQUIRE(value == 1);

	history.redo();
	REQUIRE(value == 3);
}
