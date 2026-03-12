#pragma once

/// @file Screen.hpp
/// @brief 描画サーフェス
/// @details レンダラーへの描画コマンドを抽象化するサーフェスクラス。
///          RenderPipeline2Dが接続されている場合はGPU描画に委譲し、
///          未接続の場合はカウンターのみ増加する（ヘッドレス対応）。

#include <algorithm>
#include <string>
#include <string_view>

#include <sgc/types/Color.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/math/Rect.hpp>

#include <mitiru/render/SpriteBatch.hpp>
#include <mitiru/render/ShapeRenderer.hpp>
#include <mitiru/render/TextRenderer.hpp>

namespace mitiru::render
{
class RenderPipeline2D;
} // namespace mitiru::render

namespace mitiru
{

/// @brief 描画サーフェス
/// @details ゲームの draw() に渡される描画インターフェース。
///          内部でSpriteBatch/ShapeRendererに委譲し、
///          RenderPipeline2D経由でGPUに送信する。
class Screen
{
public:
	/// @brief コンストラクタ
	/// @param width サーフェス幅（ピクセル）
	/// @param height サーフェス高さ（ピクセル）
	explicit Screen(int width, int height) noexcept
		: m_width(width)
		, m_height(height)
	{
	}

	/// @brief RenderPipeline2Dを接続する
	/// @param pipeline パイプライン（nullptrで解除）
	void setPipeline(render::RenderPipeline2D* pipeline) noexcept
	{
		m_pipeline = pipeline;
	}

	/// @brief RenderPipeline2Dを取得する
	/// @return パイプラインへのポインタ（未接続時はnullptr）
	[[nodiscard]] render::RenderPipeline2D* pipeline() const noexcept
	{
		return m_pipeline;
	}

	/// @brief サーフェスをクリアする
	/// @param color クリア色
	void clear(const sgc::Colorf& color = sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f})
	{
		m_clearColor = color;
		m_spriteBatch.begin();
		++m_drawCallCount;
	}

	/// @brief 矩形を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	void drawRect(const sgc::Rectf& rect, const sgc::Colorf& color)
	{
		m_spriteBatch.drawRect(rect, color);
		++m_drawCallCount;
	}

	/// @brief 円を描画する
	/// @param center 中心座標
	/// @param radius 半径
	/// @param color 描画色
	void drawCircle(const sgc::Vec2f& center, float radius, const sgc::Colorf& color)
	{
		m_shapeRenderer.drawCircle(center, radius, color);
		++m_drawCallCount;
	}

	/// @brief 線分を描画する
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	/// @param thickness 線の太さ
	void drawLine(const sgc::Vec2f& from, const sgc::Vec2f& to,
	              const sgc::Colorf& color, float thickness = 1.0f)
	{
		m_shapeRenderer.drawLine(from, to, color, thickness);
		++m_drawCallCount;
	}

	/// @brief テキストを描画する
	/// @param position 描画位置（左上）
	/// @param text テキスト内容
	/// @param color 描画色
	/// @param fontSize フォントサイズ（8の倍数でスケーリング）
	void drawText(const sgc::Vec2f& position, std::string_view text,
	              const sgc::Colorf& color, float fontSize = 16.0f)
	{
		const int scale = std::max(1, static_cast<int>(fontSize) / render::BitmapFont::GLYPH_HEIGHT);
		render::TextRenderer::drawText(*this, text,
		                               position.x, position.y,
		                               scale, color);
	}

	/// @brief フレーム描画を完了し、GPU送信する
	/// @details SpriteBatch/ShapeRendererの蓄積データをRenderPipeline2Dに送る。
	void present();

	/// @brief サーフェス幅を取得する
	[[nodiscard]] int width() const noexcept
	{
		return m_width;
	}

	/// @brief サーフェス高さを取得する
	[[nodiscard]] int height() const noexcept
	{
		return m_height;
	}

	/// @brief フレーム内の描画コール数を取得する
	[[nodiscard]] int drawCallCount() const noexcept
	{
		return m_drawCallCount;
	}

	/// @brief 描画コール数をリセットする
	void resetDrawCallCount() noexcept
	{
		m_drawCallCount = 0;
	}

	/// @brief 最後に設定されたクリア色を取得する
	[[nodiscard]] const sgc::Colorf& clearColor() const noexcept
	{
		return m_clearColor;
	}

	/// @brief サーフェスサイズを変更する
	/// @param width 新しい幅
	/// @param height 新しい高さ
	void resize(int width, int height) noexcept
	{
		m_width = width;
		m_height = height;
	}

	/// @brief SpriteBatchへの参照を取得する
	[[nodiscard]] render::SpriteBatch& spriteBatch() noexcept
	{
		return m_spriteBatch;
	}

	/// @brief ShapeRendererへの参照を取得する
	[[nodiscard]] render::ShapeRenderer& shapeRenderer() noexcept
	{
		return m_shapeRenderer;
	}

private:
	int m_width;                  ///< サーフェス幅
	int m_height;                 ///< サーフェス高さ
	int m_drawCallCount = 0;      ///< 描画コール数
	sgc::Colorf m_clearColor{0.0f, 0.0f, 0.0f, 1.0f};  ///< クリア色
	render::SpriteBatch m_spriteBatch;       ///< スプライトバッチ
	render::ShapeRenderer m_shapeRenderer;   ///< シェイプレンダラー
	render::RenderPipeline2D* m_pipeline = nullptr;  ///< レンダリングパイプライン（非所有）
};

} // namespace mitiru

/// @brief present()のインライン実装
/// @details RenderPipeline2D.hppをインクルードせずに宣言だけで済むよう、
///          ヘッダー下部で別途インクルードして実装する。
#include <mitiru/render/RenderPipeline2D.hpp>

inline void mitiru::Screen::present()
{
	/// SpriteBatchの蓄積を終了する
	m_spriteBatch.end();

	if (m_pipeline && m_pipeline->isValid())
	{
		/// SpriteBatchのデータをGPU送信する
		if (!m_spriteBatch.vertices().empty())
		{
			m_pipeline->submitBatch(
				m_spriteBatch.vertices(),
				m_spriteBatch.indices());
		}

		/// ShapeRendererのデータをGPU送信する
		if (!m_shapeRenderer.vertices().empty())
		{
			m_pipeline->submitBatch(
				m_shapeRenderer.vertices(),
				m_shapeRenderer.indices());
		}
	}

	/// ShapeRendererをフラッシュする
	m_shapeRenderer.flush();
}
