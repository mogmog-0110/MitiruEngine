// mitiru::Engine の detail header — 直接 include しない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/render/BackendInit.hpp>

// ── Render pipeline / viewport / resize の class 外定義 ─────────

MITIRU_INLINE void mitiru::Engine::createRenderPipeline(int screenWidth, int screenHeight)
{
	/// backend 固有の dispatch は render::createPipeline2DFor が担う。
	/// Engine は具象 IDevice subclass に直接触れない。
	auto result = render::createPipeline2DFor(
		m_device.get(), screenWidth, screenHeight);

	if (!result.pipeline)
	{
		/// NullDevice 等の場合はパイプラインなし（ヘッドレス動作）
		return;
	}

	m_renderPipeline = std::make_unique<render::RenderPipeline2D>(
		std::move(*result.pipeline));
	if (m_screen)
	{
		m_screen->setPipeline(m_renderPipeline.get());
	}

	if (result.postProcess)
	{
		m_postProcess = std::move(result.postProcess);
		/// デバイスにポストプロセスマネージャーを接続する
		if (m_device)
		{
			m_device->setPostProcessManager(m_postProcess.get());
		}
	}
}

MITIRU_INLINE void mitiru::Engine::onWindowResize(int w, int h)
{
	if (w <= 0 || h <= 0)
	{
		return;
	}

	// backbuffer は window client size に追従する (物理 pixel と 1:1)。
	// この部分は modal drag 中でも走る — DXGI swap chain は window に合わせて
	// 必ず resize しないと Present が stretch / glitch artifact を起こす。
	// 重い処理 (logical layout、CEF re-layout、pipeline projection) は
	// 後段の WM_EXITSIZEMOVE まで遅延させる。
	if (m_device && m_device->backend() != gfx::Backend::Null)
	{
		m_device->onResize(w, h);
	}

#ifdef _WIN32
	// マウス drag 中は重い re-layout を遅延させる。WM_SIZE ごとに CSS @media が
	// 再走して HTML layout が snap し、logical 座標系がずれて native sprite が
	// window に対し揺れる「ガタガタ」jitter を防ぐ。composite は texture 寸法の
	// viewport (letterbox) を使うため、window が広がる間も engine clear color の
	// padding 付きで安定した内容が見える。
	if (auto* win32 = dynamic_cast<mitiru::Win32Window*>(m_window.get());
	    win32 && win32->inModalLoop())
	{
		m_pendingResizeW = w;
		m_pendingResizeH = h;
		return; // defer logical/CEF resize to WM_EXITSIZEMOVE
	}
#endif

	// config.resizeMode に従って新しい logical size を解決する (Siv3D 相当):
	//
	// Actual  — logical = physical (1:1)。HTML @media が発火し、native draw は
	//           物理座標を使う。既定。
	// Virtual — logical は初期値で固定、viewport = window 全体 =>
	//           anisotropic stretch。論理座標で書かれた legacy game 向け。
	// Keep    — 未対応 (letterbox 用の viewport offset が要る)。
	//           当面は Virtual の semantics に fallback する。
	mitiru::Size newLogical{w, h};
	switch (m_config.resizeMode)
	{
	case EngineConfig::ResizeMode::Actual:
		if (m_loopGame)
		{
			newLogical = m_loopGame->layout(w, h);
			if (newLogical.width  <= 0) { newLogical.width  = w; }
			if (newLogical.height <= 0) { newLogical.height = h; }
		}
		break;
	case EngineConfig::ResizeMode::Virtual:
	case EngineConfig::ResizeMode::Keep:    // TODO: real letterbox
		newLogical.width  = m_logicalWidth  > 0 ? m_logicalWidth  : w;
		newLogical.height = m_logicalHeight > 0 ? m_logicalHeight : h;
		break;
	}

	if (m_screen)
	{
		m_screen->resize(newLogical.width, newLogical.height);
	}
	m_logicalWidth  = newLogical.width;
	m_logicalHeight = newLogical.height;

	if (m_renderPipeline)
	{
		m_renderPipeline->resize(
			static_cast<float>(newLogical.width),
			static_cast<float>(newLogical.height));
		m_renderPipeline->setViewportSize(
			static_cast<float>(w), static_cast<float>(h));
	}

	// CEF UI layer: browser に新サイズでの repaint を指示し、GPU texture を
	// deferred resize 対象としてマークする。texture は寸法が一致する次の
	// OnPaint で atomically に再生成される — その間は古い texture が描画され
	// 続ける (一時的に bilinear-stretch) ため、UI が空白になることはない。
	// (swap ロジックは MitiruCefTexture::applyPendingResize を参照。)
#if defined(_WIN32) && defined(MITIRU_HAS_CEF)
	if (m_cefContext.isInitialized() && m_device)
	{
		if (auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
		{
			m_cefContext.resize(*dx12, w, h);
		}
	}
#endif
}

MITIRU_INLINE void mitiru::Engine::syncViewport()
{
	if (m_renderPipeline && m_window)
	{
		m_renderPipeline->setViewportSize(
			static_cast<float>(m_window->width()),
			static_cast<float>(m_window->height()));
	}
}

MITIRU_INLINE void mitiru::Engine::create3DRenderer(int screenWidth, int screenHeight)
{
	/// backend 固有の dispatch は render::createRenderer3DFor が担う。
	/// Engine は具象 IDevice subclass に直接触れない。
	const int winW = m_window ? m_window->width()  : screenWidth;
	const int winH = m_window ? m_window->height() : screenHeight;
	m_renderer3D = render::createRenderer3DFor(
		m_device.get(), screenWidth, screenHeight, winW, winH);
}
