// mitiru::Engine の detail header。直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module_Boundary50.hpp
/// @brief ABI v50 (ADR 0067) で開いた入口の host 側。先読みの依頼と進み、オフラインの人ごとの操作、今の機器、
///        カットシーンの印、asset.reloaded の後の資産の写し替え、story のツール窓へ送る GameMemory の範囲。
/// @details 先読みの進み (preloadPending / preloadFailed) と今の機器は InputSnapshot に書くので録画に乗り、再生では
///          記録の値に置き換わる。カットシーンの印とツール窓への写しは録画に乗らない (表示だけ)。

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/core/InlineMacro.hpp>
#include <mitiru/input/InputDeviceKind.hpp>
#include <mitiru/module/detail/HostBoundary50.hpp>

MITIRU_INLINE void mitiru::Engine::fillModuleSnapshotV50(module::InputSnapshot& snap)
{
	std::uint64_t failed = 0;
	snap.preloadPending = pendingLoadCount(failed);
	snap.preloadFailed = static_cast<std::uint32_t>(std::min<std::uint64_t>(
		(failed - std::min(failed, m_preloadFailedBase)) + m_preloadRejected, 0xFFFFFFFFu));
	snap.netPlayerCount = 0;
}

MITIRU_INLINE void mitiru::Engine::finishModuleSnapshotV50(module::InputSnapshot& snap)
{
	// 台本と利用者の割り当てを通した後の入力で決める (録画が残すのもこの入力)
	m_inputDevice = input::lastUsedDevice(snap, m_inputDevice);
	snap.inputDevice = static_cast<std::uint8_t>(m_inputDevice);
	snap.padFamily = static_cast<std::uint8_t>(input::connectedPadFamily(snap));
	module::detail::fillOfflinePlayerActions(snap);
}

MITIRU_INLINE void mitiru::Engine::applyPreloadIntents(const module::FrameIntents& intents)
{
	const int n = std::clamp(static_cast<int>(intents.preloadCount), 0, module::kMaxPreloadIntents);
	std::vector<std::string> stage;
	bool hasStage = false;
	for (int i = 0; i < n; ++i)
	{
		const module::PreloadIntent& p = intents.preloads[i];
		const std::string path(p.path, module::detail::boundedLen(p.path));
		if (p.op == module::kPreloadLoad) { (void)preloadAsset(path); }
		else if (p.op == module::kPreloadRelease) { (void)releaseAsset(path); }
		else if (p.op == module::kPreloadStage)
		{
			hasStage = true;
			if (!path.empty()) { stage.push_back(path); }
		}
	}
	if (hasStage) { (void)switchStage(stage); }
}

MITIRU_INLINE void mitiru::Engine::drainModuleIntentsV50(const module::FrameIntents& intents)
{
	applyPreloadIntents(intents);
	const bool cinematic = intents.cinematicActive != 0;
	if (cinematic != m_moduleCinematic)
	{
		m_moduleCinematic = cinematic;
		m_rmlUi.setBool("view.cinematic", cinematic);
	}
	m_timelineSpans.record(++m_timelineFrame, kFixedDt, intents.timelineMarkers,
	                       std::clamp(intents.timelineMarkerCount, 0, module::kMaxTimelineMarkers));
	// asset.reloaded を受けた update の後で、ツール窓に見せる資産をもう一度写す (木の形や旗の名前が JSON の書き換えに付いてくる)
	if (m_moduleInputSnapshot && module::detail::hasAssetReload(*m_moduleInputSnapshot)) { ++m_inspectAssetsGeneration; }
}

MITIRU_INLINE void mitiru::Engine::publishModuleStory(nlohmann::json& snapshotOut) const
{
	if (m_storySlots.empty() || m_moduleMemory == nullptr) { return; }
	nlohmann::json slots = nlohmann::json::array();
	const auto* base = static_cast<const std::uint8_t*>(m_moduleMemory);
	for (const auto& [offset, size] : m_storySlots)
	{
		const bool fits = size > 0 && std::uint64_t{offset} + size <= m_moduleMemorySize;
		slots.push_back(fits ? module::detail::toHex(base + offset, size) : std::string());
	}
	snapshotOut["story"] = nlohmann::json{{"title", "story"}, {"state", nlohmann::json{{"slots", std::move(slots)}}}};
}
