#include <catch2/catch_test_macros.hpp>

#include <array>

#include "sgc/input/InputBuffer.hpp"

TEST_CASE("InputBuffer starts empty", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;
	REQUIRE(buf.size() == 0);
}

TEST_CASE("InputBuffer record increases size", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;
	buf.record(1, 0.0f);
	buf.record(2, 0.1f);
	REQUIRE(buf.size() == 2);
}

TEST_CASE("InputBuffer matchSequence single action", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;
	buf.record(42, 1.0f);

	std::array<std::uint64_t, 1> seq = {42};
	REQUIRE(buf.matchSequence(seq, 1.0f));
}

TEST_CASE("InputBuffer matchSequence multiple actions", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;
	buf.record(1, 1.0f);
	buf.record(2, 1.2f);
	buf.record(3, 1.3f);

	std::array<std::uint64_t, 3> seq = {1, 2, 3};
	REQUIRE(buf.matchSequence(seq, 0.5f));
}

TEST_CASE("InputBuffer matchSequence fails with wrong order", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;
	buf.record(3, 1.0f);
	buf.record(2, 1.1f);
	buf.record(1, 1.2f);

	std::array<std::uint64_t, 3> seq = {1, 2, 3};
	REQUIRE_FALSE(buf.matchSequence(seq, 0.5f));
}

TEST_CASE("InputBuffer matchSequence fails with timeout", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;
	buf.record(1, 1.0f);
	buf.record(2, 3.0f); // 2秒の間隔
	buf.record(3, 3.1f);

	std::array<std::uint64_t, 3> seq = {1, 2, 3};
	REQUIRE_FALSE(buf.matchSequence(seq, 0.5f));
}

TEST_CASE("InputBuffer matchSequence empty sequence", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;
	buf.record(1, 1.0f);

	std::span<const std::uint64_t> emptySeq;
	REQUIRE(buf.matchSequence(emptySeq, 1.0f));
}

TEST_CASE("InputBuffer matchSequence on empty buffer", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;

	std::array<std::uint64_t, 1> seq = {1};
	REQUIRE_FALSE(buf.matchSequence(seq, 1.0f));
}

TEST_CASE("InputBuffer clear empties buffer", "[input][inputbuffer]")
{
	sgc::InputBuffer<10> buf;
	buf.record(1, 0.0f);
	buf.record(2, 0.1f);
	buf.clear();
	REQUIRE(buf.size() == 0);
}

TEST_CASE("InputBuffer wraps around at capacity", "[input][inputbuffer]")
{
	sgc::InputBuffer<3> buf;
	buf.record(1, 0.0f);
	buf.record(2, 0.1f);
	buf.record(3, 0.2f);
	buf.record(4, 0.3f); // 1 is overwritten

	REQUIRE(buf.size() == 3);

	// sequence 1,2,3 should no longer match (1 is gone)
	std::array<std::uint64_t, 3> seq = {1, 2, 3};
	REQUIRE_FALSE(buf.matchSequence(seq, 1.0f));

	// sequence 2,3,4 should match
	std::array<std::uint64_t, 3> seq2 = {2, 3, 4};
	REQUIRE(buf.matchSequence(seq2, 1.0f));
}
