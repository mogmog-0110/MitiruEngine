#include <catch2/catch_test_macros.hpp>

#include "sgc/core/Hash.hpp"

using namespace sgc::literals;

TEST_CASE("fnv1aHash produces consistent results", "[core][hash]")
{
	constexpr auto h1 = sgc::fnv1aHash("hello");
	constexpr auto h2 = sgc::fnv1aHash("hello");
	STATIC_REQUIRE(h1 == h2);
	REQUIRE(h1 == h2);
}

TEST_CASE("fnv1aHash differs for different strings", "[core][hash]")
{
	constexpr auto h1 = sgc::fnv1aHash("hello");
	constexpr auto h2 = sgc::fnv1aHash("world");
	STATIC_REQUIRE(h1 != h2);
}

TEST_CASE("fnv1aHash empty string has known value", "[core][hash]")
{
	constexpr auto h = sgc::fnv1aHash("");
	STATIC_REQUIRE(h == sgc::FNV1A_OFFSET_BASIS);
}

TEST_CASE("user-defined literal produces same hash", "[core][hash]")
{
	constexpr auto literal = "jump"_hash;
	constexpr auto func = sgc::fnv1aHash("jump");
	STATIC_REQUIRE(literal == func);
}

TEST_CASE("StringHash wraps fnv1a correctly", "[core][hash]")
{
	constexpr sgc::StringHash h("player");
	constexpr auto expected = sgc::fnv1aHash("player");
	STATIC_REQUIRE(h.value() == expected);
	STATIC_REQUIRE(static_cast<std::uint64_t>(h) == expected);
}

TEST_CASE("StringHash equality comparison", "[core][hash]")
{
	constexpr sgc::StringHash h1("attack");
	constexpr sgc::StringHash h2("attack");
	constexpr sgc::StringHash h3("defend");
	STATIC_REQUIRE(h1 == h2);
	STATIC_REQUIRE(!(h1 == h3));
}

TEST_CASE("StringHash default constructor gives zero", "[core][hash]")
{
	constexpr sgc::StringHash h;
	STATIC_REQUIRE(h.value() == 0);
}

TEST_CASE("fnv1aHash handles various lengths", "[core][hash]")
{
	constexpr auto h1 = sgc::fnv1aHash("a");
	constexpr auto h2 = sgc::fnv1aHash("ab");
	constexpr auto h3 = sgc::fnv1aHash("abc");
	STATIC_REQUIRE(h1 != h2);
	STATIC_REQUIRE(h2 != h3);
	STATIC_REQUIRE(h1 != h3);
}
