#pragma once

/// @file ScreenRenderer.hpp
/// @brief mitiru::Screen → sgc::IRenderer アダプター
/// @details Screenの描画APIをsgc::IRendererインターフェースに適合させる。
///          これによりsgcのUI、Transition、Scene等の全機能がScreenで描画可能になる。

#include <sgc/graphics/IRenderer.hpp>
#include <sgc/math/Geometry.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>
#include <mitiru/core/Screen.hpp>

namespace mitiru::adapter
{

/// @brief mitiru::ScreenをsgcのIRendererとして使用するアダプター
class ScreenRenderer : public sgc::IRenderer
{
public:
	/// @brief コンストラクタ
	/// @param screen 描画先のScreen（非所有）
	explicit ScreenRenderer(Screen& screen) noexcept
		: m_screen(screen)
	{
	}

	/// @brief 塗りつぶし矩形を描画する
	/// @param rect AABB2f矩形（min/max座標）
	/// @param color 塗りつぶし色
	void drawRect(const sgc::AABB2f& rect, const sgc::Colorf& color) override
	{
		m_screen.drawRect(aabbToRectf(rect), color);
	}

	/// @brief 矩形の枠線を描画する
	/// @param rect AABB2f矩形（min/max座標）
	/// @param thickness 線の太さ（ピクセル）
	/// @param color 線の色
	void drawRectFrame(const sgc::AABB2f& rect, float thickness, const sgc::Colorf& color) override
	{
		const auto r = aabbToRectf(rect);
		m_screen.drawRectFrame(r, color, thickness);
	}

	/// @brief 塗りつぶし円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 塗りつぶし色
	void drawCircle(const sgc::Vec2f& center, float radius, const sgc::Colorf& color) override
	{
		m_screen.drawCircle(center, radius, color);
	}

	/// @brief 円の枠線を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param thickness 線の太さ（未使用：Screenに枠線円APIが無いため塗りつぶしで近似）
	/// @param color 線の色
	void drawCircleFrame(const sgc::Vec2f& center, float radius, float /*thickness*/, const sgc::Colorf& color) override
	{
		m_screen.drawCircle(center, radius, color);
	}

	/// @brief 線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param thickness 線の太さ
	/// @param color 線の色
	void drawLine(const sgc::Vec2f& from, const sgc::Vec2f& to, float thickness, const sgc::Colorf& color) override
	{
		m_screen.drawLine(from, to, color, thickness);
	}

	/// @brief 三角形を描画する
	/// @param p0 頂点0
	/// @param p1 頂点1
	/// @param p2 頂点2
	/// @param color 塗りつぶし色
	void drawTriangle(const sgc::Vec2f& p0, const sgc::Vec2f& p1, const sgc::Vec2f& p2, const sgc::Colorf& color) override
	{
		m_screen.drawTriangle(p0, p1, p2, color);
	}

	/// @brief フェード用の半透明オーバーレイを描画する
	/// @param alpha 不透明度 [0,1]
	/// @param color ベース色
	void drawFadeOverlay(float alpha, const sgc::Colorf& color) override
	{
		if (alpha <= 0.0f) return;
		const sgc::Colorf overlayColor{color.r, color.g, color.b, alpha};
		m_screen.drawRect(
			sgc::Rectf{0.0f, 0.0f,
			           static_cast<float>(m_screen.width()),
			           static_cast<float>(m_screen.height())},
			overlayColor);
	}

	/// @brief 背景色をクリアする
	/// @param color 背景色
	void clearBackground(const sgc::Colorf& color) override
	{
		m_screen.clear(color);
	}

private:
	/// @brief AABB2f → Rectf変換
	/// @param aabb AABB2f（min/max座標形式）
	/// @return Rectf（x,y,width,height形式）
	[[nodiscard]] static sgc::Rectf aabbToRectf(const sgc::AABB2f& aabb) noexcept
	{
		return {aabb.min.x, aabb.min.y,
		        aabb.max.x - aabb.min.x,
		        aabb.max.y - aabb.min.y};
	}

	Screen& m_screen;  ///< 描画先Screen（非所有）
};

} // namespace mitiru::adapter
