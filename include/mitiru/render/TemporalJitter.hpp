#pragma once

/// @file TemporalJitter.hpp
/// @brief TAA が射影をずらす量 (Halton 2,3 の 8 相)
/// @details 描画だけが使う。相の番号はレンダラが数えるフレームから決まり、GameMemory は読まない。
///          同じ入力を同じフレーム数だけ流せば同じ列になるので、TAA を入れた撮影も再現する。

#include <cstdint>

namespace mitiru::render
{

/// @brief 画素単位のずらし。x は右、y は下が正で、どちらも [-0.5, 0.5)
struct JitterOffset
{
	float x = 0.0f;
	float y = 0.0f;
};

inline constexpr std::uint32_t kTemporalJitterPhases = 8;

/// @brief Halton 列 (基数 base) の index 番目。index 0 は 0
[[nodiscard]] constexpr float haltonRadicalInverse(std::uint32_t index, std::uint32_t base) noexcept
{
	float result = 0.0f;
	float digit = 1.0f / static_cast<float>(base);
	while (index > 0)
	{
		result += static_cast<float>(index % base) * digit;
		index /= base;
		digit /= static_cast<float>(base);
	}
	return result;
}

/// @brief 履歴を作り直してから frameIndex 番目のフレームのずらし (phases 相で一巡)
/// @details Halton 列を 1 から数えるのは、8 相の y の平均を 0 にするため
[[nodiscard]] constexpr JitterOffset temporalJitter(std::uint32_t frameIndex,
                                                    std::uint32_t phases = kTemporalJitterPhases) noexcept
{
	const std::uint32_t i = frameIndex % (phases > 0 ? phases : 1u) + 1;
	return {haltonRadicalInverse(i, 2) - 0.5f, haltonRadicalInverse(i, 3) - 0.5f};
}

/// @brief 画素単位のずらしを NDC の平行移動へ直す。NDC の y は上が正
[[nodiscard]] constexpr JitterOffset jitterToNdc(JitterOffset px, float width, float height) noexcept
{
	if (width <= 0.0f || height <= 0.0f) { return {}; }
	return {2.0f * px.x / width, -2.0f * px.y / height};
}

} // namespace mitiru::render
