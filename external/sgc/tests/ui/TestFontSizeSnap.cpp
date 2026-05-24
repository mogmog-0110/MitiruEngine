#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sgc/ui/FontSizeSnap.hpp>
#include <array>

using namespace sgc::ui;
using Catch::Matchers::WithinAbs;

TEST_CASE("snapToNearestSize - exact match", "[ui][font-snap]")
{
	constexpr std::array<float, 5> sizes{10.0f, 14.0f, 18.0f, 24.0f, 32.0f};
	REQUIRE_THAT(snapToNearestSize(18.0f, sizes), WithinAbs(18.0f, 0.001));
}

TEST_CASE("snapToNearestSize - nearest lower", "[ui][font-snap]")
{
	constexpr std::array<float, 5> sizes{10.0f, 14.0f, 18.0f, 24.0f, 32.0f};
	REQUIRE_THAT(snapToNearestSize(19.0f, sizes), WithinAbs(18.0f, 0.001));
}

TEST_CASE("snapToNearestSize - nearest upper", "[ui][font-snap]")
{
	constexpr std::array<float, 5> sizes{10.0f, 14.0f, 18.0f, 24.0f, 32.0f};
	REQUIRE_THAT(snapToNearestSize(22.0f, sizes), WithinAbs(24.0f, 0.001));
}

TEST_CASE("snapToNearestSize - empty returns target", "[ui][font-snap]")
{
	std::span<const float> empty;
	REQUIRE_THAT(snapToNearestSize(15.0f, empty), WithinAbs(15.0f, 0.001));
}

TEST_CASE("snapToNearestSize - single element", "[ui][font-snap]")
{
	constexpr std::array<float, 1> sizes{20.0f};
	REQUIRE_THAT(snapToNearestSize(15.0f, sizes), WithinAbs(20.0f, 0.001));
}

TEST_CASE("snapFloor - exact match", "[ui][font-snap]")
{
	constexpr std::array<float, 5> sizes{10.0f, 14.0f, 18.0f, 24.0f, 32.0f};
	REQUIRE_THAT(snapFloor(18.0f, sizes), WithinAbs(18.0f, 0.001));
}

TEST_CASE("snapFloor - between sizes", "[ui][font-snap]")
{
	constexpr std::array<float, 5> sizes{10.0f, 14.0f, 18.0f, 24.0f, 32.0f};
	REQUIRE_THAT(snapFloor(20.0f, sizes), WithinAbs(18.0f, 0.001));
}

TEST_CASE("snapFloor - below all returns smallest", "[ui][font-snap]")
{
	constexpr std::array<float, 3> sizes{14.0f, 18.0f, 24.0f};
	REQUIRE_THAT(snapFloor(10.0f, sizes), WithinAbs(14.0f, 0.001));
}

TEST_CASE("snapCeil - exact match", "[ui][font-snap]")
{
	constexpr std::array<float, 5> sizes{10.0f, 14.0f, 18.0f, 24.0f, 32.0f};
	REQUIRE_THAT(snapCeil(18.0f, sizes), WithinAbs(18.0f, 0.001));
}

TEST_CASE("snapCeil - between sizes", "[ui][font-snap]")
{
	constexpr std::array<float, 5> sizes{10.0f, 14.0f, 18.0f, 24.0f, 32.0f};
	REQUIRE_THAT(snapCeil(15.0f, sizes), WithinAbs(18.0f, 0.001));
}

TEST_CASE("snapCeil - above all returns largest", "[ui][font-snap]")
{
	constexpr std::array<float, 3> sizes{14.0f, 18.0f, 24.0f};
	REQUIRE_THAT(snapCeil(30.0f, sizes), WithinAbs(24.0f, 0.001));
}
