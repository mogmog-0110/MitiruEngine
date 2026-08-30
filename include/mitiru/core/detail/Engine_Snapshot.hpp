// mitiru::Engine 用の detail header。直接インクルードしない。core/Engine.hpp 経由で取り込む
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── scene snapshot serialization のクラス外定義 ────────────────

MITIRU_INLINE std::string mitiru::Engine::snapshot() const
{
	SnapshotData data;
	if (m_clock)
	{
		data.frameNumber = m_clock->frameNumber();
		data.timestamp = m_clock->elapsed();
	}

	/// ワールドが設定されていればエンティティ情報を含める
	if (m_world)
	{
		data.entityCount = static_cast<int>(m_world->entityCount());
		data.worldSnapshot = m_world->snapshot();
	}

	/// シーンマネージャーが設定されていればシーン情報を含める
	if (m_sceneManager)
	{
		const auto* current = m_sceneManager->currentScene();
		if (current)
		{
			data.sceneInfo = current->name();
		}
	}

	/// スクリーンが存在すれば描画コール数を含める
	if (m_screen)
	{
		data.drawCallCount = m_screen->drawCallCount();
	}

	return data.toJson();
}
