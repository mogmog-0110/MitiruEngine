#pragma once

/// @file DxLibRenderer.hpp
/// @brief IRenderer の DxLib 実装
///
/// sgc::IRenderer を DxLib の描画関数で実装する。
/// DxLib.hが利用可能な環境でのみコンパイルされる。
///
/// @note このファイルはDxLib.hに依存するため、CI対象外。
///
/// @code
/// sgc::dxlib::DxLibRenderer renderer(800, 600);
/// sgc::IRenderer& r = renderer;
/// r.drawRect(rect, color);
/// @endcode

#include <DxLib.h>
#include <algorithm>
#include <cmath>

#include "sgc/graphics/IRenderer.hpp"
#include "sgc/dxlib/TypeConvert.hpp"

namespace sgc::dxlib
{

/// @brief IRenderer の DxLib 実装
///
/// sgcの型を受け取り、内部でDxLibの描画APIに変換して描画する。
class DxLibRenderer : public IRenderer
{
public:
	/// @brief コンストラクタ
	/// @param screenWidth 画面幅（フェードオーバーレイ・背景クリアに使用）
	/// @param screenHeight 画面高さ
	explicit DxLibRenderer(int screenWidth = 800, int screenHeight = 600) noexcept
		: m_screenWidth(screenWidth)
		, m_screenHeight(screenHeight)
	{
	}

	/// @brief 塗りつぶし矩形を描画する
	void drawRect(const AABB2f& rect, const Colorf& color) override
	{
		DrawBoxAA(
			rect.min.x, rect.min.y,
			rect.max.x, rect.max.y,
			toDxColor(color), TRUE);
	}

	/// @brief 矩形の枠線を描画する
	void drawRectFrame(const AABB2f& rect, float thickness, const Colorf& color) override
	{
		const auto dxColor = toDxColor(color);

		if (thickness <= 1.5f)
		{
			// 薄い枠線はDxLibの非塗りつぶしBoxで描画
			DrawBoxAA(
				rect.min.x, rect.min.y,
				rect.max.x, rect.max.y,
				dxColor, FALSE);
		}
		else
		{
			// 太い枠線は4辺の塗りつぶし矩形で描画（DxLibに太さ指定なし）
			const float t = thickness;
			// 上辺
			DrawBoxAA(rect.min.x, rect.min.y, rect.max.x, rect.min.y + t, dxColor, TRUE);
			// 下辺
			DrawBoxAA(rect.min.x, rect.max.y - t, rect.max.x, rect.max.y, dxColor, TRUE);
			// 左辺
			DrawBoxAA(rect.min.x, rect.min.y + t, rect.min.x + t, rect.max.y - t, dxColor, TRUE);
			// 右辺
			DrawBoxAA(rect.max.x - t, rect.min.y + t, rect.max.x, rect.max.y - t, dxColor, TRUE);
		}
	}

	/// @brief 塗りつぶし円を描画する
	void drawCircle(const Vec2f& center, float radius, const Colorf& color) override
	{
		const int segments = std::max(16, static_cast<int>(radius * 2.0f));
		DrawCircleAA(center.x, center.y, radius, segments, toDxColor(color), TRUE);
	}

	/// @brief 円の枠線を描画する
	///
	/// @note DxLibのDrawCircleAAは枠線の太さ指定をサポートしないため、
	///       thickness引数は無視される。
	void drawCircleFrame(
		const Vec2f& center, float radius,
		float /*thickness*/, const Colorf& color) override
	{
		const int segments = std::max(16, static_cast<int>(radius * 2.0f));
		DrawCircleAA(center.x, center.y, radius, segments, toDxColor(color), FALSE);
	}

	/// @brief 線分を描画する
	void drawLine(
		const Vec2f& from, const Vec2f& to,
		float thickness, const Colorf& color) override
	{
		DrawLineAA(from.x, from.y, to.x, to.y, toDxColor(color), thickness);
	}

	/// @brief 塗りつぶし三角形を描画する
	void drawTriangle(
		const Vec2f& p0, const Vec2f& p1, const Vec2f& p2,
		const Colorf& color) override
	{
		DrawTriangleAA(
			p0.x, p0.y,
			p1.x, p1.y,
			p2.x, p2.y,
			toDxColor(color), TRUE);
	}

	/// @brief フェード用の半透明オーバーレイを描画する
	void drawFadeOverlay(float alpha, const Colorf& color = Colorf::black()) override
	{
		const int blendAlpha = toDxAlpha(alpha);
		if (blendAlpha <= 0) return;

		SetDrawBlendMode(DX_BLENDMODE_ALPHA, blendAlpha);
		DrawBox(0, 0, m_screenWidth, m_screenHeight, toDxColor(color), TRUE);
		SetDrawBlendMode(DX_BLENDMODE_NOBLEND, 0);
	}

	/// @brief 背景色をクリアする
	void clearBackground(const Colorf& color) override
	{
		DrawBox(0, 0, m_screenWidth, m_screenHeight, toDxColor(color), TRUE);
	}

private:
	int m_screenWidth;   ///< 画面幅
	int m_screenHeight;  ///< 画面高さ
};

} // namespace sgc::dxlib
