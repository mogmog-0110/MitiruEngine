/// @file TestObserver.cpp
/// @brief Observer.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/patterns/Observer.hpp"

TEST_CASE("Signal emits to connected slots", "[patterns][observer]")
{
	sgc::Signal<int> signal;
	int received = 0;

	signal.connect([&](int val) { received = val; });
	signal.emit(42);

	REQUIRE(received == 42);
}

TEST_CASE("Signal supports multiple slots", "[patterns][observer]")
{
	sgc::Signal<> signal;
	int count = 0;

	signal.connect([&] { ++count; });
	signal.connect([&] { ++count; });
	signal.emit();

	REQUIRE(count == 2);
}

TEST_CASE("Signal disconnect removes slot", "[patterns][observer]")
{
	sgc::Signal<int> signal;
	int count = 0;

	auto id = signal.connect([&](int) { ++count; });
	signal.emit(1);
	REQUIRE(count == 1);

	signal.disconnect(id);
	signal.emit(1);
	REQUIRE(count == 1);
}

TEST_CASE("Signal disconnectAll clears all slots", "[patterns][observer]")
{
	sgc::Signal<int> signal;
	int count = 0;

	signal.connect([&](int) { ++count; });
	signal.connect([&](int) { ++count; });
	signal.disconnectAll();
	signal.emit(1);

	REQUIRE(count == 0);
	REQUIRE(signal.connectionCount() == 0);
}

TEST_CASE("Signal with multiple arguments", "[patterns][observer]")
{
	sgc::Signal<float, float> signal;
	float rx = 0.0f, ry = 0.0f;

	signal.connect([&](float x, float y) { rx = x; ry = y; });
	signal.emit(1.5f, 2.5f);

	REQUIRE(rx == 1.5f);
	REQUIRE(ry == 2.5f);
}
