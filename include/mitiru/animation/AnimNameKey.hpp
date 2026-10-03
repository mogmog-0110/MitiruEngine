#pragma once

/// @file AnimNameKey.hpp
/// @brief 状態機械の状態やグラフの名前から作る 32 bit の鍵 (FNV-1a)。
/// @details 実行時の状態 (AnimGraphState) は番号と一緒にこの鍵を持つ。JSON を読み直して番号がずれても、
///          鍵で同じ名前の状態を引き直せる。ツール窓 (anim) も同じ鍵でグラフの JSON と状態を組にする。

#include <cstdint>
#include <string_view>

namespace mitiru::animation
{

[[nodiscard]] constexpr std::uint32_t animNameKey(std::string_view name) noexcept
{
	std::uint32_t h = 2166136261u;
	for (const char c : name)
	{
		h ^= static_cast<std::uint8_t>(c);
		h *= 16777619u;
	}
	return h;
}

} // namespace mitiru::animation
