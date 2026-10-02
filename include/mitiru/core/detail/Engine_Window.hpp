// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── window mode helpers のクラス外定義 ─────────────────────────

MITIRU_INLINE void mitiru::Engine::setFullscreen(bool enable) noexcept
{
#ifdef _WIN32
	auto* w32 = dynamic_cast<Win32Window*>(m_window.get());
	if (w32) w32->setFullscreen(enable);
#else
	(void)enable;
#endif
}

MITIRU_INLINE void mitiru::Engine::setVSync(bool enabled) noexcept
{
	m_config.vsync = enabled;
#ifdef _WIN32
	if (auto* dx12 = dynamic_cast<gfx::Dx12Device*>(m_device.get()))
	{
		if (auto* swap = dynamic_cast<gfx::Dx12SwapChain*>(dx12->getSwapChain())) { swap->setVSync(enabled); }
	}
#endif
}

MITIRU_INLINE void mitiru::Engine::resizeWindowClient(int w, int h) noexcept
{
	if (!m_window || w <= 0 || h <= 0 || isFullscreen()) { return; }
	int x = 0, y = 0, outerW = 0, outerH = 0;
	if (!m_window->getWindowRect(x, y, outerW, outerH)) { return; }
	// moveWindow は枠込みの大きさを取るので、今の枠の厚みを足す
	m_window->moveWindow(x, y, w + (outerW - m_window->width()), h + (outerH - m_window->height()));
}

MITIRU_INLINE void mitiru::Engine::setQualityCaps(const render::QualityCaps& caps) noexcept
{
	m_config.qualityCaps = caps;
#ifdef _WIN32
	if (auto* dx12 = dynamic_cast<render::Renderer3D_DX12*>(m_renderer3D.get())) { dx12->setQualityCaps(caps); }
#endif
}

MITIRU_INLINE void mitiru::Engine::setWindowIcon(std::string_view icoPath) noexcept
{
	// IWindow の仮想 default が no-op なので headless / 非 Win32 でも安全。
	if (m_window) { m_window->setIcon(icoPath); }
}

MITIRU_INLINE bool mitiru::Engine::isFullscreen() const noexcept
{
#ifdef _WIN32
	const auto* w32 = dynamic_cast<const Win32Window*>(m_window.get());
	return w32 && w32->isFullscreen();
#else
	return false;
#endif
}
