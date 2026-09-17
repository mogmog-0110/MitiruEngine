#pragma once

/// @file Fnv1a.hpp
/// @brief dirty 検知専用の FNV-1a 64bit ハッシュ (暗号強度は不要)
/// @details GameMemory バイト列や JSON dump 文字列の内容一致判定にだけ使う。
///          衝突耐性より計算コストの低さを優先する。

#include <cstddef>
#include <cstdint>

namespace mitiru::observe
{

/// @brief バイト列を FNV-1a 64 で畳む。衝突耐性ではなく dirty 検知の速さが目的。
[[nodiscard]] inline std::uint64_t fnv1a64(const std::uint8_t* p, std::size_t n) noexcept
{
	std::uint64_t h = 14695981039346656037ull;
	for (std::size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

}  // namespace mitiru::observe
