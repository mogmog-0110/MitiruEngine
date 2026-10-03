// mitiru::Engine の detail ヘッダー。直接 include せず、core/Engine.hpp 経由で include する
#pragma once

/// @file Engine_Streaming.hpp
/// @brief 資産を先に読む・手放す・ステージを切り替える host 側の入口と、読み込みの状態の集計
/// @details 読み込み自体は Renderer3D_DX12 (AssetStreamer と COPY キュー) と音の SoundPreloads がする。
///          ここは一覧の行 (resource/StagePlan.hpp の書式) を種類ごとに振り分けるだけで、ゲームの状態には触れない。

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/resource/StagePlan.hpp>
#ifdef _WIN32
#include <mitiru/render/Renderer3D_DX12.hpp>
#endif

namespace mitiru::detail
{

#ifdef _WIN32
[[nodiscard]] inline render::Renderer3D_DX12* dx12RendererOf(render::IRenderer3D* r) noexcept
{
	return dynamic_cast<render::Renderer3D_DX12*>(r);
}
#endif

/// @brief perf のツール窓に出す読み込みの節。まだ何も読んでいなければ null
[[nodiscard]] inline nlohmann::json streamingReportJson(const Engine::StreamingReport& r)
{
	if (r.pending == 0 && r.finished == 0 && r.cells == 0) { return nullptr; }
	return {{"pending", r.pending}, {"finished", r.finished}, {"failed", r.failed}, {"maxFinishMs", r.maxFinishMs},
	        {"residentBytes", r.residentBytes}, {"budgetBytes", r.budgetBytes}, {"vramUsageBytes", r.vramUsageBytes},
	        {"vramBudgetBytes", r.vramBudgetBytes}, {"cells", r.cells}, {"cellsResident", r.cellsResident},
	        {"cellsLoaded", r.cellsLoaded}, {"budgetSkips", r.budgetSkips}};
}

} // namespace mitiru::detail

MITIRU_INLINE mitiru::Engine::StreamingReport mitiru::Engine::streamingReport() const
{
	StreamingReport out;
#ifdef _WIN32
	if (const auto* dx12 = dynamic_cast<const render::Renderer3D_DX12*>(m_renderer3D.get()))
	{
		const auto s = dx12->streamingStats();
		out.pending = s.pending;
		out.finished = s.finished;
		out.failed = s.failed;
		out.maxFinishMs = s.maxFinishMs;
		out.residentBytes = dx12->residentAssetBytes();
		out.budgetBytes = dx12->streamingBudgetBytes();
		const auto vram = dx12->vramBudget();
		out.vramUsageBytes = vram.usageBytes;
		out.vramBudgetBytes = vram.budgetBytes;
		const auto cells = dx12->outdoorRegionStats();
		out.cells = cells.cells;
		out.cellsResident = cells.resident;
		out.cellsLoaded = cells.loaded;
		out.budgetSkips = dx12->regionBudgetSkips();
	}
#endif
	if (m_audioEngine) { out.pending += static_cast<std::uint32_t>(m_audioEngine->pendingSoundLoads()); }
	return out;
}

MITIRU_INLINE std::uint32_t mitiru::Engine::pendingLoadCount(std::uint64_t& failed) const
{
	std::uint32_t pending = 0;
	failed = 0;
#ifdef _WIN32
	if (const auto* dx12 = dynamic_cast<const render::Renderer3D_DX12*>(m_renderer3D.get()))
	{
		const auto s = dx12->streamingStats();
		pending = s.pending;
		failed = s.failed;
	}
#endif
	if (m_audioEngine) { pending += static_cast<std::uint32_t>(m_audioEngine->pendingSoundLoads()); }
	return pending;
}

MITIRU_INLINE bool mitiru::Engine::preloadAsset(std::string_view entry)
{
	const auto [kind, path] = resource::splitAssetKind(entry);
	bool requested = false;
	if (kind == "sound") { requested = m_audioEngine && m_audioEngine->preloadSound(path); }
#ifdef _WIN32
	else if (auto* dx12 = detail::dx12RendererOf(m_renderer3D.get()))
	{
		using K = render::Renderer3D_DX12::PreloadKind;
		const K k = kind == "clod" ? K::Clod : (kind == "model" ? K::Model : K::Auto);
		requested = dx12->preloadAsset(std::string(path).c_str(), k);
	}
#endif
	// 頼めなかった先読みは、ゲームの preloadFailed (ABI v50) に読めなかった資産として数える
	if (!requested) { ++m_preloadRejected; }
	return requested;
}

MITIRU_INLINE bool mitiru::Engine::releaseAsset(std::string_view entry)
{
	const auto [kind, path] = resource::splitAssetKind(entry);
	if (kind == "sound") { return m_audioEngine && m_audioEngine->releaseSound(path); }
	return m_renderer3D && m_renderer3D->releaseModel(std::string(path).c_str());
}

MITIRU_INLINE bool mitiru::Engine::assetReady(std::string_view entry)
{
	const auto [kind, path] = resource::splitAssetKind(entry);
	if (kind == "sound") { return m_audioEngine && m_audioEngine->preloadSound(path); }
#ifdef _WIN32
	if (auto* dx12 = detail::dx12RendererOf(m_renderer3D.get()))
	{
		using K = render::Renderer3D_DX12::PreloadKind;
		const K k = kind == "clod" ? K::Clod : (kind == "model" ? K::Model : K::Auto);
		return dx12->assetReady(std::string(path).c_str(), k);
	}
#endif
	return false;
}

MITIRU_INLINE std::size_t mitiru::Engine::switchStage(const std::vector<std::string>& entries)
{
	std::uint64_t failed = 0;
	(void)pendingLoadCount(failed);
	m_preloadFailedBase = failed;
	m_preloadRejected = 0;
	const auto plan = resource::planStageSwitch(m_stageAssets, entries);
	for (const auto& e : plan.release) { (void)releaseAsset(e); }
	std::size_t requested = 0;
	for (const auto& e : plan.load) { requested += preloadAsset(e) ? 1 : 0; }
	m_stageAssets = entries;
	return requested;
}

MITIRU_INLINE void mitiru::Engine::settleLoads()
{
#ifdef _WIN32
	if (auto* dx12 = detail::dx12RendererOf(m_renderer3D.get())) { (void)dx12->settleLoads(); }
#endif
}

MITIRU_INLINE int mitiru::Engine::prewarmPipelines()
{
#ifdef _WIN32
	if (auto* dx12 = detail::dx12RendererOf(m_renderer3D.get())) { return dx12->prewarmPipelines(); }
#endif
	return 0;
}
