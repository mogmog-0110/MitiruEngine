// mitiru::Engine 用の detail header — 直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <algorithm>
#include <vector>

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

	// 前フレームで遅延させた tap の release を先に適用する (down を 1 フレーム見せてから離す)。
	for (const int vk : m_deferredInjectKeyUps)    { m_inputState.setKeyDownInjected(vk, false); }
	for (const int mb : m_deferredInjectMouseUps)  { m_inputState.setMouseButtonDownInjected(static_cast<MouseButton>(mb), false); }
	m_deferredInjectKeyUps.clear();
	m_deferredInjectMouseUps.clear();

	const auto commands = m_inputInjector.consumePending();
	// このバッチ内で down した key/button。同バッチの up はタップ扱いで遅延 release する。
	std::vector<int> downedKeys, downedMice;
	for (const auto& cmd : commands)
	{
		switch (cmd.type)
		{
		case InputCommandType::KeyDown:
			// injected 版: focus 喪失の clearHeldKeys に消されない (背景実行の AI 操作を守る)。
			m_inputState.setKeyDownInjected(cmd.keyCode, true);
			downedKeys.push_back(cmd.keyCode);
			break;
		case InputCommandType::KeyUp:
			// 同バッチで down した直後の up (= action="press" のタップ) は翌フレームへ遅延。
			// そうしないと同フレームで打ち消され just-pressed が観測されない。
			if (std::find(downedKeys.begin(), downedKeys.end(), cmd.keyCode) != downedKeys.end())
			{
				m_deferredInjectKeyUps.push_back(cmd.keyCode);
			}
			else
			{
				m_inputState.setKeyDownInjected(cmd.keyCode, false);  // 別フレームの hold 解除はそのまま
			}
			break;
		case InputCommandType::MouseMove:
			m_inputState.setMousePosition(cmd.mouseX, cmd.mouseY);
			break;
		case InputCommandType::MouseDown:
			m_inputState.setMouseButtonDownInjected(
				static_cast<MouseButton>(cmd.mouseButton), true);
			downedMice.push_back(cmd.mouseButton);
			break;
		case InputCommandType::MouseUp:
			if (std::find(downedMice.begin(), downedMice.end(), cmd.mouseButton) != downedMice.end())
			{
				m_deferredInjectMouseUps.push_back(cmd.mouseButton);
			}
			else
			{
				m_inputState.setMouseButtonDownInjected(
					static_cast<MouseButton>(cmd.mouseButton), false);
			}
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
