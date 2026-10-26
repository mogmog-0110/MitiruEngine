#pragma once
// mitiru::Screen 用の detail header。直接インクルードしない。core/Screen.hpp 経由で取り込む

#include <mitiru/module/detail/HostBoundary62.hpp>
#include <mitiru/render/IRenderer3D.hpp>

inline void mitiru::Screen::setPipeline(render::RenderPipeline2D* pipeline) noexcept
{
	m_pipeline = pipeline;
	// GPU が 4 隅を作れる描画先 (DX12) では、軸に平行な四角形を 1 枚 48 バイトのレコードで積む (ABI v61)。
	// 積みかけのレコードは、読めない描画先へ替わる前に頂点へ展開しておく
	const bool quad = pipeline != nullptr && pipeline->supportsQuadBatch();
	if (!quad) { m_spriteBatch.materialize(); }
	m_spriteBatch.setQuadMode(quad);
}

inline void mitiru::Screen::setValidator(validate::DrawCallValidator* validator) noexcept
{
	m_validator = validator;
}

inline void mitiru::Screen::clear(const sgc::Colorf& color)
{
	m_clearColor = color;
	m_spriteBatch.begin();
	m_curTexHandle = 0; // フレーム頭で run 状態をリセット
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
	// 前のフレームが 3D の上に重ねる蓄積で切っていても、フレームの頭で選び直す (バッチは前のフレームの present で空になっている)
	if (m_spriteBatch.empty())
	{
		m_spriteBatch.setQuadMode(m_pipeline != nullptr && m_pipeline->supportsQuadBatch());
	}
	m_drawCallCount = 0;
	m_drawLog.clear(); // AI 観測用 draw log もフレーム毎にリセット (capacity は維持)
	m_3dStarted = false; // 3D facade: 次フレームの最初の drawMesh で beginFrame し直す
	m_overlayRunUsed = 0; // 3D フレームの 2D 蓄積もフレーム毎にリセット
	m_lightCount2D = 0;   // v62: 2D ライトは毎フレーム積み直す
	m_ambient2DSet = 0;
	if (m_pipeline != nullptr) { m_pipeline->clearLighting2D(); }
	// v62: 入口だけの機能が前のフレームに使われていれば 1 度だけ知らせる (DLL でなく host が呼ぶ関数なので、知らせる表は host に 1 つ)
	if (const std::uint32_t pending = takePendingV62(); pending != 0) { module::detail::reportPendingV62(pending); }
}

inline void mitiru::Screen::resize(int width, int height) noexcept
{
	m_width = width;
	m_height = height;
}

// ── textured sprite batch ヘルパー──────────────────────
// painter 順を保つため、頂点カラー run と textured run の切替で現バッチを
// flush+submit する。RenderPipeline2D の完全型が要るためここ（detail）で定義。

inline bool mitiru::Screen::deferring3DOverlay() const noexcept
{
	return m_3dStarted && m_renderer3D != nullptr
		&& m_renderer3D->hasOverlaySupport();
}

inline mitiru::Screen::OverlayRun& mitiru::Screen::appendOverlayRun(OverlayRun::Kind kind)
{
	if (m_overlayRunUsed == m_overlayRuns.size()) { m_overlayRuns.emplace_back(); }
	OverlayRun& run = m_overlayRuns[m_overlayRunUsed++];
	run.kind = kind;
	run.texHandle = 0;
	run.verts.clear();
	run.indices.clear();
	return run;
}

inline void mitiru::Screen::stopQuadBatching()
{
	// 3D を使うフレームの 2D は、3D の後に重ねる蓄積 (OverlayRun) へ頂点と添字で渡す。レコードのまま積み続けると、
	// 渡すたびに展開の確保が入る。ここまでに積んだ分だけ展開して、以後は頂点で積む
	if (!m_spriteBatch.quadMode()) { return; }
	m_spriteBatch.materialize();
	m_spriteBatch.setQuadMode(false);
}

