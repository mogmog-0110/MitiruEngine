#pragma once
/// @file ModernRng.hpp
/// @brief 現代的な音源が使う、シードを固定できる乱数
/// @details rand() や時刻を使うと同じ MML から同じ PCM にならない。ノイズや位相のばらつきは
///          ここの乱数だけを使い、音 1 つごとに (チャンネル, 発音の通し番号) からシードを作る。
///          そうすると、一部のトラックだけを描き直しても、残ったトラックの音は変わらない。

#include <cstdint>

namespace mitiru_mml
{

/// @brief 32bit の整数をかき混ぜてシードにする
[[nodiscard]] inline std::uint32_t mixSeed(std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0) noexcept
{
	std::uint32_t h = a * 0x9E3779B1u;
	h ^= h >> 15;
	h += b * 0x85EBCA77u;
	h ^= h >> 13;
	h *= 0xC2B2AE3Du;
	h += c * 0x27D4EB2Fu;
	h ^= h >> 16;
	h *= 0x85EBCA6Bu;
	h ^= h >> 13;
	return h == 0 ? 0x1u : h;
}

/// @brief xorshift32 の乱数
class ModernRng
{
public:
	explicit ModernRng(std::uint32_t seed = 1) noexcept
		: m_state(seed == 0 ? 1u : seed)
	{
	}

	[[nodiscard]] std::uint32_t nextU32() noexcept
	{
		std::uint32_t x = m_state;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		m_state = x;
		return x;
	}

	/// @brief [-1, 1) の値
	[[nodiscard]] float nextSigned() noexcept
	{
		return static_cast<float>(nextU32() >> 8) * (1.0f / 8388608.0f) - 1.0f;
	}

	/// @brief [0, 1) の値
	[[nodiscard]] float nextUnit() noexcept
	{
		return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f);
	}

private:
	std::uint32_t m_state;
};

} // namespace mitiru_mml
