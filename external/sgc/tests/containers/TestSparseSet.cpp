#include <catch2/catch_test_macros.hpp>

#include "sgc/containers/SparseSet.hpp"

#include <string>

using namespace sgc::containers;

TEST_CASE("SparseSet - insert and contains", "[containers][SparseSet]")
{
	SparseSet<int> set;
	REQUIRE(set.insert(5, 100));
	REQUIRE(set.contains(5));
	REQUIRE_FALSE(set.contains(3));
	REQUIRE(set.size() == 1);
}

TEST_CASE("SparseSet - duplicate insert returns false", "[containers][SparseSet]")
{
	SparseSet<int> set;
	REQUIRE(set.insert(5, 100));
	REQUIRE_FALSE(set.insert(5, 200));
	REQUIRE(set.get(5) == 100);
}

TEST_CASE("SparseSet - get retrieves correct value", "[containers][SparseSet]")
{
	SparseSet<std::string> set;
	set.insert(0, "alpha");
	set.insert(10, "beta");
	set.insert(100, "gamma");

	REQUIRE(set.get(0) == "alpha");
	REQUIRE(set.get(10) == "beta");
	REQUIRE(set.get(100) == "gamma");
}

TEST_CASE("SparseSet - get throws for missing ID", "[containers][SparseSet]")
{
	SparseSet<int> set;
	REQUIRE_THROWS_AS(set.get(42), std::out_of_range);
}

TEST_CASE("SparseSet - remove with swap-back", "[containers][SparseSet]")
{
	SparseSet<int> set;
	set.insert(1, 10);
	set.insert(2, 20);
	set.insert(3, 30);

	REQUIRE(set.remove(2));
	REQUIRE_FALSE(set.contains(2));
	REQUIRE(set.contains(1));
	REQUIRE(set.contains(3));
	REQUIRE(set.size() == 2);
	REQUIRE(set.get(1) == 10);
	REQUIRE(set.get(3) == 30);
}

TEST_CASE("SparseSet - remove nonexistent returns false", "[containers][SparseSet]")
{
	SparseSet<int> set;
	REQUIRE_FALSE(set.remove(99));
}

TEST_CASE("SparseSet - clear empties the set", "[containers][SparseSet]")
{
	SparseSet<int> set;
	set.insert(1, 10);
	set.insert(2, 20);
	set.clear();
	REQUIRE(set.empty());
	REQUIRE(set.size() == 0);
	REQUIRE_FALSE(set.contains(1));
	REQUIRE_FALSE(set.contains(2));
}

TEST_CASE("SparseSet - range-for iteration over dense array", "[containers][SparseSet]")
{
	SparseSet<int> set;
	set.insert(5, 50);
	set.insert(10, 100);
	set.insert(15, 150);

	int sum = 0;
	for (const auto& val : set)
	{
		sum += val;
	}
	REQUIRE(sum == 300);
}

TEST_CASE("SparseSet - tryGet returns optional", "[containers][SparseSet]")
{
	SparseSet<int> set;
	set.insert(7, 42);

	auto found = set.tryGet(7);
	REQUIRE(found.has_value());
	REQUIRE(found->get() == 42);

	auto notFound = set.tryGet(99);
	REQUIRE_FALSE(notFound.has_value());
}

TEST_CASE("SparseSet - ids and data accessors", "[containers][SparseSet]")
{
	SparseSet<int> set;
	set.insert(3, 30);
	set.insert(7, 70);

	const auto& ids = set.ids();
	const auto& data = set.data();

	REQUIRE(ids.size() == 2);
	REQUIRE(data.size() == 2);
	REQUIRE(ids[0] == 3);
	REQUIRE(data[0] == 30);
}

TEST_CASE("SparseSet - reinsert after remove", "[containers][SparseSet]")
{
	SparseSet<int> set;
	set.insert(1, 10);
	set.remove(1);
	REQUIRE_FALSE(set.contains(1));
	REQUIRE(set.insert(1, 99));
	REQUIRE(set.get(1) == 99);
}
