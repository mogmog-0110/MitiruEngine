// Detail header for mitiru::Engine — do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── Injected input application out-of-class definition ───────────────────

MITIRU_INLINE void mitiru::Engine::applyInjectedInput()
{
	// Replay path: pump recorded commands for the current frame into the
	// injector BEFORE consuming, so they flow through the same KeyDown /
	// MouseMove / etc. switch below as live-injected commands. This keeps
	// the recorder hook on the consumePending() result honest — replayed
	// frames re-record as the same frame they came from.
	if (m_replayActive && m_clock)
	{
		const auto replayed = m_inputReplayer.getCommandsForFrame(
			m_clock->frameNumber());
		for (const auto& cmd : replayed)
		{
			m_inputInjector.inject(cmd);
		}
	}

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

	// Record injected input for deterministic replay (axis 4). Frames with no
	// commands are skipped to keep the file size proportional to actual events
	// rather than total runtime.
	if (m_inputRecorder.isRecording() && !commands.empty() && m_clock)
	{
		m_inputRecorder.recordFrame(m_clock->frameNumber(), commands);
	}
}
