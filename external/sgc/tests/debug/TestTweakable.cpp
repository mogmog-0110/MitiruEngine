#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sgc/debug/Tweakable.hpp>

using namespace sgc::debug;

TEST_CASE("TweakRegistry - register and get float", "[debug]")
{
	TweakRegistry registry;
	registry.registerTweak<float>("gravity", 9.8f, 0.0f, 100.0f);

	const auto val = registry.getTweak<float>("gravity");
	REQUIRE(val.has_value());
	REQUIRE_THAT(val.value(), Catch::Matchers::WithinAbs(9.8, 0.001));
}

TEST_CASE("TweakRegistry - register and get int", "[debug]")
{
	TweakRegistry registry;
	registry.registerTweak<int>("lives", 3, 0, 99);

	const auto val = registry.getTweak<int>("lives");
	REQUIRE(val.has_value());
	REQUIRE(val.value() == 3);
}

TEST_CASE("TweakRegistry - setTweak updates value", "[debug]")
{
	TweakRegistry registry;
	registry.registerTweak<float>("speed", 5.0f, 0.0f, 20.0f);

	REQUIRE(registry.setTweak<float>("speed", 15.0f));
	const auto val = registry.getTweak<float>("speed");
	REQUIRE(val.has_value());
	REQUIRE_THAT(val.value(), Catch::Matchers::WithinAbs(15.0, 0.001));
}

TEST_CASE("TweakRegistry - setTweak clamps to range", "[debug]")
{
	TweakRegistry registry;
	registry.registerTweak<int>("count", 5, 0, 10);

	registry.setTweak<int>("count", 50);
	REQUIRE(registry.getTweak<int>("count").value() == 10);

	registry.setTweak<int>("count", -5);
	REQUIRE(registry.getTweak<int>("count").value() == 0);
}

TEST_CASE("TweakRegistry - getTweak returns nullopt for unknown name", "[debug]")
{
	TweakRegistry registry;
	REQUIRE_FALSE(registry.getTweak<float>("nonexistent").has_value());
}

TEST_CASE("TweakRegistry - getTweak returns nullopt for wrong type", "[debug]")
{
	TweakRegistry registry;
	registry.registerTweak<int>("val", 5, 0, 10);
	REQUIRE_FALSE(registry.getTweak<float>("val").has_value());
}

TEST_CASE("TweakRegistry - resetTweak restores default", "[debug]")
{
	TweakRegistry registry;
	registry.registerTweak<float>("x", 1.0f, 0.0f, 10.0f);
	registry.setTweak<float>("x", 8.0f);
	REQUIRE(registry.resetTweak("x"));

	const auto val = registry.getTweak<float>("x");
	REQUIRE_THAT(val.value(), Catch::Matchers::WithinAbs(1.0, 0.001));
}

TEST_CASE("TweakRegistry - listTweaks returns all entries", "[debug]")
{
	TweakRegistry registry;
	registry.registerTweak<float>("a", 1.0f, 0.0f, 10.0f);
	registry.registerTweak<int>("b", 5, 0, 100);

	const auto infos = registry.listTweaks();
	REQUIRE(infos.size() == 2);

	// 名前でソートされる保証はないので名前を集めてチェック
	bool foundA = false;
	bool foundB = false;
	for (const auto& info : infos)
	{
		if (info.name == "a")
		{
			foundA = true;
		}
		if (info.name == "b")
		{
			foundB = true;
		}
	}
	REQUIRE(foundA);
	REQUIRE(foundB);
}

TEST_CASE("TweakRegistry - contains and size", "[debug]")
{
	TweakRegistry registry;
	REQUIRE(registry.size() == 0);
	REQUIRE_FALSE(registry.contains("x"));

	registry.registerTweak<int>("x", 0, 0, 10);
	REQUIRE(registry.size() == 1);
	REQUIRE(registry.contains("x"));

	registry.clear();
	REQUIRE(registry.size() == 0);
}

TEST_CASE("TweakRegistry - getVar returns pointer", "[debug]")
{
	TweakRegistry registry;
	auto* ptr = registry.registerTweak<float>("val", 3.0f, 0.0f, 10.0f);
	REQUIRE(ptr != nullptr);
	REQUIRE_THAT(ptr->value, Catch::Matchers::WithinAbs(3.0, 0.001));
	REQUIRE_THAT(ptr->minValue, Catch::Matchers::WithinAbs(0.0, 0.001));
	REQUIRE_THAT(ptr->maxValue, Catch::Matchers::WithinAbs(10.0, 0.001));
}

TEST_CASE("TweakRegistry - SGC_TWEAK macro", "[debug]")
{
	TweakRegistry registry;
	auto* ptr = SGC_TWEAK(registry, float, "test", 5.0f, 0.0f, 20.0f);
	REQUIRE(ptr != nullptr);
	REQUIRE_THAT(ptr->value, Catch::Matchers::WithinAbs(5.0, 0.001));
}
