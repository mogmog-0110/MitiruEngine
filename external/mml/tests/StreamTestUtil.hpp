#pragma once
/// @file StreamTestUtil.hpp
/// @brief ストリーミングの検査で共有する道具: 塊の大きさの決め方、最後まで取り出す関数、ビットの一致の比較

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace mitiru_mml_test
{

/// @brief 曲全体を 1 度に取り出すのに十分な塊の大きさ (フレーム)
inline constexpr std::size_t WHOLE_SONG = std::size_t{1} << 21;

/// @brief 塊の大きさの決め方 (呼ぶ回数の番号から大きさを返す)
using ChunkPlan = std::function<std::size_t(std::size_t)>;

inline ChunkPlan fixedChunk(std::size_t n)
{
	return [n](std::size_t) { return n; };
}

/// @brief 1〜maxSize のばらばらの大きさ (シード固定の線形合同法)
inline ChunkPlan randomChunks(std::uint32_t seed, std::size_t maxSize)
{
	auto state = std::make_shared<std::uint32_t>(seed);
	return [state, maxSize](std::size_t)
	{
		*state = *state * 1664525u + 1013904223u;
		return static_cast<std::size_t>(1 + (*state >> 8) % maxSize);
	};
}

/// @brief ストリームを最後まで取り出す。read が 0 を返すまで
template <class Stream>
std::vector<float> drain(Stream& stream, const ChunkPlan& plan)
{
	std::vector<float> all;
	std::vector<float> chunk;
	for (std::size_t call = 0;; ++call)
	{
		const std::size_t want = plan(call);
		chunk.assign(want * 2, 0.0f);
		const std::size_t got = stream.read(chunk.data(), want);
		REQUIRE(got <= want);
		all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got * 2));
		if (got < want)
		{
			break;
		}
	}
	return all;
}

inline bool sameBits(const std::vector<float>& a, const std::vector<float>& b)
{
	const bool same = a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
	if (!same)
	{
		std::size_t first = 0;
		while (first < a.size() && first < b.size() && std::memcmp(&a[first], &b[first], sizeof(float)) == 0)
		{
			++first;
		}
		UNSCOPED_INFO("sizes " << a.size() << " and " << b.size() << ", first different float at " << first);
	}
	return same;
}

} // namespace mitiru_mml_test
