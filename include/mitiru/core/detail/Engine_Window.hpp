// mitiru::Engine 用の detail header — 直接インクルードしない。core/Engine.hpp 経由で取り込む
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

MITIRU_INLINE bool mitiru::Engine::isFullscreen() const noexcept
{
#ifdef _WIN32
	const auto* w32 = dynamic_cast<const Win32Window*>(m_window.get());
	return w32 && w32->isFullscreen();
#else
	return false;
#endif
}
