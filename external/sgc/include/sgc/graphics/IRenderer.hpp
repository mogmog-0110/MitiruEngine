#pragma once

/// @file IRenderer.hpp
/// @brief 抽象描画インターフェース
///
/// フレームワーク非依存の描画APIを定義する。
/// Siv3D、DxLib等の具体的な描画実装はこのインターフェースを実装する。
///
/// @note テキスト描画はフォント型がフレームワーク固有のため除外。
///
/// @code
/// class MyRenderer : public sgc::IRenderer {
/// public:
///     void drawRect(const AABB2f& rect, const Colorf& color) override { /* ... */ }
///     // ...
/// };
/// @endcode

#include "sgc/math/Geometry.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/types/Color.hpp"

namespace sgc
{

/// @brief 抽象描画インターフェース
///
/// 幾何プリミティブ（矩形、円、線、三角形）の描画を抽象化する。
/// フェードオーバーレイや背景色のクリアもサポートする。
class IRenderer
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IRenderer() = default;

	/// @brief 塗りつぶし矩形を描画する
	/// @param rect 矩形
	/// @param color 塗りつぶし色
	virtual void drawRect(const AABB2f& rect, const Colorf& color) = 0;

	/// @brief 矩形の枠線を描画する
	/// @param rect 矩形
	/// @param thickness 線の太さ（ピクセル）
	/// @param color 線の色
	virtual void drawRectFrame(const AABB2f& rect, float thickness, const Colorf& color) = 0;

	/// @brief 塗りつぶし円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 塗りつぶし色
	virtual void drawCircle(const Vec2f& center, float radius, const Colorf& color) = 0;

	/// @brief 円の枠線を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param thickness 線の太さ
	/// @param color 線の色
	virtual void drawCircleFrame(const Vec2f& center, float radius, float thickness, const Colorf& color) = 0;

	/// @brief 線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param thickness 線の太さ
	/// @param color 線の色
	virtual void drawLine(const Vec2f& from, const Vec2f& to, float thickness, const Colorf& color) = 0;

	/// @brief 塗りつぶし三角形を描画する
	/// @param p0 頂点0
	/// @param p1 頂点1
	/// @param p2 頂点2
	/// @param color 塗りつぶし色
	virtual void drawTriangle(const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, const Colorf& color) = 0;

	/// @brief フェード用の半透明オーバーレイを描画する
	/// @param alpha 不透明度 [0,1]（0=透明、1=完全遮蔽）
	/// @param color ベース色（デフォルト: 黒）
	virtual void drawFadeOverlay(float alpha, const Colorf& color = Colorf::black()) = 0;

	/// @brief 背景色をクリアする
	/// @param color 背景色
	virtual void clearBackground(const Colorf& color) = 0;
};

} // namespace sgc
