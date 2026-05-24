#include <catch2/catch_test_macros.hpp>

#include "sgc/core/HandleMap.hpp"

struct TestTag {};
using TestHandle = sgc::Handle<TestTag>;

struct OtherTag {};
using OtherHandle = sgc::Handle<OtherTag>;

TEST_CASE("Handle default is invalid", "[core][handle]")
{
	TestHandle h;
	REQUIRE_FALSE(h.isValid());
}

TEST_CASE("Handle equality comparison", "[core][handle]")
{
	TestHandle a{0, 1};
	TestHandle b{0, 1};
	TestHandle c{0, 2};
	TestHandle d{1, 1};

	REQUIRE(a == b);
	REQUIRE_FALSE(a == c);
	REQUIRE_FALSE(a == d);
}

TEST_CASE("HandleMap insert and get", "[core][handle]")
{
	sgc::HandleMap<TestTag, int> map;
	auto h = map.insert(42);

	REQUIRE(h.isValid());
	REQUIRE(map.isValid(h));
	REQUIRE(map.size() == 1);

	auto* val = map.get(h);
	REQUIRE(val != nullptr);
	REQUIRE(*val == 42);
}

TEST_CASE("HandleMap remove invalidates handle", "[core][handle]")
{
	sgc::HandleMap<TestTag, int> map;
	auto h = map.insert(10);
	map.remove(h);

	REQUIRE_FALSE(map.isValid(h));
	REQUIRE(map.get(h) == nullptr);
	REQUIRE(map.size() == 0);
}

TEST_CASE("HandleMap generation prevents stale access", "[core][handle]")
{
	sgc::HandleMap<TestTag, int> map;
	auto h1 = map.insert(100);
	map.remove(h1);

	// 新しい挿入はスロットを再利用するが世代が異なる
	auto h2 = map.insert(200);
	REQUIRE_FALSE(map.isValid(h1));
	REQUIRE(map.isValid(h2));
	REQUIRE(*map.get(h2) == 200);
}

TEST_CASE("HandleMap multiple inserts and removes", "[core][handle]")
{
	sgc::HandleMap<TestTag, std::string> map;
	auto h1 = map.insert("alpha");
	auto h2 = map.insert("beta");
	auto h3 = map.insert("gamma");

	REQUIRE(map.size() == 3);

	map.remove(h2);
	REQUIRE(map.size() == 2);
	REQUIRE(map.isValid(h1));
	REQUIRE_FALSE(map.isValid(h2));
	REQUIRE(map.isValid(h3));

	REQUIRE(*map.get(h1) == "alpha");
	REQUIRE(*map.get(h3) == "gamma");
}

TEST_CASE("HandleMap forEach iterates all elements", "[core][handle]")
{
	sgc::HandleMap<TestTag, int> map;
	(void)map.insert(1);
	(void)map.insert(2);
	(void)map.insert(3);

	int sum = 0;
	int count = 0;
	map.forEach([&](TestHandle, int& val) {
		sum += val;
		++count;
	});
	REQUIRE(count == 3);
	REQUIRE(sum == 6);
}

TEST_CASE("HandleMap clear removes all", "[core][handle]")
{
	sgc::HandleMap<TestTag, int> map;
	auto h = map.insert(1);
	(void)map.insert(2);
	map.clear();

	REQUIRE(map.empty());
	REQUIRE(map.size() == 0);
	REQUIRE_FALSE(map.isValid(h));
}

TEST_CASE("HandleMap const get", "[core][handle]")
{
	sgc::HandleMap<TestTag, int> map;
	auto h = map.insert(99);

	const auto& constMap = map;
	const auto* val = constMap.get(h);
	REQUIRE(val != nullptr);
	REQUIRE(*val == 99);
}

TEST_CASE("HandleMap invalid handle returns nullptr", "[core][handle]")
{
	sgc::HandleMap<TestTag, int> map;
	TestHandle invalid;
	REQUIRE(map.get(invalid) == nullptr);
	REQUIRE_FALSE(map.isValid(invalid));
}

TEST_CASE("HandleMap slot reuse after multiple removes", "[core][handle]")
{
	sgc::HandleMap<TestTag, int> map;
	auto h1 = map.insert(1);
	auto h2 = map.insert(2);

	map.remove(h1);
	map.remove(h2);

	auto h3 = map.insert(3);
	auto h4 = map.insert(4);

	REQUIRE(map.size() == 2);
	REQUIRE(*map.get(h3) == 3);
	REQUIRE(*map.get(h4) == 4);
	REQUIRE_FALSE(map.isValid(h1));
	REQUIRE_FALSE(map.isValid(h2));
}
