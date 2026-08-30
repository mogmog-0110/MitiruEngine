#pragma once
/// @file PixelFont.hpp
/// @brief 美咲フォント(8x8 ドット日本語)のグリフ参照・UTF-8 デコード・計測。
/// @details SDF のアンチエイリアスでなく、ピクセルパーフェクトなドット文字を描くための
///          グリフデータ層。Screen に依存しない純ロジック（描画は PixelText.hpp）。
///          グリフ実データは自動生成の MisakiGlyph_tables.hpp（Unicode 符号化）。

#include <cstdint>
#include <string_view>
#include <vector>

#include <mitiru/render/pixel/MisakiGlyph_tables.hpp>

namespace mitiru::render::pixel
{

/// @brief グリフセルの寸法（8x8 固定）
inline constexpr int kCellSize = 8;

/// @brief Unicode コードポイントからグリフを引く（二分探索）。
/// @return 見つかればグリフ、無ければ nullptr
[[nodiscard]] inline const MisakiGlyph* glyphFor(std::uint32_t cp) noexcept
{
	std::size_t lo = 0, hi = kMisakiGlyphCount;
	while (lo < hi)
	{
		const std::size_t mid = lo + (hi - lo) / 2;
		const std::uint32_t c = kMisakiGlyphs[mid].cp;
		if (c == cp) return &kMisakiGlyphs[mid];
		if (c < cp) lo = mid + 1;
		else hi = mid;
	}
	return nullptr;
}

/// @brief UTF-8 文字列を 1 コードポイントずつ sink へ渡す（ヒープ確保なし）。
/// @details 不正バイトはスキップ（堅牢性優先）。
/// @param sink void(std::uint32_t cp)
template <typename Sink>
inline void forEachCodepoint(std::string_view s, Sink&& sink)
{
	std::size_t i = 0;
	while (i < s.size())
	{
		const auto b0 = static_cast<unsigned char>(s[i]);
		std::uint32_t cp = 0;
		int extra = 0;
		if (b0 < 0x80) { cp = b0; }
		else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; extra = 1; }
		else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; extra = 2; }
		else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; extra = 3; }
		else { ++i; continue; } // 不正な先頭バイト
		if (i + static_cast<std::size_t>(extra) >= s.size()) break;
		bool ok = true;
		for (int k = 1; k <= extra; ++k)
		{
			const auto bk = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
			if ((bk & 0xC0) != 0x80) { ok = false; break; }
			cp = (cp << 6) | (bk & 0x3F);
		}
		i += static_cast<std::size_t>(extra) + 1;
		if (ok) { sink(cp); }
	}
}

/// @brief UTF-8 文字列を Unicode コードポイント列にデコードする。
/// @details 不正バイトはスキップ（堅牢性優先）。
[[nodiscard]] inline std::vector<std::uint32_t> decodeUtf8(std::string_view s)
{
	std::vector<std::uint32_t> out;
	out.reserve(s.size());
	forEachCodepoint(s, [&out](std::uint32_t cp) { out.push_back(cp); });
	return out;
}

/// @brief テキストのピクセル寸法（行送り含む）。
/// @param text UTF-8 文字列（改行 '\n' 可）
/// @param scale 整数拡大率（1=8px セル）
/// @return {幅, 高さ}（ピクセル）
struct PixelSize { int w = 0; int h = 0; };
[[nodiscard]] inline PixelSize measurePixelText(std::string_view text, int scale)
{
	if (scale < 1) scale = 1;
	int x = 0, maxW = 0, lines = 1;
	forEachCodepoint(text, [&](std::uint32_t cp)
	{
		if (cp == '\n') { maxW = (x > maxW) ? x : maxW; x = 0; ++lines; return; }
		const auto* g = glyphFor(cp);
		x += (g ? g->advance : 4) * scale;
	});
	maxW = (x > maxW) ? x : maxW;
	return { maxW, lines * kCellSize * scale };
}

/// @brief テキストの各点灯ピクセルを矩形として列挙する（描画は呼び出し側）。
/// @param text UTF-8 文字列（改行 '\n' 可）
/// @param originX,originY 左上原点（ピクセル）
/// @param scale 整数拡大率
/// @param sink void(int x, int y, int size)。点灯ピクセル矩形を受け取る
template <typename Sink>
inline void forEachPixel(std::string_view text, int originX, int originY, int scale, Sink&& sink)
{
	if (scale < 1) scale = 1;
	int penX = originX, penY = originY;
	forEachCodepoint(text, [&](std::uint32_t cp)
	{
		if (cp == '\n') { penX = originX; penY += kCellSize * scale; return; }
		const auto* g = glyphFor(cp);
		if (g != nullptr)
		{
			for (int row = 0; row < kCellSize; ++row)
			{
				const std::uint8_t bits = g->rows[row];
				if (bits == 0) continue;
				for (int col = 0; col < kCellSize; ++col)
				{
					if (bits & (1 << (7 - col)))
					{
						sink(penX + col * scale, penY + row * scale, scale);
					}
				}
			}
		}
		penX += (g ? g->advance : 4) * scale;
	});
}

} // namespace mitiru::render::pixel
