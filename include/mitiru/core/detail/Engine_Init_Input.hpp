// Detail header for mitiru::Engine — do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── Injected input application out-of-class definition ───────────────────

MITIRU_INLINE void mitiru::Engine::applyInjectedInput()
{
	const auto commands = m_inputInjector.consumePending();
	for (const auto& cmd : commands)
	{
		switch (cmd.type)
		{
		case InputCommandType::KeyDown:
			m_inputState.setKeyDown(cmd.keyCode, true);
			break;
		case InputCommandType::KeyUp:
			m_inputState.setKeyDown(cmd.keyCode, false);
			break;
		case InputCommandType::MouseMove:
			m_inputState.setMousePosition(cmd.mouseX, cmd.mouseY);
			break;
		case InputCommandType::MouseDown:
			m_inputState.setMouseButtonDown(
				static_cast<MouseButton>(cmd.mouseButton), true);
			break;
		case InputCommandType::MouseUp:
			m_inputState.setMouseButtonDown(
				static_cast<MouseButton>(cmd.mouseButton), false);
			break;
		}
	}
}
