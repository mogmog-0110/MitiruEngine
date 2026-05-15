#pragma once

/// @file Renderer2DBridge.hpp
/// @brief sgc::graphics::IRenderer2Dのラッパー（DX11スタブ）
///
/// IRenderer2Dインターフェースをラップし、グロー効果や変換スタックなどの
/// 付加機能を提供する高レベル2Dレンダラー。
///
/// @code
/// auto null = std::make_unique<sgc::graphics::NullRenderer2D>();
/// mitiru::render::Renderer2DBridge renderer(std::move(null));
/// renderer.beginFrame();
/// renderer.drawCircleWithGlow({400, 300}, 50.0f, Color::red(), Color::yellow(), 10.0f);
/// renderer.endFrame();
/// @endcode

#include <memory>
#include <vector>

#include <sgc/graphics/IRenderer2D.hpp>
#include <sgc/graphics/Camera2D.hpp>
#include <sgc/math/Mat4.hpp>

namespace mitiru::render
{

/// @brief 2Dレンダラー設定
struct Renderer2DBridgeConfig
{
	float screenW = 1280.0f;  ///< スクリーン幅
	float screenH = 720.0f;   ///< スクリーン高さ
	bool vsync = true;        ///< 垂直同期
};

/// @brief sgc::graphics::IRenderer2Dをラップする高レベル2Dレンダラー
///
/// IRenderer2Dへの全描画コールのデリゲーションに加え、
/// グロー効果付き描画、変換スタック、カメラ連携を提供する。
class Renderer2DBridge
{
public:
	/// @brief コンストラクタ
	/// @param inner IRenderer2Dの実装（所有権を取得）
	explicit Renderer2DBridge(std::unique_ptr<sgc::graphics::IRenderer2D> inner) noexcept
		: m_inner(std::move(inner))
	{
	}

	// ── デリゲーション ──────────────────────────────────

	/// @brief 塗りつぶし円を描画する
	void drawCircle(sgc::Vec2f center, float radius, sgc::graphics::Color fill)
	{
		m_inner->drawCircle(center, radius, fill);
	}

	/// @brief 円の枠線を描画する
	void drawCircleOutline(sgc::Vec2f center, float radius,
	                       sgc::graphics::Color stroke, float thickness)
	{
		m_inner->drawCircleOutline(center, radius, stroke, thickness);
	}

	/// @brief 塗りつぶし矩形を描画する
	void drawRect(sgc::Vec2f pos, sgc::Vec2f size, sgc::graphics::Color fill)
	{
		m_inner->drawRect(pos, size, fill);
	}

	/// @brief 矩形の枠線を描画する
	void drawRectOutline(sgc::Vec2f pos, sgc::Vec2f size,
	                     sgc::graphics::Color stroke, float thickness)
	{
		m_inner->drawRectOutline(pos, size, stroke, thickness);
	}

	/// @brief 線分を描画する
	void drawLine(sgc::Vec2f from, sgc::Vec2f to,
	              sgc::graphics::Color color, float thickness)
	{
		m_inner->drawLine(from, to, color, thickness);
	}

	/// @brief 三角形を描画する
	void drawTriangle(sgc::Vec2f a, sgc::Vec2f b, sgc::Vec2f c,
	                  sgc::graphics::Color fill)
	{
		m_inner->drawTriangle(a, b, c, fill);
	}

	/// @brief ポリゴンを描画する
	void drawPolygon(const std::vector<sgc::Vec2f>& points, sgc::graphics::Color fill)
	{
		m_inner->drawPolygon(points, fill);
	}

	/// @brief ベジエ曲線を描画する
	void drawBezier(sgc::Vec2f p0, sgc::Vec2f p1, sgc::Vec2f p2, sgc::Vec2f p3,
	                sgc::graphics::Color color, float thickness, int segments = 32)
	{
		m_inner->drawBezier(p0, p1, p2, p3, color, thickness, segments);
	}

	/// @brief テキストを描画する
	void drawText(sgc::Vec2f pos, const std::string& text,
	              float fontSize, sgc::graphics::Color color)
	{
		m_inner->drawText(pos, text, fontSize, color);
	}

	/// @brief ビュー行列を設定する
	void setViewMatrix(const sgc::Mat4f& mat)
	{
		m_inner->setViewMatrix(mat);
	}

	/// @brief スクリーンサイズを取得する
	[[nodiscard]] sgc::Vec2f getScreenSize() const
	{
		return m_inner->getScreenSize();
	}

