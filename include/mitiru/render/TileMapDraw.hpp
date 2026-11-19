#pragma once

/// @file TileMapDraw.hpp
/// @brief タイルマップを 1 関数で描くヘルパ。
/// @details game が row-major な int[w*h] (-1 = 空) を渡すと、tileset の各セルを
///          atlas index で切り出して `Screen::drawSprite` を続けて呼ぶ。同じ texture が
///          続くのでテクスチャバッチにまとまり、1 ドローコールで済む。

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/TileAtlas.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

/// @brief 空でない各タイルについて callback fn(idx, dstRect, srcRect) を呼ぶ、純関数の反復。
/// @details テストしやすくするためと、ゲーム側が drawSprite 以外 (debug overlay 等) に流用できるようにするために分けてある。
/// @return 呼び出した (visible) タイルの数。
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

/// @brief タイルマップを一括描画する (空でない各タイルが 1 drawSprite。同じ texture なので
///        テクスチャバッチで 1 ドローコールにまとまる)。
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

/// @brief 描画先の 1 タイルを、タイルセットの 1 タイルと同じピクセル寸法で描く convenience overload。
inline void drawTiles(Screen& screen, const Texture& tileset, const TileAtlas& atlas,
                     const int* indices, int mapW, int mapH,
                     float worldX, float worldY,
                     const sgc::Colorf& tint = {1.0f, 1.0f, 1.0f, 1.0f})
{
	drawTiles(screen, tileset, atlas, indices, mapW, mapH, worldX, worldY,
	          static_cast<float>(atlas.tileW), static_cast<float>(atlas.tileH), tint);
}

}  // namespace mitiru::render
