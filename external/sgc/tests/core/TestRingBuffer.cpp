#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "sgc/core/RingBuffer.hpp"

TEST_CASE("RingBuffer starts empty", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 4> buf;
	REQUIRE(buf.empty());
	REQUIRE(buf.size() == 0);
	REQUIRE(buf.capacity() == 4);
	REQUIRE_FALSE(buf.full());
}

TEST_CASE("RingBuffer pushBack and access", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 4> buf;
	buf.pushBack(10);
	buf.pushBack(20);
	buf.pushBack(30);

	REQUIRE(buf.size() == 3);
	REQUIRE(buf.front() == 10);
	REQUIRE(buf.back() == 30);
	REQUIRE(buf[0] == 10);
	REQUIRE(buf[1] == 20);
	REQUIRE(buf[2] == 30);
}

TEST_CASE("RingBuffer full and overwrite", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 3> buf;
	buf.pushBack(1);
	buf.pushBack(2);
	buf.pushBack(3);
	REQUIRE(buf.full());

	buf.pushBack(4); // 1 is overwritten
	REQUIRE(buf.size() == 3);
	REQUIRE(buf.front() == 2);
	REQUIRE(buf.back() == 4);
	REQUIRE(buf[0] == 2);
	REQUIRE(buf[1] == 3);
	REQUIRE(buf[2] == 4);
}

TEST_CASE("RingBuffer popFront", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 4> buf;
	buf.pushBack(1);
	buf.pushBack(2);
	buf.pushBack(3);

	buf.popFront();
	REQUIRE(buf.size() == 2);
	REQUIRE(buf.front() == 2);
}

TEST_CASE("RingBuffer popBack", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 4> buf;
	buf.pushBack(1);
	buf.pushBack(2);
	buf.pushBack(3);

	buf.popBack();
	REQUIRE(buf.size() == 2);
	REQUIRE(buf.back() == 2);
}

TEST_CASE("RingBuffer clear", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 4> buf;
	buf.pushBack(1);
	buf.pushBack(2);
	buf.clear();

	REQUIRE(buf.empty());
	REQUIRE(buf.size() == 0);
}

TEST_CASE("RingBuffer iterator range-for", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 4> buf;
	buf.pushBack(10);
	buf.pushBack(20);
	buf.pushBack(30);

	std::vector<int> result;
	for (const auto& v : buf)
	{
		result.push_back(v);
	}
	REQUIRE(result.size() == 3);
	REQUIRE(result[0] == 10);
	REQUIRE(result[1] == 20);
	REQUIRE(result[2] == 30);
}

TEST_CASE("RingBuffer iterator after wrap", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 3> buf;
	buf.pushBack(1);
	buf.pushBack(2);
	buf.pushBack(3);
	buf.pushBack(4); // wraps
	buf.pushBack(5); // wraps

	std::vector<int> result;
	for (const auto& v : buf)
	{
		result.push_back(v);
	}
	REQUIRE(result.size() == 3);
	REQUIRE(result[0] == 3);
	REQUIRE(result[1] == 4);
	REQUIRE(result[2] == 5);
}

TEST_CASE("RingBuffer with string type", "[core][ringbuffer]")
{
	sgc::RingBuffer<std::string, 2> buf;
	buf.pushBack("hello");
	buf.pushBack("world");
	REQUIRE(buf.front() == "hello");
	REQUIRE(buf.back() == "world");

	buf.pushBack("!"); // overwrites "hello"
	REQUIRE(buf.front() == "world");
	REQUIRE(buf.back() == "!");
}

TEST_CASE("RingBuffer move semantics", "[core][ringbuffer]")
{
	sgc::RingBuffer<std::string, 3> buf;
	std::string s = "moveme";
	buf.pushBack(std::move(s));
	REQUIRE(buf.front() == "moveme");
}

TEST_CASE("RingBuffer push pop interleaved", "[core][ringbuffer]")
{
	sgc::RingBuffer<int, 4> buf;
	buf.pushBack(1);
	buf.pushBack(2);
	buf.popFront(); // remove 1
	buf.pushBack(3);
	buf.pushBack(4);
	buf.pushBack(5);

	REQUIRE(buf.size() == 4);
	REQUIRE(buf[0] == 2);
	REQUIRE(buf[1] == 3);
	REQUIRE(buf[2] == 4);
	REQUIRE(buf[3] == 5);
}
