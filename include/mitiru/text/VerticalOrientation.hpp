#pragma once

/// @file VerticalOrientation.hpp
/// @brief UAX #50 の Vertical_Orientation に基づく縦書きの字の向き。
/// @details 書体に依らない既定値で、書体の vert は見ない。Tu と Tr は書体に縦組みの字形があれば使い、無ければ Tu は立て、Tr は寝かせる。

#include <algorithm>
#include <cstdint>
#include <iterator>

#include <mitiru/text/detail/VerticalOrientationData.hpp>

namespace mitiru::text
{

enum class VerticalOrientation : std::uint8_t
{
	Upright,          ///< U: 符号表と同じ向き
	Rotated,          ///< R: 時計回りに 90 度寝かせる
	TransformUpright, ///< Tu: 縦組みの字形、無ければ立てる
	TransformRotated, ///< Tr: 縦組みの字形、無ければ寝かせる
};

[[nodiscard]] inline VerticalOrientation verticalOrientation(std::uint32_t codepoint) noexcept
{
	const auto* first = std::begin(detail::kVerticalOrientationRanges);
	const auto* last = std::end(detail::kVerticalOrientationRanges);
	const auto* it = std::upper_bound(first, last, codepoint,
		[](std::uint32_t cp, const detail::VerticalOrientationRange& r) { return cp < r.first; });
	if (it == first) { return VerticalOrientation::Rotated; }
	return static_cast<VerticalOrientation>((it - 1)->value);
}

/// @brief 直前の字と同じ塊として向きを決める字か。
/// @details UAX #50 の 3.2.1。向きは塊の先頭の字で決める。囲み記号 (Me) が付く塊は常に U。
[[nodiscard]] constexpr bool continuesCluster(std::uint32_t cp) noexcept
{
	return (cp >= 0x0300 && cp <= 0x036F)      // 結合用ダイアクリティカルマーク
	    || (cp >= 0x1AB0 && cp <= 0x1AFF)
	    || (cp >= 0x1DC0 && cp <= 0x1DFF)
	    || (cp >= 0x20D0 && cp <= 0x20FF)      // 記号用の結合マーク (囲みを含む)
	    || cp == 0x3099 || cp == 0x309A        // かなの濁点と半濁点 (結合)
	    || (cp >= 0xFE00 && cp <= 0xFE0F)      // 異体字選択子
	    || (cp >= 0xFE20 && cp <= 0xFE2F)
	    || cp == 0x200D                        // ZWJ
	    || (cp >= 0xE0100 && cp <= 0xE01EF);   // 異体字選択子の補助
}

[[nodiscard]] constexpr bool isEnclosingMark(std::uint32_t cp) noexcept
{
	return (cp >= 0x20DD && cp <= 0x20E0) || (cp >= 0x20E2 && cp <= 0x20E4)
	    || (cp >= 0xA670 && cp <= 0xA672);
}

} // namespace mitiru::text
