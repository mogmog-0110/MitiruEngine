#pragma once
// mitiru::Screen 用の detail header — 直接インクルードしない。core/Screen.hpp 経由で取り込む

inline void mitiru::Screen::setPipeline(render::RenderPipeline2D* pipeline) noexcept
{
	m_pipeline = pipeline;
}

inline void mitiru::Screen::setValidator(validate::DrawCallValidator* validator) noexcept
{
	m_validator = validator;
}

inline void mitiru::Screen::clear(const sgc::Colorf& color)
{
	m_clearColor = color;
	m_spriteBatch.begin();
	if (m_softwareFb) { clearFramebuffer(); }
	++m_drawCallCount;
}

inline void mitiru::Screen::renderUI(const ui::UINode& root, const ui::UITheme& theme)
{
	if (!root.visible()) return;
	renderUINode(root, theme);
}

inline void mitiru::Screen::resetDrawCallCount() noexcept
{
	m_drawCallCount = 0;
}

inline void mitiru::Screen::resize(int width, int height) noexcept
{
	m_width = width;
	m_height = height;
}

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

	/// ソフトウェアフレームバッファへのラスタライズ
	if (m_softwareFb)
	{
		if (!m_spriteBatch.vertices().empty())
		{
			rasterizeTriangles(m_spriteBatch.vertices(), m_spriteBatch.indices());
		}
		if (!m_shapeRenderer.vertices().empty())
		{
			rasterizeTriangles(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
		}
	}

	/// ShapeRendererをフラッシュする
	m_shapeRenderer.flush();
}

inline void mitiru::Screen::pushClipRect(const sgc::Rectf& rect)
{
	if (m_pipeline)
	{
		// クリップ変更前にバッチをフラッシュする
		m_spriteBatch.end();
		if (!m_spriteBatch.vertices().empty())
		{
			m_pipeline->submitBatch(m_spriteBatch.vertices(), m_spriteBatch.indices());
		}
		if (!m_shapeRenderer.vertices().empty())
		{
			m_pipeline->submitBatch(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
			m_shapeRenderer.flush();
		}
		m_spriteBatch.begin();

		m_pipeline->pushScissorRect(rect);
	}
}

inline void mitiru::Screen::popClipRect()
{
	if (m_pipeline)
	{
		// クリップ変更前にバッチをフラッシュする
		m_spriteBatch.end();
		if (!m_spriteBatch.vertices().empty())
		{
			m_pipeline->submitBatch(m_spriteBatch.vertices(), m_spriteBatch.indices());
		}
		if (!m_shapeRenderer.vertices().empty())
		{
			m_pipeline->submitBatch(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
			m_shapeRenderer.flush();
		}
		m_spriteBatch.begin();

		m_pipeline->popScissorRect();
	}
}

inline void mitiru::Screen::setBlendMode(gfx::BlendMode mode)
{
	if (m_pipeline)
	{
		// ブレンド変更前にバッチをフラッシュする
		m_spriteBatch.end();
		if (!m_spriteBatch.vertices().empty())
		{
			m_pipeline->submitBatch(m_spriteBatch.vertices(), m_spriteBatch.indices());
		}
		if (!m_shapeRenderer.vertices().empty())
		{
			m_pipeline->submitBatch(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
			m_shapeRenderer.flush();
		}
		m_spriteBatch.begin();

		m_pipeline->setBlendMode(mode);
	}
}
