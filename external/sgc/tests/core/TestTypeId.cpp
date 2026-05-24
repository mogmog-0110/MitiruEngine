/// @file TestTypeId.cpp
/// @brief TypeId.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/core/TypeId.hpp"

TEST_CASE("TypeId returns same value for same type", "[core][typeid]")
{
	auto id1 = sgc::typeId<int>();
	auto id2 = sgc::typeId<int>();
	REQUIRE(id1 == id2);
}

TEST_CASE("TypeId returns different values for different types", "[core][typeid]")
{
	auto intId = sgc::typeId<int>();
	auto floatId = sgc::typeId<float>();
	auto stringId = sgc::typeId<std::string>();

	REQUIRE(intId != floatId);
	REQUIRE(intId != stringId);
	REQUIRE(floatId != stringId);
}

TEST_CASE("TypeId is consistent across calls", "[core][typeid]")
{
	auto first = sgc::typeId<double>();
	auto second = sgc::typeId<double>();
	auto third = sgc::typeId<double>();

	REQUIRE(first == second);
	REQUIRE(second == third);
}
