#pragma once

/// @file BusDspConfig.hpp
/// @brief mix.json の eq / compressor / limiter の設定。何も書かなければ空で、engine のバスには何も差さない
/// @details EQ の場所ごとの切り替えは、reverb.zones の id に合わせる (箱は残響と共有し、別の箱を持たない)。
///          場所の設定に書いたバスだけが既定の設定を置き換え、書かなかったバスは既定のまま。

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <mitiru/audio/mix/BusDucker.hpp>
#include <mitiru/audio/mix/BusDynamics.hpp>
#include <mitiru/audio/mix/BusEq.hpp>

namespace mitiru::audio::mix
{

/// @brief バスごと (添字は MixBus) の EQ の帯域。has は書いてあるか (空の配列でも true で、場所で既定の EQ を外すのに使う)
struct EqBusSet
{
	std::array<EqBands, kMixBusCount> bands{};
	std::array<bool, kMixBusCount>    has{};

	/// 帯域が 1 本でも掛かるか
	[[nodiscard]] bool active(std::size_t bus) const noexcept
	{
		return has[bus] && std::any_of(bands[bus].begin(), bands[bus].end(), [](const EqBand& b) { return b.type != EqBandType::Off; });
	}
};

struct EqZoneSet
{
	std::string id;
	EqBusSet    set;
};

struct BusDspConfig
{
	float                                                    eqFadeSec = 0.2f;
	EqBusSet                                                 eq;
	std::vector<EqZoneSet>                                   eqZones;
	std::array<std::optional<CompressorSettings>, kMixBusCount> compressor;
	std::optional<LimiterSettings>                           limiter;

	/// @brief bus に EQ の帯域が 1 本でも掛かりうるか (既定、またはどれかの場所)
	[[nodiscard]] bool hasEq(MixBus bus) const noexcept
	{
		const auto b = static_cast<std::size_t>(bus);
		if (eq.active(b)) { return true; }
		for (const EqZoneSet& z : eqZones) { if (z.set.active(b)) { return true; } }
		return false;
	}

	[[nodiscard]] bool any() const noexcept
	{
		if (limiter) { return true; }
		for (std::size_t b = 0; b < kMixBusCount; ++b)
		{
			if (compressor[b] || hasEq(static_cast<MixBus>(b))) { return true; }
		}
		return false;
	}

	/// @brief zone 番号 (eqZones の添字、-1 なら既定) での bus の帯域
	[[nodiscard]] const EqBands& eqFor(MixBus bus, int zone) const noexcept
	{
		const auto b = static_cast<std::size_t>(bus);
		if (zone >= 0 && static_cast<std::size_t>(zone) < eqZones.size() && eqZones[static_cast<std::size_t>(zone)].set.has[b])
		{
			return eqZones[static_cast<std::size_t>(zone)].set.bands[b];
		}
		return eq.bands[b];
	}

	/// @brief 掛かっているものの名前。--audio-capture の記録と selfcheck の summary.md の頭に出す
	[[nodiscard]] std::vector<std::string> activeNames() const
	{
		static constexpr const char* kBusNames[kMixBusCount] = {"master", "music", "sfx", "voice", "ui", "ambient", "user6", "user7"};
		std::vector<std::string> out;
		for (std::size_t b = 0; b < kMixBusCount; ++b)
		{
			if (hasEq(static_cast<MixBus>(b))) { out.push_back(std::string("eq:") + kBusNames[b]); }
			if (compressor[b]) { out.push_back(std::string("compressor:") + kBusNames[b]); }
		}
		if (limiter) { out.push_back("limiter"); }
		return out;
	}
};

}  // namespace mitiru::audio::mix
