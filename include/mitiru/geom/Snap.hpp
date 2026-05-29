#pragma once

/// @file Snap.hpp
/// @brief ピクセル/タイル変換と snap の純関数群。
/// @details 全タイル系ゲームが game 側で手書きする `snap()` / `cellRange()` / `cellRect()` を
///          共通化。world ↔ tile 変換の取り違えバグを構造で潰す。

#include <algorithm>
#include <cmath>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>

namespace mitiru::geom
{

/// @brief 浮動小数を整数ピクセルに丸める (banker rounding でなく `floor(x + 0.5)`)。
[[nodiscard]] inline float snapToPixel(float v) noexcept
{
	return std::floor(v + 0.5f);
}

[[nodiscard]] inline sgc::Vec2f snapToPixel(sgc::Vec2f v) noexcept
{
	return sgc::Vec2f{ snapToPixel(v.x), snapToPixel(v.y) };
}

/// @brief world 座標 (px) → tile index (col, row) (truncate toward -∞)。
[[nodiscard]] inline int worldToTileCol(float x, float tileW) noexcept
{
	return static_cast<int>(std::floor(x / tileW));
}
[[nodiscard]] inline int worldToTileRow(float y, float tileH) noexcept
{
	return static_cast<int>(std::floor(y / tileH));
}

/// @brief tile index → world 座標の左上 (px)。
[[nodiscard]] inline sgc::Vec2f tileToWorld(int col, int row, float tileW, float tileH) noexcept
{
	return sgc::Vec2f{ col * tileW, row * tileH };
}

/// @brief world rect → 占有 tile index 範囲 [tx0..tx1, ty0..ty1] (inclusive)。
/// @details 右下 sliver の誤拾いを避けるため 1e-4 縮める (#15 と同じ規約)。
inline void worldRectToTileRange(const sgc::Rectf& r, float tileW, float tileH,
                                 int& tx0, int& ty0, int& tx1, int& ty1) noexcept
{
	const float right  = r.x() + r.width()  - 1e-4f;
	const float bottom = r.y() + r.height() - 1e-4f;
	tx0 = worldToTileCol(r.x(),    tileW);
	ty0 = worldToTileRow(r.y(),    tileH);
	tx1 = worldToTileCol(right,    tileW);
	ty1 = worldToTileRow(bottom,   tileH);
}

/// @brief tile index → 1 タイルの world rect。
[[nodiscard]] inline sgc::Rectf tileCellRect(int col, int row, float tileW, float tileH) noexcept
{
	return sgc::Rectf{ col * tileW, row * tileH, tileW, tileH };
}

}  // namespace mitiru::geom
