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
	m_curTexHandle = 0; // フレーム頭で run 状態をリセット（ADR 0009）
	// 観測しないフレームは clear も skip し、直前のラスタライズ結果を保持する (#53)
	if (m_softwareFb && m_swFbActive) { clearFramebuffer(); }
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
	m_drawLog.clear(); // AI 観測用 draw log もフレーム毎にリセット (capacity は維持)
	m_3dStarted = false; // 3D facade: 次フレームの最初の drawMesh で beginFrame し直す
}

inline void mitiru::Screen::resize(int width, int height) noexcept
{
	m_width = width;
	m_height = height;
}

// ── textured sprite batch ヘルパー（ADR 0009）──────────────────────
// painter 順を保つため、頂点カラー run と textured run の切替で現バッチを
// flush+submit する。RenderPipeline2D の完全型が要るためここ（detail）で定義。

inline void mitiru::Screen::flushCurrentBatch()
{
	m_spriteBatch.end();
	if (!m_spriteBatch.vertices().empty())
	{
		if (m_pipeline && m_pipeline->isValid())
		{
			if (m_curTexHandle != 0)
			{
				m_pipeline->submitTexturedBatch(
					m_spriteBatch.vertices(), m_spriteBatch.indices(),
					m_curTexHandle);
			}
			else
			{
				m_pipeline->submitBatch(
					m_spriteBatch.vertices(), m_spriteBatch.indices());
			}
		}
		if (m_softwareFb && m_swFbActive)
		{
			rasterizeTriangles(m_spriteBatch.vertices(), m_spriteBatch.indices());
		}
	}
	m_spriteBatch.begin();
	m_curTexHandle = 0;
}

inline void mitiru::Screen::switchToTexture(std::uint32_t texHandle)
{
	if (m_curTexHandle == texHandle) return;
	flushCurrentBatch();        // 現 run を submit して空に
	m_curTexHandle = texHandle; // 新しい run のテクスチャ
}

inline void mitiru::Screen::drawSpriteTexturedQuad(
	std::uint32_t texHandle, const sgc::Vec2f corners[4],
	const sgc::Vec2f uvs[4], const sgc::Colorf& color)
{
	switchToTexture(texHandle);
	m_spriteBatch.drawSpriteQuad(corners, uvs, color);
	++m_drawCallCount;
}

inline void mitiru::Screen::flushSpriteBatch()
{
	flushCurrentBatch();
}

inline void mitiru::Screen::present()
{
	/// 現在のバッチ（頂点カラー or textured run）を submit し、空で開き直す
	flushCurrentBatch();

	/// ShapeRenderer（回転クワッド/三角/線）は従来通り最後に送る
	if (m_pipeline && m_pipeline->isValid())
	{
		if (!m_shapeRenderer.vertices().empty())
		{
			m_pipeline->submitBatch(
				m_shapeRenderer.vertices(),
				m_shapeRenderer.indices());
		}
	}

	/// ソフトウェアフレームバッファへのラスタライズ（spriteBatch は flush 済み）
	if (m_softwareFb && m_swFbActive && !m_shapeRenderer.vertices().empty())
	{
		rasterizeTriangles(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
	}

	/// ShapeRendererをフラッシュする
	m_shapeRenderer.flush();
}

inline void mitiru::Screen::pushClipRect(const sgc::Rectf& rect)
{
	if (m_pipeline)
	{
		// クリップ変更前にバッチをフラッシュする（textured run も正しく閉じる）
		flushCurrentBatch();
		if (!m_shapeRenderer.vertices().empty())
		{
			m_pipeline->submitBatch(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
			m_shapeRenderer.flush();
		}

		m_pipeline->pushScissorRect(rect);
	}
}

inline void mitiru::Screen::popClipRect()
{
	if (m_pipeline)
	{
		// クリップ変更前にバッチをフラッシュする（textured run も正しく閉じる）
		flushCurrentBatch();
		if (!m_shapeRenderer.vertices().empty())
		{
			m_pipeline->submitBatch(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
			m_shapeRenderer.flush();
		}

		m_pipeline->popScissorRect();
	}
}

inline void mitiru::Screen::setBlendMode(gfx::BlendMode mode)
{
	if (m_pipeline)
	{
		// ブレンド変更前にバッチをフラッシュする（textured run も正しく閉じる）
		flushCurrentBatch();
		if (!m_shapeRenderer.vertices().empty())
		{
			m_pipeline->submitBatch(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
			m_shapeRenderer.flush();
		}

		m_pipeline->setBlendMode(mode);
	}
}
