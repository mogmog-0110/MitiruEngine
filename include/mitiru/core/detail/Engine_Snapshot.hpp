// Detail header for mitiru::Engine - do not include directly; included via core/Engine.hpp
#pragma once

#include <mitiru/core/InlineMacro.hpp>

// ── Scene snapshot serialization out-of-class definitions ────────────────

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
