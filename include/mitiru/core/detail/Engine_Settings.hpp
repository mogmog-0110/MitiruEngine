// Detail header for mitiru::Engine — do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── Settings & persistence out-of-class definitions ──────────────────────

MITIRU_INLINE bool mitiru::Engine::saveSettings() noexcept
{
	if (!m_config.persistSettings) { return false; }
	return GameSettings::saveFrom(m_config);
}

MITIRU_INLINE void mitiru::Engine::persistIfEnabled() noexcept
{
	if (m_loopConfig && m_loopConfig->persistSettings)
	{
		GameSettings::saveFrom(*m_loopConfig);
	}
}
