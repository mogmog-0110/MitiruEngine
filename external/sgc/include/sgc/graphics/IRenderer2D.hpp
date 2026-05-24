#pragma once

/// @file IRenderer2D.hpp
/// @brief 抽象2D描画インターフェース
///
/// フレームワーク非依存の2D描画APIを定義する。
/// DX11、Vulkan等の具体的な描画実装はこのインターフェースを実装する。
///
/// @code
/// class MyRenderer : public sgc::graphics::IRenderer2D {
/// public:
///     void drawCircle(sgc::Vec2f center, float radius, Color fill) override { /* ... */ }
///     // ...
/// };
/// @endcode

#include <string>
#include <vector>

#include "sgc/math/Vec2.hpp"
#include "sgc/math/Mat4.hpp"

namespace sgc::graphics
{

/// @brief RGBA色（浮動小数点）
///
/// 各成分は [0, 1] の範囲。
/// 静的メソッドでよく使うプリセット色を提供する。
///
/// @code
/// auto red = sgc::graphics::Color::red();
/// auto semi = sgc::graphics::Color{1.0f, 1.0f, 1.0f, 0.5f};
/// @endcode
struct Color
{
	float r = 0.0f;  ///< 赤成分 [0, 1]
	float g = 0.0f;  ///< 緑成分 [0, 1]
	float b = 0.0f;  ///< 青成分 [0, 1]
	float a = 1.0f;  ///< アルファ成分 [0, 1]

	/// @brief デフォルトコンストラクタ（黒・不透明）
	constexpr Color() noexcept = default;

	/// @brief RGBA成分を指定して構築する
	/// @param r 赤 [0, 1]
	/// @param g 緑 [0, 1]
	/// @param b 青 [0, 1]
	/// @param a アルファ [0, 1]（デフォルト: 1）
	constexpr Color(float r, float g, float b, float a = 1.0f) noexcept
		: r(r), g(g), b(b), a(a) {}

	/// @brief 等価比較
	[[nodiscard]] constexpr bool operator==(const Color& rhs) const noexcept = default;

	// ── プリセット色 ────────────────────────────────────

	/// @brief 白 (1, 1, 1, 1)
	[[nodiscard]] static constexpr Color white() noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }

	/// @brief 黒 (0, 0, 0, 1)
	[[nodiscard]] static constexpr Color black() noexcept { return {0.0f, 0.0f, 0.0f, 1.0f}; }

	/// @brief 赤 (1, 0, 0, 1)
	[[nodiscard]] static constexpr Color red() noexcept { return {1.0f, 0.0f, 0.0f, 1.0f}; }

	/// @brief 緑 (0, 1, 0, 1)
	[[nodiscard]] static constexpr Color green() noexcept { return {0.0f, 1.0f, 0.0f, 1.0f}; }

	/// @brief 青 (0, 0, 1, 1)
	[[nodiscard]] static constexpr Color blue() noexcept { return {0.0f, 0.0f, 1.0f, 1.0f}; }

	/// @brief シアン (0, 1, 1, 1)
	[[nodiscard]] static constexpr Color cyan() noexcept { return {0.0f, 1.0f, 1.0f, 1.0f}; }

	/// @brief マゼンタ (1, 0, 1, 1)
	[[nodiscard]] static constexpr Color magenta() noexcept { return {1.0f, 0.0f, 1.0f, 1.0f}; }

	/// @brief 黄 (1, 1, 0, 1)
	[[nodiscard]] static constexpr Color yellow() noexcept { return {1.0f, 1.0f, 0.0f, 1.0f}; }

	/// @brief 透明 (0, 0, 0, 0)
	[[nodiscard]] static constexpr Color transparent() noexcept { return {0.0f, 0.0f, 0.0f, 0.0f}; }
};

/// @brief 抽象2D描画インターフェース
///
/// 幾何プリミティブ（矩形、円、線、三角形、ポリゴン、ベジエ曲線）の描画と
/// テキスト描画、ビュー行列設定、フレーム制御を抽象化する。
///
/// @code
/// void render(sgc::graphics::IRenderer2D& renderer) {
///     renderer.beginFrame();
///     renderer.drawCircle({400, 300}, 50.0f, Color::red());
///     renderer.drawRect({10, 10}, {200, 100}, Color::blue());
///     renderer.endFrame();
/// }
/// @endcode
class IRenderer2D
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IRenderer2D() = default;

	// ── 描画メソッド ────────────────────────────────────

	/// @brief 塗りつぶし円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param fill 塗りつぶし色
	virtual void drawCircle(Vec2f center, float radius, Color fill) = 0;

	/// @brief 円の枠線を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param stroke 枠線色
	/// @param thickness 線の太さ
	virtual void drawCircleOutline(Vec2f center, float radius, Color stroke, float thickness) = 0;

	/// @brief 塗りつぶし矩形を描画する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param fill 塗りつぶし色
	virtual void drawRect(Vec2f pos, Vec2f size, Color fill) = 0;

	/// @brief 矩形の枠線を描画する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param stroke 枠線色
	/// @param thickness 線の太さ
	virtual void drawRectOutline(Vec2f pos, Vec2f size, Color stroke, float thickness) = 0;

	/// @brief 線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param color 線の色
	/// @param thickness 線の太さ
	virtual void drawLine(Vec2f from, Vec2f to, Color color, float thickness) = 0;

	/// @brief 塗りつぶし三角形を描画する
	/// @param a 頂点A
	/// @param b 頂点B
	/// @param c 頂点C
	/// @param fill 塗りつぶし色
	virtual void drawTriangle(Vec2f a, Vec2f b, Vec2f c, Color fill) = 0;

	/// @brief 塗りつぶしポリゴンを描画する
	/// @param points 頂点リスト
	/// @param fill 塗りつぶし色
	virtual void drawPolygon(const std::vector<Vec2f>& points, Color fill) = 0;

	/// @brief 3次ベジエ曲線を描画する
	/// @param p0 開始点
	/// @param p1 制御点1
	/// @param p2 制御点2
	/// @param p3 終了点
	/// @param color 線の色
	/// @param thickness 線の太さ
	/// @param segments 分割数（デフォルト: 32）
	virtual void drawBezier(Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3,
	                        Color color, float thickness, int segments = 32) = 0;

	/// @brief テキストを描画する
	/// @param pos 描画位置（左上）
	/// @param text テキスト文字列（UTF-8）
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param color テキスト色
	virtual void drawText(Vec2f pos, const std::string& text, float fontSize, Color color) = 0;

	// ── ビュー・フレーム制御 ──────────────────────────

	/// @brief ビュー行列を設定する
	/// @param mat 4x4ビュー行列
	virtual void setViewMatrix(const Mat4f& mat) = 0;

	/// @brief スクリーンサイズを取得する
	/// @return スクリーンの幅と高さ
	[[nodiscard]] virtual Vec2f getScreenSize() const = 0;

	/// @brief フレーム描画を開始する
	virtual void beginFrame() = 0;

	/// @brief フレーム描画を終了する
	virtual void endFrame() = 0;
};

} // namespace sgc::graphics
