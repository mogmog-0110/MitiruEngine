#pragma once

/// @file TileMapDraw.hpp
/// @brief タイルマップ 1 関数描画ヘルパ。
/// @details game が row-major な int[w*h] (-1 = 空) を渡すと、tileset の各セルを
///          atlas index で切り出して連続 `Screen::drawSprite` する。同一 texture
///          連続なので ADR 0009 のテクスチャバッチに合流し 1 ドローコールに集約。

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/Texture.hpp>

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

/// @brief atlas index → tileset 内 src rect の純関数 (テスト容易性)。
[[nodiscard]] inline sgc::Rectf tileSrcRect(const TileAtlas& a, int idx) noexcept
{
	const int x = (idx % a.cols) * a.tileW;
	const int y = (idx / a.cols) * a.tileH;
	return sgc::Rectf{static_cast<float>(x), static_cast<float>(y),
	                  static_cast<float>(a.tileW), static_cast<float>(a.tileH)};
}

/// @brief 各非空タイルに対し callback fn(idx, dstRect, srcRect) を呼ぶ純関数 iteration。
/// @details テスト容易性と、ゲーム側が drawSprite 以外 (debug overlay 等) に流用するため分離。
/// @return 呼び出された (visible) タイル数。
template <typename Fn>
inline int forEachVisibleTile(const TileAtlas& a, const int* indices, int mapW, int mapH,
                              float worldX, float worldY, float dstW, float dstH, Fn&& fn)
{
	if (!indices || mapW <= 0 || mapH <= 0 || a.cols <= 0
	    || a.tileW <= 0 || a.tileH <= 0) { return 0; }
	int count = 0;
	for (int y = 0; y < mapH; ++y)
	{
		for (int x = 0; x < mapW; ++x)
		{
			const int idx = indices[y * mapW + x];
			if (idx < 0) { continue; }
			const sgc::Rectf dst{worldX + x * dstW, worldY + y * dstH, dstW, dstH};
			fn(idx, dst, tileSrcRect(a, idx));
			++count;
		}
	}
	return count;
}

/// @brief タイルマップを一括描画する (各非空タイル = 1 drawSprite、同一 texture なので
///        ADR 0009 のテクスチャバッチで 1 ドローコールに合流)。
inline void drawTiles(Screen& screen, const Texture& tileset, const TileAtlas& atlas,
                     const int* indices, int mapW, int mapH,
                     float worldX, float worldY,
                     float dstTileW, float dstTileH,
                     const sgc::Colorf& tint = {1.0f, 1.0f, 1.0f, 1.0f})
{
	forEachVisibleTile(atlas, indices, mapW, mapH, worldX, worldY, dstTileW, dstTileH,
		[&](int /*idx*/, const sgc::Rectf& dst, const sgc::Rectf& src)
		{
			screen.drawSprite(tileset, dst, src, tint);
		});
}

/// @brief 描画先ピクセル = タイルセットの 1 タイルピクセルと等寸の convenience overload。
inline void drawTiles(Screen& screen, const Texture& tileset, const TileAtlas& atlas,
                     const int* indices, int mapW, int mapH,
                     float worldX, float worldY,
                     const sgc::Colorf& tint = {1.0f, 1.0f, 1.0f, 1.0f})
{
	drawTiles(screen, tileset, atlas, indices, mapW, mapH, worldX, worldY,
	          static_cast<float>(atlas.tileW), static_cast<float>(atlas.tileH), tint);
}

}  // namespace mitiru::render
