// Detail header for mitiru::Engine - do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── Window mode helpers out-of-class definitions ─────────────────────────

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
