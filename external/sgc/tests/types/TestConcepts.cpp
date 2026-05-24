/// @file TestConcepts.cpp
/// @brief Concepts.hpp のユニットテスト — 各コンセプトの型制約を検証

#include <catch2/catch_test_macros.hpp>

#include "sgc/types/Concepts.hpp"

#include <string>
#include <vector>

// ── Arithmetic ──────────────────────────────────────────────────

TEST_CASE("Arithmetic concept accepts numeric types", "[types][concepts]")
{
	STATIC_REQUIRE(sgc::Arithmetic<int>);
	STATIC_REQUIRE(sgc::Arithmetic<float>);
	STATIC_REQUIRE(sgc::Arithmetic<double>);
	STATIC_REQUIRE(sgc::Arithmetic<unsigned long>);
	STATIC_REQUIRE(sgc::Arithmetic<char>);
	STATIC_REQUIRE(sgc::Arithmetic<bool>);
}

TEST_CASE("Arithmetic concept rejects non-numeric types", "[types][concepts]")
{
	STATIC_REQUIRE_FALSE(sgc::Arithmetic<std::string>);
	STATIC_REQUIRE_FALSE(sgc::Arithmetic<std::vector<int>>);
	STATIC_REQUIRE_FALSE(sgc::Arithmetic<void*>);
}

// ── FloatingPoint ───────────────────────────────────────────────

TEST_CASE("FloatingPoint concept", "[types][concepts]")
{
	STATIC_REQUIRE(sgc::FloatingPoint<float>);
	STATIC_REQUIRE(sgc::FloatingPoint<double>);
	STATIC_REQUIRE(sgc::FloatingPoint<long double>);
	STATIC_REQUIRE_FALSE(sgc::FloatingPoint<int>);
	STATIC_REQUIRE_FALSE(sgc::FloatingPoint<unsigned>);
}

// ── Integral ────────────────────────────────────────────────────

TEST_CASE("Integral concepts", "[types][concepts]")
{
	STATIC_REQUIRE(sgc::Integral<int>);
	STATIC_REQUIRE(sgc::SignedIntegral<int>);
	STATIC_REQUIRE_FALSE(sgc::UnsignedIntegral<int>);

	STATIC_REQUIRE(sgc::UnsignedIntegral<unsigned int>);
	STATIC_REQUIRE_FALSE(sgc::SignedIntegral<unsigned int>);
}

// ── Hashable ────────────────────────────────────────────────────

TEST_CASE("Hashable concept", "[types][concepts]")
{
	STATIC_REQUIRE(sgc::Hashable<int>);
	STATIC_REQUIRE(sgc::Hashable<std::string>);
	STATIC_REQUIRE(sgc::Hashable<float>);
}

// ── EqualityComparable / TotallyOrdered ─────────────────────────

TEST_CASE("Ordering concepts", "[types][concepts]")
{
	STATIC_REQUIRE(sgc::EqualityComparable<int>);
	STATIC_REQUIRE(sgc::TotallyOrdered<int>);
	STATIC_REQUIRE(sgc::EqualityComparable<std::string>);
	STATIC_REQUIRE(sgc::TotallyOrdered<std::string>);
}

// ── Iterable / Sizable ──────────────────────────────────────────

TEST_CASE("Iterable and Sizable concepts", "[types][concepts]")
{
	STATIC_REQUIRE(sgc::Iterable<std::vector<int>>);
	STATIC_REQUIRE(sgc::Sizable<std::vector<int>>);
	STATIC_REQUIRE(sgc::Iterable<std::string>);
	STATIC_REQUIRE_FALSE(sgc::Iterable<int>);
	STATIC_REQUIRE_FALSE(sgc::Sizable<int>);
}

// ── Enum ────────────────────────────────────────────────────────

enum UnscopedEnum { A, B, C };
enum class ScopedEnumType { X, Y, Z };

TEST_CASE("Enum concepts", "[types][concepts]")
{
	STATIC_REQUIRE(sgc::Enum<UnscopedEnum>);
	STATIC_REQUIRE(sgc::Enum<ScopedEnumType>);
	STATIC_REQUIRE_FALSE(sgc::Enum<int>);

	STATIC_REQUIRE(sgc::ScopedEnum<ScopedEnumType>);
	STATIC_REQUIRE_FALSE(sgc::ScopedEnum<UnscopedEnum>);
	STATIC_REQUIRE_FALSE(sgc::ScopedEnum<int>);
}

// ── Callable ────────────────────────────────────────────────────

TEST_CASE("Callable concepts", "[types][concepts]")
{
	auto intReturner = []() -> int { return 42; };
	STATIC_REQUIRE(sgc::InvocableReturning<decltype(intReturner), int>);

	auto isPositive = [](int x) -> bool { return x > 0; };
	STATIC_REQUIRE(sgc::Predicate<decltype(isPositive), int>);
}
