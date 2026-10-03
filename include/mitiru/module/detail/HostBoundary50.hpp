#pragma once

/// @file HostBoundary50.hpp
/// @brief ABI v50 (ADR 0067) の host 側の小さな関数 (Engine を持ち込まずにテストする)。使うのは core/detail/Engine_Module_*.hpp。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/module/InspectAssetsHost.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::module::detail
{

/// @brief オフラインでは手元の 1 人が席 0。オンラインは network/RollbackInput.hpp の合成が席ごとに書く
inline void fillOfflinePlayerActions(InputSnapshot& snap) noexcept
{
	if (snap.netPlayerCount != 0) { return; }
	for (int p = 0; p < kMaxNetPlayers; ++p)
	{
		snap.actionsDownByPlayer[p] = (p == 0) ? snap.actionsDown : 0u;
		snap.actionsPressedByPlayer[p] = (p == 0) ? snap.actionsPressed : 0u;
		snap.actionsReleasedByPlayer[p] = (p == 0) ? snap.actionsReleased : 0u;
	}
}

/// @brief このフレームの action event に asset.reloaded があるか
[[nodiscard]] inline bool hasAssetReload(const InputSnapshot& snap) noexcept
{
	const int n = std::clamp(snap.actionEventCount, 0, 16);
	for (int i = 0; i < n; ++i)
	{
		const char* name = snap.actionEvents[i].name;
		if (std::string_view(name, strnlen(name, sizeof(snap.actionEvents[i].name))) == "asset.reloaded") { return true; }
	}
	return false;
}

/// @brief story の資産 (narrative/StoryInspect.hpp の JSON) の slots のうち、GameMemory の中に収まる範囲。
///        収まらない slot は大きさ 0 にして順番を保つ (ツール窓は同じ順で読む)
[[nodiscard]] inline std::vector<std::pair<std::uint32_t, std::uint32_t>> storySlotsOf(
	const std::vector<CopiedInspectAsset>& assets, std::uint32_t memorySize)
{
	constexpr std::uint64_t kMaxSlotBytes = 64u * 1024u;
	std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
	for (const CopiedInspectAsset& a : assets)
	{
		if (a.kind != "story") { continue; }
		const auto j = nlohmann::json::parse(a.bytes.begin(), a.bytes.end(), nullptr, false);
		if (!j.is_object() || !j.contains("slots") || !j["slots"].is_array()) { return out; }
		for (const auto& s : j["slots"])
		{
			const std::uint64_t offset = s.value("offset", std::uint64_t{0});
			const std::uint64_t size = s.value("size", std::uint64_t{0});
			const bool fits = size > 0 && size <= kMaxSlotBytes && offset <= memorySize && size <= memorySize - offset;
			out.emplace_back(fits ? static_cast<std::uint32_t>(offset) : 0u, fits ? static_cast<std::uint32_t>(size) : 0u);
		}
		return out;
	}
	return out;
}

[[nodiscard]] inline std::string toHex(const std::uint8_t* p, std::size_t n)
{
	static constexpr char kDigits[] = "0123456789abcdef";
	std::string s(n * 2, '0');
	for (std::size_t i = 0; i < n; ++i)
	{
		s[i * 2] = kDigits[p[i] >> 4];
		s[i * 2 + 1] = kDigits[p[i] & 15];
	}
	return s;
}

}  // namespace mitiru::module::detail
