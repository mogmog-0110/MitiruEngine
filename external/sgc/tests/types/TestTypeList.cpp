/// @file TestTypeList.cpp
/// @brief TypeList.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/types/TypeList.hpp"

#include <string>

using TestList = sgc::TypeList<int, float, double, std::string>;

TEST_CASE("TypeList size", "[types][typelist]")
{
	STATIC_REQUIRE(sgc::TYPE_LIST_SIZE<TestList> == 4);
	STATIC_REQUIRE(sgc::TYPE_LIST_SIZE<sgc::TypeList<>> == 0);
}

TEST_CASE("TypeList at", "[types][typelist]")
{
	STATIC_REQUIRE(std::is_same_v<sgc::TypeListAtT<0, TestList>, int>);
	STATIC_REQUIRE(std::is_same_v<sgc::TypeListAtT<1, TestList>, float>);
	STATIC_REQUIRE(std::is_same_v<sgc::TypeListAtT<2, TestList>, double>);
	STATIC_REQUIRE(std::is_same_v<sgc::TypeListAtT<3, TestList>, std::string>);
}

TEST_CASE("TypeList contains", "[types][typelist]")
{
	STATIC_REQUIRE(sgc::TYPE_LIST_CONTAINS<int, TestList>);
	STATIC_REQUIRE(sgc::TYPE_LIST_CONTAINS<float, TestList>);
	STATIC_REQUIRE_FALSE(sgc::TYPE_LIST_CONTAINS<char, TestList>);
}

TEST_CASE("TypeList index", "[types][typelist]")
{
	STATIC_REQUIRE(sgc::TYPE_LIST_INDEX<int, TestList> == 0);
	STATIC_REQUIRE(sgc::TYPE_LIST_INDEX<float, TestList> == 1);
	STATIC_REQUIRE(sgc::TYPE_LIST_INDEX<double, TestList> == 2);
}

TEST_CASE("TypeList push front", "[types][typelist]")
{
	using Result = sgc::TypeListPushFront<char, TestList>::Type;
	STATIC_REQUIRE(sgc::TYPE_LIST_SIZE<Result> == 5);
	STATIC_REQUIRE(std::is_same_v<sgc::TypeListAtT<0, Result>, char>);
}

TEST_CASE("TypeList push back", "[types][typelist]")
{
	using Result = sgc::TypeListPushBack<char, TestList>::Type;
	STATIC_REQUIRE(sgc::TYPE_LIST_SIZE<Result> == 5);
	STATIC_REQUIRE(std::is_same_v<sgc::TypeListAtT<4, Result>, char>);
}
