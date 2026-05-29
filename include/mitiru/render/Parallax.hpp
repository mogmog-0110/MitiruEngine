#pragma once

/// @file Parallax.hpp
/// @brief 2D 横スクロール用の多層パララックス背景。
/// @details `addLayer(tex, scrollMulX, tileX, tileY, offsetY)` で奥行レイヤを積み、
///          `draw(screen, camX, camY, viewW, viewH)` で一括描画。tile=true なら
///          viewport 幅で wrap。手書きの border バグを構造で潰す。

#include <cmath>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/Texture.hpp>

namespace mitiru::render
{

class Parallax
{
public:
	struct Layer
	{
		const Texture* tex = nullptr;
		float scrollMulX = 1.0f;  ///< camera 移動量に対するスクロール率 (0=固定、1=cam と同速、0.3=遠景)
		float scrollMulY = 1.0f;  ///< Y 同様 (上下スクロールしないなら呼び側が 0 を渡す)
		bool  tileX = true;       ///< 横方向に viewport 幅で wrap repeat する
		bool  tileY = false;      ///< 縦方向 wrap (天井 / 床にしないなら false)
		float offsetY = 0.0f;     ///< Y オフセット (地平線位置の微調整)
		sgc::Colorf tint{1.0f, 1.0f, 1.0f, 1.0f};
	};

	void addLayer(const Layer& L) { m_layers.push_back(L); }
	void clear() { m_layers.clear(); }
	[[nodiscard]] int size() const noexcept { return static_cast<int>(m_layers.size()); }

	/// @brief 全レイヤを camera 座標に応じて描画する。
	/// @param camX,camY camera world position
	/// @param viewW,viewH 描画範囲 (screen-space)
	void draw(Screen& screen, float camX, float camY, float viewW, float viewH) const
	{
		for (const auto& L : m_layers)
		{
			if (!L.tex || L.tex->width() <= 0 || L.tex->height() <= 0) { continue; }
			const float tw = static_cast<float>(L.tex->width());
			const float th = static_cast<float>(L.tex->height());

			// スクロール量 (cam を mul で減衰)
			const float sx = camX * L.scrollMulX;
			const float sy = camY * L.scrollMulY;

			// 描画開始位置: 1 タイル前から repeat
			float startX = -std::fmod(sx, tw);
			if (startX > 0.0f) { startX -= tw; }
			float startY = (L.tileY)
				? (-std::fmod(sy, th) - ((-std::fmod(sy, th) > 0.0f) ? th : 0.0f))
				: (-sy + L.offsetY);

			// X tile (常に左→右で view 幅をカバー)
			const int colsX = (L.tileX)
				? static_cast<int>(std::ceil((viewW - startX) / tw)) + 1
				: 1;
			const int rowsY = (L.tileY)
				? static_cast<int>(std::ceil((viewH - startY) / th)) + 1
				: 1;

			for (int row = 0; row < rowsY; ++row)
			{
				for (int col = 0; col < colsX; ++col)
				{
					const float dx = startX + static_cast<float>(col) * tw;
					const float dy = startY + static_cast<float>(row) * th;
					screen.drawSprite(*L.tex, sgc::Rectf{dx, dy, tw, th}, L.tint);
				}
			}
		}
	}

private:
	std::vector<Layer> m_layers;
};

}  // namespace mitiru::render
