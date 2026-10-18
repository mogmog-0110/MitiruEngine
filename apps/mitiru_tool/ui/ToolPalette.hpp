#pragma once

/// @file ToolPalette.hpp
/// @brief 窓の中でソフトウェア描画する絵 (ナビの地図、状態機械の図、軌跡、ステージの図) の色
/// @details tool_look.rcss の面と字の色から、Windows のアプリのテーマ (ライト / ダーク) に合わせて作る。
///          値の出典は docs/TOOL_LOOK.md。

#include <mitiru/ui_rml/SystemTheme.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace mitiru::tool
{

using PaletteRgb = std::array<std::uint8_t, 3>;

struct ToolPalette
{
	PaletteRgb paper;      ///< 絵の地 (SolidBackgroundFillColorSecondary)
	PaletteRgb gridMinor;  ///< 字の色を地へ 6% 混ぜた細い格子
	PaletteRgb grid;       ///< 12%
	PaletteRgb fill;       ///< 面 (床・枠の内側) 16%
	PaletteRgb edge;       ///< 面の縁 40%
	PaletteRgb strong;     ///< 塗りつぶし 55%
	PaletteRgb ink;        ///< 字と線 (TextFillColorPrimary)
	PaletteRgb mute;       ///< 副次の字と線 (60%)
	PaletteRgb faint;      ///< 三次の字 (45%)
	PaletteRgb accent;     ///< AccentFillColorDefault
	PaletteRgb critical;   ///< SystemFillColorCritical
	PaletteRgb highlight;  ///< 選んだ物の縁 (SystemFillColorCaution)
};

namespace palette_detail
{

inline std::string hex(const PaletteRgb& c)
{
	char buf[8];
	std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c[0], c[1], c[2]);
	return buf;
}

constexpr PaletteRgb mix(PaletteRgb a, PaletteRgb b, double t)
{
	const auto ch = [t](std::uint8_t x, std::uint8_t y) {
		return static_cast<std::uint8_t>(x + (static_cast<double>(y) - static_cast<double>(x)) * t + 0.5);
	};
	return { ch(a[0], b[0]), ch(a[1], b[1]), ch(a[2], b[2]) };
}

constexpr ToolPalette make(PaletteRgb paper, PaletteRgb text, PaletteRgb accent, PaletteRgb critical,
                           PaletteRgb caution)
{
	return { paper, mix(paper, text, 0.06), mix(paper, text, 0.12), mix(paper, text, 0.16), mix(paper, text, 0.40),
	         mix(paper, text, 0.55), text, mix(paper, text, 0.60), mix(paper, text, 0.45), accent, critical, caution };
}

} // namespace palette_detail

/// 状態ごとの帯の色 (RCSS の data-style に渡す "#rrggbb")。字の色を地へ混ぜた濃さを、状態の番号で選ぶ
inline const std::array<std::string, 6>& toolBandShades();

/// 窓を開いた時の Windows のテーマで 1 回だけ決める
inline const ToolPalette& toolPalette()
{
	static const ToolPalette palette = ui_rml::systemPrefersDark()
		? palette_detail::make({ 0x1c, 0x1c, 0x1c }, { 0xff, 0xff, 0xff }, { 0x4c, 0xc2, 0xff }, { 0xff, 0x99, 0xa4 },
		               { 0xfc, 0xe1, 0x00 })
		: palette_detail::make({ 0xee, 0xee, 0xee }, { 0x1a, 0x1a, 0x1a }, { 0x00, 0x67, 0xc0 }, { 0xc4, 0x2b, 0x1c },
		               { 0x9d, 0x5d, 0x00 });
	return palette;
}

inline const std::array<std::string, 6>& toolBandShades()
{
	static const std::array<std::string, 6> shades = [] {
		const ToolPalette& p = toolPalette();
		const double t[6] = { 0.06, 0.10, 0.14, 0.08, 0.12, 0.17 };
		std::array<std::string, 6> out;
		for (std::size_t i = 0; i < out.size(); ++i) { out[i] = palette_detail::hex(palette_detail::mix(p.paper, p.ink, t[i])); }
		return out;
	}();
	return shades;
}

} // namespace mitiru::tool
