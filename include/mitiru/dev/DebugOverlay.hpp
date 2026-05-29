#pragma once

/// @file DebugOverlay.hpp
/// @brief 開発時 visual デバッグの描画ヘルパ集 (AABB / 十字 / タイルグリッド / FPS テキスト)。
/// @details game 側が toggle フラグを持ち、ON のときだけこれらを呼ぶ。engine は描く部品だけ
///          提供 (philosophy: 主ゲーム窓に debug UI を常駐させない、game が必要な時だけ呼ぶ)。

#include <cmath>
#include <cstdio>
#include <string>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/pixel/PixelText.hpp>

namespace mitiru::dev
{

/// @brief AABB の輪郭を太さ thickness で描く。`Screen::drawRectFrame` の薄いラッパ。
inline void drawAabbOutline(Screen& s, const sgc::Rectf& r, const sgc::Colorf& color,
                            float thickness = 1.0f)
{
	s.drawRectFrame(r, color, thickness);
}

/// @brief (x,y) を中心に十字マーク (size px の半長)。entity 位置可視化用。
inline void drawCross(Screen& s, float x, float y, float size, const sgc::Colorf& color)
{
	s.drawRect(sgc::Rectf{x - size, y - 0.5f, size * 2.0f, 1.0f}, color);
	s.drawRect(sgc::Rectf{x - 0.5f, y - size, 1.0f, size * 2.0f}, color);
}

/// @brief タイル格子線を viewport に対して描く。camX/camY を引いた screen 座標に出る。
/// @param viewport screen 描画範囲 (左上 + 寸法)
/// @param tileW/tileH tile ピクセル寸法
/// @param camX/camY camera world 左上 (= viewport の world origin)
inline void drawTileGrid(Screen& s, const sgc::Rectf& viewport,
                         float tileW, float tileH, float camX, float camY,
                         const sgc::Colorf& color)
{
	if (tileW <= 0.0f || tileH <= 0.0f) { return; }
	// camX を tile 境界に snap (camX を超えない最大の tile boundary)
	const float startX = std::floor(camX / tileW) * tileW - camX + viewport.x();
	const float startY = std::floor(camY / tileH) * tileH - camY + viewport.y();

	const float rightLim = viewport.x() + viewport.width();
	const float botLim   = viewport.y() + viewport.height();

	for (float x = startX; x < rightLim; x += tileW)
	{
		s.drawRect(sgc::Rectf{x, viewport.y(), 1.0f, viewport.height()}, color);
	}
	for (float y = startY; y < botLim; y += tileH)
	{
		s.drawRect(sgc::Rectf{viewport.x(), y, viewport.width(), 1.0f}, color);
	}
}

/// @brief FPS 数値を pixel font で描く。
inline void drawFps(Screen& s, float x, float y, float fps,
                    int scale = 2, const sgc::Colorf& color = {1, 1, 0, 1})
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "FPS %3d", static_cast<int>(fps + 0.5f));
	mitiru::render::pixel::drawPixelText(s, x, y, buf, scale, color);
}

}  // namespace mitiru::dev
