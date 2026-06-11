// mitiru::Engine 用の detail header — 直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── inject された input の適用 (クラス外定義) ───────────────────

MITIRU_INLINE void mitiru::Engine::applyInjectedInput()
{
	// Replay path: 現フレームの記録済み command を consume する前に injector へ
	// 流し込む。そうすれば下の KeyDown / MouseMove 等の switch を live-inject された
	// command と同じ経路で通る。これにより consumePending() 結果への recorder hook が
	// 正直になる — replay された frame は元の frame として再記録される。
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
			// injected 版: focus 喪失の clearHeldKeys に消されない (背景実行の AI 操作を守る)。
			m_inputState.setKeyDownInjected(cmd.keyCode, true);
			break;
		case InputCommandType::KeyUp:
			m_inputState.setKeyDownInjected(cmd.keyCode, false);
			break;
		case InputCommandType::MouseMove:
			m_inputState.setMousePosition(cmd.mouseX, cmd.mouseY);
			break;
		case InputCommandType::MouseDown:
			m_inputState.setMouseButtonDownInjected(
				static_cast<MouseButton>(cmd.mouseButton), true);
			break;
		case InputCommandType::MouseUp:
			m_inputState.setMouseButtonDownInjected(
				static_cast<MouseButton>(cmd.mouseButton), false);
			break;
		}
	}

	// deterministic replay (axis 4) 用に inject された input を記録する。command の無い
	// frame はスキップし、ファイルサイズを総ランタイムではなく実イベント数に比例させる。
	if (m_inputRecorder.isRecording() && !commands.empty() && m_clock)
	{
		m_inputRecorder.recordFrame(m_clock->frameNumber(), commands);
	}
}
