#include <catch2/catch_test_macros.hpp>

#include "sgc/ui/PendingAction.hpp"

#include <string>

TEST_CASE("PendingAction<void> - basic trigger and consume", "[ui][PendingAction]")
{
	sgc::ui::PendingAction<> action;

	REQUIRE_FALSE(action.isPending());
	REQUIRE_FALSE(action.consume());

	action.trigger();
	REQUIRE(action.isPending());
	REQUIRE(action.consume());
	REQUIRE_FALSE(action.isPending());
	REQUIRE_FALSE(action.consume());
}

TEST_CASE("PendingAction<void> - multiple triggers before consume", "[ui][PendingAction]")
{
	sgc::ui::PendingAction<> action;

	action.trigger();
	action.trigger();
	REQUIRE(action.consume());
	REQUIRE_FALSE(action.consume());
}

TEST_CASE("PendingAction<void> - reset clears trigger", "[ui][PendingAction]")
{
	sgc::ui::PendingAction<> action;

	action.trigger();
	action.reset();
	REQUIRE_FALSE(action.isPending());
	REQUIRE_FALSE(action.consume());
}

TEST_CASE("PendingAction<void> - trigger is const-callable", "[ui][PendingAction]")
{
	const sgc::ui::PendingAction<> action;
	action.trigger();
	REQUIRE(action.isPending());
}

TEST_CASE("PendingAction<T> - trigger and consumeValue", "[ui][PendingAction]")
{
	sgc::ui::PendingAction<std::string> action;

	REQUIRE_FALSE(action.isPending());
	REQUIRE_FALSE(action.consumeValue().has_value());

	action.trigger("hello");
	REQUIRE(action.isPending());

	auto val = action.consumeValue();
	REQUIRE(val.has_value());
	REQUIRE(*val == "hello");
	REQUIRE_FALSE(action.isPending());
}

TEST_CASE("PendingAction<T> - trigger overwrites previous value", "[ui][PendingAction]")
{
	sgc::ui::PendingAction<int> action;

	action.trigger(42);
	action.trigger(99);

	auto val = action.consumeValue();
	REQUIRE(val.has_value());
	REQUIRE(*val == 99);
}

TEST_CASE("PendingAction<T> - reset clears value", "[ui][PendingAction]")
{
	sgc::ui::PendingAction<int> action;

	action.trigger(42);
	action.reset();
	REQUIRE_FALSE(action.isPending());
	REQUIRE_FALSE(action.consumeValue().has_value());
}

TEST_CASE("PendingAction<T> - trigger is const-callable", "[ui][PendingAction]")
{
	const sgc::ui::PendingAction<std::string> action;
	action.trigger("test");
	REQUIRE(action.isPending());
}
