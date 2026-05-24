#include <catch2/catch_test_macros.hpp>

#include "sgc/containers/PriorityQueue.hpp"

#include <string>

using namespace sgc::containers;

TEST_CASE("PriorityQueue - push and top return min element", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	pq.push(5);
	pq.push(3);
	pq.push(7);

	REQUIRE(pq.top() == 3);
	REQUIRE(pq.size() == 3);
}

TEST_CASE("PriorityQueue - pop removes min element", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	pq.push(5);
	pq.push(3);
	pq.push(7);
	pq.push(1);

	pq.pop();
	REQUIRE(pq.top() == 3);
	pq.pop();
	REQUIRE(pq.top() == 5);
	pq.pop();
	REQUIRE(pq.top() == 7);
	pq.pop();
	REQUIRE(pq.empty());
}

TEST_CASE("PriorityQueue - pop on empty throws", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	REQUIRE_THROWS_AS(pq.pop(), std::out_of_range);
}

TEST_CASE("PriorityQueue - top on empty throws", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	REQUIRE_THROWS_AS(pq.top(), std::out_of_range);
}

TEST_CASE("PriorityQueue - decreaseKey promotes element", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	auto h1 = pq.push(10);
	pq.push(5);
	pq.push(8);

	REQUIRE(pq.top() == 5);
	REQUIRE(pq.decreaseKey(h1, 2));
	REQUIRE(pq.top() == 2);
}

TEST_CASE("PriorityQueue - decreaseKey fails for larger value", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	auto h = pq.push(5);

	REQUIRE_FALSE(pq.decreaseKey(h, 10));
	REQUIRE(pq.top() == 5);
}

TEST_CASE("PriorityQueue - handle getValue returns correct value", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	auto h = pq.push(42);

	auto val = pq.getValue(h);
	REQUIRE(val.has_value());
	REQUIRE(val->get() == 42);
}

TEST_CASE("PriorityQueue - getValue on popped handle returns nullopt", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	auto h = pq.push(42);
	pq.pop();

	REQUIRE_FALSE(pq.getValue(h).has_value());
	REQUIRE_FALSE(pq.isValidHandle(h));
}

TEST_CASE("PriorityQueue - max-heap with greater comparator", "[containers][PriorityQueue]")
{
	PriorityQueue<int, std::greater<int>> pq;
	pq.push(5);
	pq.push(3);
	pq.push(7);

	REQUIRE(pq.top() == 7);
	pq.pop();
	REQUIRE(pq.top() == 5);
}

TEST_CASE("PriorityQueue - clear resets state", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	pq.push(1);
	pq.push(2);
	pq.push(3);

	pq.clear();
	REQUIRE(pq.empty());
	REQUIRE(pq.size() == 0);
}

TEST_CASE("PriorityQueue - many elements maintain heap order", "[containers][PriorityQueue]")
{
	PriorityQueue<int> pq;
	for (int i = 100; i > 0; --i)
	{
		pq.push(i);
	}

	int prev = 0;
	while (!pq.empty())
	{
		REQUIRE(pq.top() > prev);
		prev = pq.top();
		pq.pop();
	}
}
