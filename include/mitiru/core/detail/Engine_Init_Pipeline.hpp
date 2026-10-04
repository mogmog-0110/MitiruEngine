// mitiru::Engine の detail header。直接 include しない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/render/BackendInit.hpp>

// ── Render pipeline / viewport / resize の class 外定義 ─────────

namespace mitiru::detail
{
	struct KeepViewportRect
	{
		float width;
		float height;
		float offsetX;
		float offsetY;
	};

	// ResizeMode::Keep 用: aspect 比を保った内接矩形 (letterbox/pillarbox) を計算する。
	// Engine::onWindowResize から切り出し済み（window/device なしで単体テストできるようにするため）。
	[[nodiscard]] inline KeepViewportRect computeKeepViewportRect(
		int windowW, int windowH, int logicalW, int logicalH) noexcept
	{
		KeepViewportRect rect{
			static_cast<float>(windowW), static_cast<float>(windowH), 0.0f, 0.0f};
		if (windowW <= 0 || windowH <= 0 || logicalW <= 0 || logicalH <= 0)
		{
			return rect;
		}
		const float logicalAspect = static_cast<float>(logicalW) / static_cast<float>(logicalH);
		const float windowAspect  = static_cast<float>(windowW) / static_cast<float>(windowH);
		if (windowAspect > logicalAspect)
		{
			// window の方が横長 => 高さ基準で内接、左右に pillarbox
			rect.height  = static_cast<float>(windowH);
			rect.width   = rect.height * logicalAspect;
			rect.offsetX = 0.5f * (static_cast<float>(windowW) - rect.width);
		}
		else
		{
			// window の方が縦長 (または同じ) => 幅基準で内接、上下に letterbox
			rect.width   = static_cast<float>(windowW);
			rect.height  = rect.width / logicalAspect;
			rect.offsetY = 0.5f * (static_cast<float>(windowH) - rect.height);
		}
		return rect;
	}
}

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
	// この部分は modal drag 中でも走る。DXGI swap chain は window に合わせて
	// 必ず resize しないと Present が stretch / glitch artifact を起こす。
	// 重い処理 (logical layout、pipeline projection) は
	// 後段の WM_EXITSIZEMOVE まで遅延させる。
	if (m_device && m_device->backend() != gfx::Backend::Null)
	{
		m_device->onResize(w, h);
	}

	// 3D レンダラの内部 RT (depth/MSAA/HDR/FXAA/OIT) は backbuffer と同寸が前提。
	// device->onResize (GPU 待機 + swapchain resize) の後、物理 px で追従させる。
	if (m_renderer3D)
	{
		m_renderer3D->resize(w, h);
	}

#ifdef _WIN32
	// マウス drag 中は重い re-layout を遅延させる。WM_SIZE ごとに logical 座標系が
	// 作り直されて native sprite が window に対し揺れる「ガタガタ」jitter を防ぐ。
	if (auto* win32 = dynamic_cast<mitiru::Win32Window*>(m_window.get());
	    win32 && win32->inModalLoop())
	{
		m_pendingResizeW = w;
		m_pendingResizeH = h;
		return; // defer logical resize to WM_EXITSIZEMOVE
	}
#endif

	// config.resizeMode に従って新しい logical size を解決する (Siv3D 相当)。
	//
	// Actual。logical = physical (1:1)。HTML @media が発火し、native draw は
	//           物理座標を使う。既定。
	// Virtual。logical は初期値で固定、viewport = window 全体 =>
	//           anisotropic stretch。論理座標で書かれた legacy game 向け。
	// Keep。logical は初期値で固定、viewport は aspect 比を保った
	//           最大内接矩形 (letterbox/pillarbox)。stretch させない。
	mitiru::Size newLogical{w, h};
	// Keep 用: aspect 比を保った viewport サイズ + 中央寄せオフセット (letterbox/pillarbox)。
	// RenderPipeline2D::setViewportSize が offsetX/offsetY を受け取れるようになった
	// (B12) ので、余白を左右/上下均等に振り分けて中央寄せにする。
	float viewportW = static_cast<float>(w);
	float viewportH = static_cast<float>(h);
	float viewportOffsetX = 0.0f;
	float viewportOffsetY = 0.0f;
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
		newLogical.width  = m_logicalWidth  > 0 ? m_logicalWidth  : w;
		newLogical.height = m_logicalHeight > 0 ? m_logicalHeight : h;
		break;
	case EngineConfig::ResizeMode::Keep:
		newLogical.width  = m_logicalWidth  > 0 ? m_logicalWidth  : w;
		newLogical.height = m_logicalHeight > 0 ? m_logicalHeight : h;
		{
			const auto keep = mitiru::detail::computeKeepViewportRect(
				w, h, newLogical.width, newLogical.height);
			viewportW       = keep.width;
			viewportH       = keep.height;
			viewportOffsetX = keep.offsetX;
			viewportOffsetY = keep.offsetY;
		}
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
		m_renderPipeline->setViewportSize(viewportW, viewportH, viewportOffsetX, viewportOffsetY);
	}
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
		m_device.get(), screenWidth, screenHeight, winW, winH,
		m_config.antiAliasing3D, m_config.motionBlur3D);
	setQualityCaps(m_config.qualityCaps);
#ifdef _WIN32
	if (auto* dx12 = dynamic_cast<render::Renderer3D_DX12*>(m_renderer3D.get()))
	{
		dx12->setPassGpuTimingEnabled(m_config.gpuPassTiming);
		dx12->setAsyncLoads(m_config.asyncLoads);
		dx12->setStreamingBudgetBytes(m_config.streamingBudgetBytes);
		render::ScreenSpaceReflectionSettings ssr;
		ssr.enabled = m_config.screenSpaceReflections3D;
		dx12->setScreenSpaceReflections(ssr);
		render::GlobalIlluminationSettings gi;
		gi.dynamic = m_config.dynamicGi3D;
		dx12->setGlobalIllumination(gi);
		if (!m_config.lightingBake3D.empty()) { (void)dx12->loadLightingBake(m_config.lightingBake3D.c_str()); }
	}
#endif
}