	/// @brief フレーム描画を開始する
	void beginFrame()
	{
		m_inner->beginFrame();
	}

	/// @brief フレーム描画を終了する
	void endFrame()
	{
		m_inner->endFrame();
	}

	// ── 付加機能: グロー効果 ────────────────────────────

	/// @brief グロー付き円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param fillColor 本体の色
	/// @param glowColor グローの色
	/// @param glowRadius グローの追加半径
	///
	/// グロー円（大きめ・半透明）を先に描画し、その上に本体を描画する。
	void drawCircleWithGlow(sgc::Vec2f center, float radius,
	                        sgc::graphics::Color fillColor,
	                        sgc::graphics::Color glowColor,
	                        float glowRadius)
	{
		// グロー（半透明の大きい円）
		auto glow = glowColor;
		glow.a *= 0.4f;
		m_inner->drawCircle(center, radius + glowRadius, glow);
		// 本体
		m_inner->drawCircle(center, radius, fillColor);
	}

	/// @brief グロー付き矩形を描画する
	/// @param pos 左上座標
	/// @param size 幅と高さ
	/// @param fillColor 本体の色
	/// @param glowColor グローの色
	/// @param glowRadius グローの追加サイズ
	void drawRectWithGlow(sgc::Vec2f pos, sgc::Vec2f size,
	                      sgc::graphics::Color fillColor,
	                      sgc::graphics::Color glowColor,
	                      float glowRadius)
	{
		auto glow = glowColor;
		glow.a *= 0.4f;
		const sgc::Vec2f glowPos{pos.x - glowRadius, pos.y - glowRadius};
		const sgc::Vec2f glowSize{size.x + glowRadius * 2.0f, size.y + glowRadius * 2.0f};
		m_inner->drawRect(glowPos, glowSize, glow);
		m_inner->drawRect(pos, size, fillColor);
	}

	/// @brief グロー付き線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param color 本体の色
	/// @param glowColor グローの色
	/// @param thickness 線の太さ
	/// @param glowThickness グローの追加太さ
	void drawLineWithGlow(sgc::Vec2f from, sgc::Vec2f to,
	                      sgc::graphics::Color color,
	                      sgc::graphics::Color glowColor,
	                      float thickness, float glowThickness)
	{
		auto glow = glowColor;
		glow.a *= 0.4f;
		m_inner->drawLine(from, to, glow, thickness + glowThickness);
		m_inner->drawLine(from, to, color, thickness);
	}

	// ── 付加機能: 変換スタック ──────────────────────────

	/// @brief 変換行列をスタックにプッシュし、ビュー行列を更新する
	/// @param mat プッシュする変換行列
	void pushTransform(const sgc::Mat4f& mat)
	{
		m_transformStack.push_back(mat);
		applyTransformStack();
	}

	/// @brief 変換スタックから最後の行列をポップし、ビュー行列を更新する
	void popTransform()
	{
		if (!m_transformStack.empty())
		{
			m_transformStack.pop_back();
			applyTransformStack();
		}
	}

	/// @brief 変換スタックの深さを取得する
	[[nodiscard]] int transformStackDepth() const noexcept
	{
		return static_cast<int>(m_transformStack.size());
	}

	// ── 付加機能: カメラ連携 ────────────────────────────

	/// @brief sgc::graphics::Camera2Dからビュー行列を設定する
	/// @param camera 2Dカメラ
	void setCamera(const sgc::graphics::Camera2D& camera)
	{
		m_inner->setViewMatrix(camera.getViewMatrix());
	}

	/// @brief 内部IRenderer2Dへのアクセス
	/// @return IRenderer2Dへの参照
	[[nodiscard]] sgc::graphics::IRenderer2D& inner() noexcept
	{
		return *m_inner;
	}

	/// @brief 内部IRenderer2Dへのconstアクセス
	/// @return IRenderer2Dへのconst参照
	[[nodiscard]] const sgc::graphics::IRenderer2D& inner() const noexcept
	{
		return *m_inner;
	}

private:
	/// @brief 変換スタックを合成してビュー行列に反映する
	void applyTransformStack()
	{
		auto result = sgc::Mat4f::identity();
		for (const auto& mat : m_transformStack)
		{
			result = result * mat;
		}
		m_inner->setViewMatrix(result);
	}

	std::unique_ptr<sgc::graphics::IRenderer2D> m_inner;  ///< 内部レンダラー
	std::vector<sgc::Mat4f> m_transformStack;              ///< 変換スタック
};

} // namespace mitiru::render