inline void mitiru::Screen::flushCurrentBatch()
{
	m_spriteBatch.end();
	if (!m_spriteBatch.empty())
	{
		// レコードを読めない先 (3D の上に重ねる蓄積、CPU 描画) へ渡す前に、従来の頂点 + 添字へ展開する。
		// 3D を使うフレームの残りは、展開しなくて済むよう頂点で積む
		const bool overlay = deferring3DOverlay();
		const bool softwareRaster = m_softwareFb && m_swFbActive;
		if (m_spriteBatch.hasQuads()
		    && (overlay || softwareRaster || m_pipeline == nullptr || !m_pipeline->isValid() || !m_pipeline->supportsQuadBatch()))
		{
			m_spriteBatch.materialize();
		}
		if (overlay) { m_spriteBatch.setQuadMode(false); }
		if (m_spriteBatch.hasQuads())
		{
			m_pipeline->submitQuadBatch(m_spriteBatch, m_curTexHandle);
		}
		else if (overlay)
		{
			OverlayRun& run = appendOverlayRun(OverlayRun::Kind::Batch);
			run.texHandle = m_curTexHandle;
			run.verts = m_spriteBatch.vertices();
			run.indices = m_spriteBatch.indices();
		}
		else if (m_pipeline && m_pipeline->isValid())
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
	// v62: 最後のフレームで使った入口だけの機能も、次のフレーム頭を待たずに知らせる
	if (const std::uint32_t pending = takePendingV62(); pending != 0) { module::detail::reportPendingV62(pending); }
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

	/// ShapeRenderer をフラッシュする
	m_shapeRenderer.flush();
}

inline void mitiru::Screen::present3DOverlay()
{
	// 3D の後にまとめてサブミットする 2D には光を当てない (途中で光を替えられないため)
	if (m_pipeline != nullptr) { m_pipeline->clearLighting2D(); }
	flushCurrentBatch();
	if (!m_shapeRenderer.vertices().empty())
	{
		OverlayRun& run = appendOverlayRun(OverlayRun::Kind::Batch);
		run.verts = m_shapeRenderer.vertices();
		run.indices = m_shapeRenderer.indices();
		if (m_softwareFb && m_swFbActive)
		{
			rasterizeTriangles(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
		}
		m_shapeRenderer.flush();
	}
	if (m_pipeline && m_pipeline->isValid())
	{
		for (std::size_t i = 0; i < m_overlayRunUsed; ++i)
		{
			const OverlayRun& run = m_overlayRuns[i];
			switch (run.kind)
			{
			case OverlayRun::Kind::Batch:
				if (run.texHandle != 0)
				{
					m_pipeline->submitTexturedBatch(run.verts, run.indices, run.texHandle);
				}
				else
				{
					m_pipeline->submitBatch(run.verts, run.indices);
				}
				break;
			case OverlayRun::Kind::PushClip: m_pipeline->pushScissorRect(run.clip); break;
			case OverlayRun::Kind::PopClip:  m_pipeline->popScissorRect(); break;
			case OverlayRun::Kind::Blend:    m_pipeline->setBlendMode(run.blend); break;
			}
		}
	}
	m_overlayRunUsed = 0;
}

/// 開いているバッチと図形を閉じて送る (3D フレーム中は蓄積へ)
inline void mitiru::Screen::closeBatchesForStateChange()
{
	flushCurrentBatch();
	if (!m_shapeRenderer.vertices().empty())
	{
		if (deferring3DOverlay())
		{
			OverlayRun& run = appendOverlayRun(OverlayRun::Kind::Batch);
			run.verts = m_shapeRenderer.vertices();
			run.indices = m_shapeRenderer.indices();
		}
		else
		{
			m_pipeline->submitBatch(m_shapeRenderer.vertices(), m_shapeRenderer.indices());
		}
		m_shapeRenderer.flush();
	}
}

inline void mitiru::Screen::pushClipRect(const sgc::Rectf& rect)
{
	if (m_pipeline)
	{
		closeBatchesForStateChange();
		if (deferring3DOverlay())
		{
			appendOverlayRun(OverlayRun::Kind::PushClip).clip = rect;
		}
		else
		{
			m_pipeline->pushScissorRect(rect);
		}
	}
}

inline void mitiru::Screen::popClipRect()
{
	if (m_pipeline)
	{
		closeBatchesForStateChange();
		if (deferring3DOverlay())
		{
			appendOverlayRun(OverlayRun::Kind::PopClip);
		}
		else
		{
			m_pipeline->popScissorRect();
		}
	}
}

inline void mitiru::Screen::setBlendMode(gfx::BlendMode mode)
{
	if (m_pipeline)
	{
		closeBatchesForStateChange();
		if (deferring3DOverlay())
		{
			appendOverlayRun(OverlayRun::Kind::Blend).blend = mode;
		}
		else
		{
			m_pipeline->setBlendMode(mode);
		}
	}
}
