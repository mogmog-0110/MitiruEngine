#pragma once

/// @file Siv3DRenderer.hpp
/// @brief IRenderer の Siv3D 実装
///
/// sgc::IRenderer を Siv3D の描画関数で実装する。
/// DrawAdapter.hpp と同等の機能を仮想関数として提供する。
///
/// @code
/// sgc::siv3d::Siv3DRenderer renderer;
/// sgc::IRenderer& r = renderer;
/// r.drawRect(rect, color);
/// @endcode

#include <Siv3D.hpp>

#include "sgc/graphics/IRenderer.hpp"
#include "sgc/siv3d/TypeConvert.hpp"

namespace sgc::siv3d
{

/// @brief IRenderer の Siv3D 実装
///
/// sgcの型を受け取り、内部でSiv3Dの描画APIに変換して描画する。
class Siv3DRenderer : public IRenderer
{
public:
	/// @brief 塗りつぶし矩形を描画する
	void drawRect(const AABB2f& rect, const Colorf& color) override
	{
		toSivRect(rect).draw(toSivColorF(color));
	}

	/// @brief 矩形の枠線を描画する
	void drawRectFrame(const AABB2f& rect, float thickness, const Colorf& color) override
	{
		toSivRect(rect).drawFrame(static_cast<double>(thickness), toSivColorF(color));
	}

	/// @brief 塗りつぶし円を描画する
	void drawCircle(const Vec2f& center, float radius, const Colorf& color) override
	{
		s3d::Circle{toSiv(center), static_cast<double>(radius)}.draw(toSivColorF(color));
	}

	/// @brief 円の枠線を描画する
	void drawCircleFrame(const Vec2f& center, float radius, float thickness, const Colorf& color) override
	{
		s3d::Circle{toSiv(center), static_cast<double>(radius)}
			.drawFrame(static_cast<double>(thickness), toSivColorF(color));
	}

	/// @brief 線分を描画する
	void drawLine(const Vec2f& from, const Vec2f& to, float thickness, const Colorf& color) override
	{
		s3d::Line{toSiv(from), toSiv(to)}.draw(static_cast<double>(thickness), toSivColorF(color));
	}

	/// @brief 塗りつぶし三角形を描画する
	void drawTriangle(const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, const Colorf& color) override
	{
		s3d::Triangle{toSiv(p0), toSiv(p1), toSiv(p2)}.draw(toSivColorF(color));
	}

	/// @brief フェード用の半透明オーバーレイを描画する
	void drawFadeOverlay(float alpha, const Colorf& color = Colorf::black()) override
	{
		const auto overlayColor = color.withAlpha(alpha);
		s3d::Scene::Rect().draw(toSivColorF(overlayColor));
	}

	/// @brief 背景色をクリアする
	void clearBackground(const Colorf& color) override
	{
		s3d::Scene::SetBackground(toSivColorF(color));
	}
};

} // namespace sgc::siv3d
