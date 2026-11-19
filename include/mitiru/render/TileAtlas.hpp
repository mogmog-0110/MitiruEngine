#pragma once

/// @file TileAtlas.hpp
/// @brief タイルセット (tileset PNG) の格子設定。Canvas (module/DrawCommands.hpp) が重い Screen.hpp を引かずに使えるよう、TileMapDraw.hpp から分けた。

#include <sgc/math/Rect.hpp>

namespace mitiru::render
{

/// @brief タイルセット (tileset PNG) の格子設定。
/// @details `tileW * tileH` がソースセル寸法、`cols` は横並び数 (1 行あたりのタイル数)。
///          atlas index `i` → src rect (i%cols * tileW, i/cols * tileH, tileW, tileH)。
struct TileAtlas
{
	int tileW;
	int tileH;
	int cols;
};

/// @brief atlas index から tileset 内の src rect を求める純関数 (テスト容易性のため)。
[[nodiscard]] inline sgc::Rectf tileSrcRect(const TileAtlas& a, int idx) noexcept
{
	const int x = (idx % a.cols) * a.tileW;
	const int y = (idx / a.cols) * a.tileH;
	return sgc::Rectf{static_cast<float>(x), static_cast<float>(y),
	                  static_cast<float>(a.tileW), static_cast<float>(a.tileH)};
}

}  // namespace mitiru::render
