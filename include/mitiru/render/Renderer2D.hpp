#pragma once

/// @file Renderer2D.hpp
/// @brief 高レベル2Dレンダラー
/// @details SpriteBatch と ShapeRenderer を統合し、
///          フレーム単位の2D描画パイプラインを提供する。

#include <algorithm>
#include <string_view>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/BitmapFont.hpp>
#include <mitiru/render/Camera2D.hpp>
#include <mitiru/render/ShapeRenderer.hpp>
#include <mitiru/render/SpriteBatch.hpp>
#include <mitiru/render/TextRenderer.hpp>

namespace mitiru::render
{

/// @brief 高レベル2Dレンダラー
/// @details SpriteBatch + ShapeRenderer を内部で管理し、
///          beginFrame/endFrame でフレーム描画を制御する。
///
/// @code
/// mitiru::render::Renderer2D renderer;
/// renderer.beginFrame();
/// renderer.drawRect({10, 10, 100, 50}, sgc::Colorf::red());
/// renderer.drawCircle({400, 300}, 30.0f, sgc::Colorf::blue());
/// renderer.endFrame();
/// @endcode
class Renderer2D
{
public:
	/// @brief デフォルトコンストラクタ
	Renderer2D() noexcept = default;

	/// @brief スクリーンサイズ指定コンストラクタ
	/// @param screenWidth スクリーン幅
	/// @param screenHeight スクリーン高さ
	explicit Renderer2D(float screenWidth, float screenHeight) noexcept
		: m_camera(screenWidth, screenHeight)
	{
	}

	/// @brief フレーム描画を開始する
	void beginFrame() noexcept
	{
		m_spriteBatch.begin();
		m_totalDrawCalls = 0;
		m_rendering = true;
	}

	/// @brief フレーム描画を終了しフラッシュする
	void endFrame() noexcept
	{
		if (!m_rendering)
		{
			return;
		}

		m_spriteBatch.end();
		m_totalDrawCalls += m_spriteBatch.drawCallCount();
		m_totalDrawCalls += m_shapeRenderer.drawCallCount();

		/// ShapeRenderer のデータもフラッシュする
		m_shapeRenderer.flush();

		m_rendering = false;
	}

	/// @brief 塗りつぶし矩形を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	void drawRect(const sgc::Rectf& rect, const sgc::Colorf& color)
	{
		m_spriteBatch.drawRect(rect, color);
	}

	/// @brief 矩形枠を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	/// @param thickness 枠線の太さ
	void drawRectFrame(const sgc::Rectf& rect,
	                   const sgc::Colorf& color,
	                   float thickness = 1.0f)
	{
		m_spriteBatch.drawRectFrame(rect, color, thickness);
	}

	/// @brief 円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 描画色
	/// @param segments 分割数
	void drawCircle(const sgc::Vec2f& center,
	                float radius,
	                const sgc::Colorf& color,
	                int segments = 32)
	{
		m_shapeRenderer.drawCircle(center, radius, color, segments);
	}

	/// @brief 線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	/// @param thickness 線の太さ
	void drawLine(const sgc::Vec2f& from,
	              const sgc::Vec2f& to,
	              const sgc::Colorf& color,
	              float thickness = 1.0f)
	{
		m_shapeRenderer.drawLine(from, to, color, thickness);
	}

	/// @brief 三角形を描画する
	/// @param p0 頂点0
	/// @param p1 頂点1
	/// @param p2 頂点2
	/// @param color 描画色
	void drawTriangle(const sgc::Vec2f& p0,
	                  const sgc::Vec2f& p1,
	                  const sgc::Vec2f& p2,
	                  const sgc::Colorf& color)
	{
		m_shapeRenderer.drawTriangle(p0, p1, p2, color);
	}

	/// @brief テキストを描画する
	/// @param position 描画位置（左上）
	/// @param text テキスト内容
	/// @param color 描画色
	/// @param fontSize フォントサイズ（8の倍数でスケーリング）
	void drawText(const sgc::Vec2f& position,
	              std::string_view text,
	              const sgc::Colorf& color,
	              float fontSize = 16.0f)
	{
		const int scale = std::max(1, static_cast<int>(fontSize) / BitmapFont::GLYPH_HEIGHT);
		TextRenderer::drawText(m_spriteBatch, text,
		                       position.x, position.y,
		                       scale, color);
		++m_totalDrawCalls;
	}

	/// @brief カメラを設定する
	/// @param camera 2Dカメラ
	void setCamera(const Camera2D& camera) noexcept
	{
		m_camera = camera;
	}

	/// @brief カメラへの参照を取得する
	[[nodiscard]] Camera2D& camera() noexcept
	{
		return m_camera;
	}

	/// @brief カメラへの定数参照を取得する
	[[nodiscard]] const Camera2D& camera() const noexcept
	{
		return m_camera;
	}

	/// @brief フレーム内の合計描画コール数を取得する
	[[nodiscard]] int totalDrawCalls() const noexcept
	{
		return m_totalDrawCalls;
	}

	/// @brief SpriteBatch への参照を取得する
	[[nodiscard]] SpriteBatch& spriteBatch() noexcept
	{
		return m_spriteBatch;
	}

	/// @brief ShapeRenderer への参照を取得する
	[[nodiscard]] ShapeRenderer& shapeRenderer() noexcept
	{
		return m_shapeRenderer;
	}

private:
	SpriteBatch m_spriteBatch;       ///< スプライトバッチ
	ShapeRenderer m_shapeRenderer;   ///< シェイプレンダラー
	Camera2D m_camera;               ///< 2Dカメラ
	int m_totalDrawCalls = 0;        ///< フレーム内描画コール合計
	bool m_rendering = false;        ///< 描画中フラグ
};

} // namespace mitiru::render
