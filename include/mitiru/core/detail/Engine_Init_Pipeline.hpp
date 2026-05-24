// Detail header for mitiru::Engine — do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/render/BackendInit.hpp>

// ── Render pipeline / viewport / resize out-of-class definitions ─────────

MITIRU_INLINE void mitiru::Engine::createRenderPipeline(int screenWidth, int screenHeight)
{
	/// Backend-specific dispatch lives in render::createPipeline2DFor;
	/// Engine no longer reaches into concrete IDevice subclasses.
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

	// Backbuffer follows window client size (1:1 with physical pixels).
	// This part runs even during modal drag — DXGI swap chain MUST resize
	// to match window or Present hits stretching/glitch artifacts. The
	// expensive parts (logical layout, CEF re-layout, pipeline projection)
	// are deferred to WM_EXITSIZEMOVE below.
	if (m_device && m_device->backend() != gfx::Backend::Null)
	{
		m_device->onResize(w, h);
	}

#ifdef _WIN32
	// During mouse-driven drag, defer the costly re-layout. This prevents
	// the "gatagata" jitter where every WM_SIZE re-runs CSS @media (causing
	// HTML layout snap) and shifts the logical coord system (causing native
	// sprites to wobble relative to the window). Composite uses texture dim
	// viewport (letterbox), so the user sees stable content with engine
	// clear color padding as the window grows.
	if (auto* win32 = dynamic_cast<mitiru::Win32Window*>(m_window.get());
	    win32 && win32->inModalLoop())
	{
		m_pendingResizeW = w;
		m_pendingResizeH = h;
		return; // defer logical/CEF resize to WM_EXITSIZEMOVE
	}
#endif

	// Resolve the new logical size per config.resizeMode (Siv3D parity):
	//
	// Actual  — logical = physical (1:1). HTML @media fires, native draw
	//           uses physical coords. Default.
	// Virtual — logical stays at initial value, viewport = full window =>
	//           anisotropic stretch. Legacy logical-coord games.
	// Keep    — not yet supported (needs viewport offset for letterbox);
	//           falls back to Virtual semantics for now.
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

	// CEF UI layer: tell the browser to repaint at the new size and mark
	// the GPU texture for deferred resize. The texture is recreated
	// atomically on the next OnPaint that arrives with matching dims —
	// during the in-flight window the old texture continues to render
	// (bilinear-stretched briefly), so the UI never goes blank.
	// (See MitiruCefTexture::applyPendingResize for the swap logic.)
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
	/// Backend-specific dispatch lives in render::createRenderer3DFor;
	/// Engine no longer reaches into concrete IDevice subclasses.
	const int winW = m_window ? m_window->width()  : screenWidth;
	const int winH = m_window ? m_window->height() : screenHeight;
	m_renderer3D = render::createRenderer3DFor(
		m_device.get(), screenWidth, screenHeight, winW, winH);
}
