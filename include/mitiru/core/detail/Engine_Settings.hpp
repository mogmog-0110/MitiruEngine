// mitiru::Engine 用の detail header — 直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── settings & persistence のクラス外定義 ──────────────────────

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
