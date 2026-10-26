// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

MITIRU_INLINE void mitiru::Engine::setKeyMode(input::KeyMode mode) noexcept
{
	m_config.keyMode = mode;
#ifdef _WIN32
	if (auto* w32 = dynamic_cast<Win32Window*>(m_window.get())) { w32->setKeyMode(mode); }
#endif
}

MITIRU_INLINE void mitiru::Engine::setStickShaping(const input::StickShaping& shaping) noexcept
{
	m_config.stickShaping = shaping;
	m_gamepads.setShaping(shaping);
}
