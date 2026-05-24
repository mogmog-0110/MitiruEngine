/// @file TestBitSet.cpp
/// @brief BitSet.hpp のユニットテスト

#include <catch2/catch_test_macros.hpp>

#include "sgc/containers/BitSet.hpp"

TEST_CASE("BitSet set and clear individual bits", "[containers][bitset]")
{
	sgc::containers::BitSet<128> bs;
	REQUIRE(bs.none());

	bs.set(0);
	REQUIRE(bs.test(0));
	REQUIRE_FALSE(bs.test(1));

	bs.set(63);
	REQUIRE(bs.test(63));

	bs.set(64);
	REQUIRE(bs.test(64));

	bs.set(127);
	REQUIRE(bs.test(127));

	bs.clear(0);
	REQUIRE_FALSE(bs.test(0));

	// 範囲外は無視される
	bs.set(200);
	REQUIRE_FALSE(bs.test(200));
}

TEST_CASE("BitSet toggle bits", "[containers][bitset]")
{
	sgc::containers::BitSet<64> bs;
	bs.toggle(5);
	REQUIRE(bs.test(5));

	bs.toggle(5);
	REQUIRE_FALSE(bs.test(5));
}

TEST_CASE("BitSet count returns number of set bits", "[containers][bitset]")
{
	sgc::containers::BitSet<256> bs;
	REQUIRE(bs.count() == 0);

	bs.set(0);
	bs.set(10);
	bs.set(100);
	bs.set(255);
	REQUIRE(bs.count() == 4);
}

TEST_CASE("BitSet bitwise operators", "[containers][bitset]")
{
	sgc::containers::BitSet<64> a;
	sgc::containers::BitSet<64> b;

	a.set(0);
	a.set(1);
	a.set(2);

	b.set(1);
	b.set(2);
	b.set(3);

	auto andResult = a & b;
	REQUIRE_FALSE(andResult.test(0));
	REQUIRE(andResult.test(1));
	REQUIRE(andResult.test(2));
	REQUIRE_FALSE(andResult.test(3));

	auto orResult = a | b;
	REQUIRE(orResult.test(0));
	REQUIRE(orResult.test(1));
	REQUIRE(orResult.test(2));
	REQUIRE(orResult.test(3));

	auto xorResult = a ^ b;
	REQUIRE(xorResult.test(0));
	REQUIRE_FALSE(xorResult.test(1));
	REQUIRE_FALSE(xorResult.test(2));
	REQUIRE(xorResult.test(3));
}

TEST_CASE("BitSet firstSet returns lowest set bit index", "[containers][bitset]")
{
	sgc::containers::BitSet<128> bs;
	REQUIRE(bs.firstSet() == -1);

	bs.set(42);
	REQUIRE(bs.firstSet() == 42);

	bs.set(10);
	REQUIRE(bs.firstSet() == 10);

	bs.set(100);
	REQUIRE(bs.firstSet() == 10);
}

TEST_CASE("BitSet all any none predicates", "[containers][bitset]")
{
	sgc::containers::BitSet<8> bs;
	REQUIRE(bs.none());
	REQUIRE_FALSE(bs.any());
	REQUIRE_FALSE(bs.all());

	bs.set(0);
	REQUIRE_FALSE(bs.none());
	REQUIRE(bs.any());
	REQUIRE_FALSE(bs.all());

	bs.setAll();
	REQUIRE(bs.all());
	REQUIRE(bs.any());
	REQUIRE_FALSE(bs.none());
	REQUIRE(bs.count() == 8);
}

TEST_CASE("BitSet setAll and clearAll", "[containers][bitset]")
{
	sgc::containers::BitSet<100> bs;
	bs.setAll();
	REQUIRE(bs.all());
	REQUIRE(bs.count() == 100);

	bs.clearAll();
	REQUIRE(bs.none());
	REQUIRE(bs.count() == 0);
}

TEST_CASE("BitSet NOT operator respects bit count", "[containers][bitset]")
{
	sgc::containers::BitSet<10> bs;
	auto inverted = ~bs;
	REQUIRE(inverted.all());
	REQUIRE(inverted.count() == 10);

	// ビット10以降はセットされていないことを確認
	// (内部的に64ビットワードだが、有効ビットは10個)
	REQUIRE(inverted.size() == 10);
}

TEST_CASE("BitSet constexpr operations", "[containers][bitset]")
{
	constexpr auto makeBitSet = []()
	{
		sgc::containers::BitSet<32> bs;
		bs.set(0);
		bs.set(15);
		bs.set(31);
		return bs;
	};

	constexpr auto bs = makeBitSet();
	STATIC_REQUIRE(bs.test(0));
	STATIC_REQUIRE(bs.test(15));
	STATIC_REQUIRE(bs.test(31));
	STATIC_REQUIRE(bs.count() == 3);
	STATIC_REQUIRE(bs.size() == 32);
	STATIC_REQUIRE(bs.firstSet() == 0);
}
