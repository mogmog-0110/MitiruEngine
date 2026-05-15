#pragma once

/// @file TileRenderer.hpp
/// @brief Grid2D<int>をScreenに描画するユーティリティ
/// @details カラーマッパー関数とグリッド線オプションを提供する。

#include <functional>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>
#include <mitiru/util/Grid2D.hpp>
#include <mitiru/core/Screen.hpp>

namespace mitiru::util
{

/// @brief タイルカラーマッパー型（セル値→色）
using TileColorMapper = std::function<sgc::Colorf(int cellValue)>;

/// @brief Grid2D<int>をScreenに描画するユーティリティ
struct TileRenderer
{
	/// @brief Grid2Dを画面に描画する
	/// @param screen 描画先サーフェス
	/// @param grid 描画するグリッド
	/// @param origin 描画開始位置（左上、スクリーン座標）
	/// @param tileSize 1タイルのピクセルサイズ
	/// @param colorMapper セル値→色の変換関数
	/// @param drawGridLines グリッド線を描画するか
	/// @param gridLineColor グリッド線の色
	static void draw(
		Screen& screen,
		const Grid2D<int>& grid,
		const sgc::Vec2f& origin,
		float tileSize,
		const TileColorMapper& colorMapper,
		bool drawGridLines = false,
		const sgc::Colorf& gridLineColor = sgc::Colorf{0.3f, 0.3f, 0.3f, 1.0f})
	{
		/// タイルを描画する
		for (int y = 0; y < grid.height(); ++y)
		{
			for (int x = 0; x < grid.width(); ++x)
			{
				const auto color = colorMapper(grid.at(x, y));
				const sgc::Rectf rect{
					origin.x + static_cast<float>(x) * tileSize,
					origin.y + static_cast<float>(y) * tileSize,
					tileSize, tileSize
				};
				screen.drawRect(rect, color);
			}
		}

		/// グリッド線を描画する
		if (drawGridLines)
		{
			const float totalW = static_cast<float>(grid.width()) * tileSize;
			const float totalH = static_cast<float>(grid.height()) * tileSize;

			for (int x = 0; x <= grid.width(); ++x)
			{
				const float px = origin.x + static_cast<float>(x) * tileSize;
				screen.drawLine({px, origin.y}, {px, origin.y + totalH}, gridLineColor, 1.0f);
			}
			for (int y = 0; y <= grid.height(); ++y)
			{
				const float py = origin.y + static_cast<float>(y) * tileSize;
				screen.drawLine({origin.x, py}, {origin.x + totalW, py}, gridLineColor, 1.0f);
			}
		}
	}

	/// @brief 描画呼び出し数をカウントする（テスト用）
	/// @param grid グリッド
	/// @param drawGridLines グリッド線を含めるか
	/// @return 予想される描画コール数
	[[nodiscard]] static int expectedDrawCalls(const Grid2D<int>& grid, bool drawGridLines) noexcept
	{
		int calls = grid.width() * grid.height(); // タイル描画
		if (drawGridLines)
		{
			calls += (grid.width() + 1) + (grid.height() + 1); // 線描画
		}
		return calls;
	}
};

} // namespace mitiru::util
