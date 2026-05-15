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

	// Screen論理サイズは維持する（layout()が決定した固定値のまま投影行列に使う）
	// バックバッファはウィンドウのクライアントサイズに追従させる。
	// → 物理解像度 = バックバッファサイズ → ストレッチなしシャープ描画。
	if (m_device && m_device->backend() != gfx::Backend::Null)
	{
		m_device->onResize(w, h);
	}
	if (m_renderPipeline)
	{
		m_renderPipeline->setViewportSize(
			static_cast<float>(w), static_cast<float>(h));
	}
	// CEF は論理解像度固定のため onWindowResize でリサイズしない。
	// composite() 側でウィンドウサイズにスケール描画する。
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
