/// @file TestFlags.cpp
/// @brief Flags.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/core/Flags.hpp"

namespace
{
enum class Layer : unsigned
{
	None    = 0,
	Player  = 1 << 0,
	Enemy   = 1 << 1,
	Bullet  = 1 << 2,
	Wall    = 1 << 3,
};
} // namespace
SGC_ENABLE_FLAGS(Layer);

TEST_CASE("Flags OR operator", "[core][flags]")
{
	constexpr auto mask = Layer::Player | Layer::Enemy;
	REQUIRE(sgc::hasFlag(mask, Layer::Player));
	REQUIRE(sgc::hasFlag(mask, Layer::Enemy));
	REQUIRE_FALSE(sgc::hasFlag(mask, Layer::Bullet));
}

TEST_CASE("Flags setFlag and clearFlag", "[core][flags]")
{
	auto flags = Layer::None;
	flags = sgc::setFlag(flags, Layer::Player);
	REQUIRE(sgc::hasFlag(flags, Layer::Player));

	flags = sgc::clearFlag(flags, Layer::Player);
	REQUIRE_FALSE(sgc::hasFlag(flags, Layer::Player));
}

TEST_CASE("Flags toggleFlag", "[core][flags]")
{
	auto flags = Layer::Player;
	flags = sgc::toggleFlag(flags, Layer::Player);
	REQUIRE_FALSE(sgc::hasFlag(flags, Layer::Player));

	flags = sgc::toggleFlag(flags, Layer::Player);
	REQUIRE(sgc::hasFlag(flags, Layer::Player));
}

TEST_CASE("Flags AND operator", "[core][flags]")
{
	constexpr auto a = Layer::Player | Layer::Enemy | Layer::Bullet;
	constexpr auto b = Layer::Enemy | Layer::Wall;
	constexpr auto result = a & b;
	REQUIRE(sgc::hasFlag(result, Layer::Enemy));
	REQUIRE_FALSE(sgc::hasFlag(result, Layer::Player));
}

TEST_CASE("Flags compound assignment", "[core][flags]")
{
	auto flags = Layer::None;
	flags |= Layer::Player;
	flags |= Layer::Bullet;
	REQUIRE(sgc::hasFlag(flags, Layer::Player));
	REQUIRE(sgc::hasFlag(flags, Layer::Bullet));
}
