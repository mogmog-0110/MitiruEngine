#pragma once

/// @file MixDspJson.hpp
/// @brief mix.json の "eq" "compressor" "limiter" を BusDspConfig に読む。書式は docs/AUDIO.md
/// @details 範囲を外れた値は黙って丸めず、書いた場所つきで誤りにする (帯域の設定を誤ると音が割れるため)。

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/audio/mix/BusDspConfig.hpp>

namespace mitiru::audio::mix::detail
{

inline std::string parseEqBand(const nlohmann::json& j, const std::string& where, EqBand& out)
{
	if (!j.is_object()) { return where + ": { } のオブジェクトで書く"; }
	EqBand b;
	if (!eqTypeFromName(j.at("type").get<std::string>(), b.type))
	{
		return where + ": type は peaking / lowShelf / highShelf / highPass / lowPass / bandPass / notch";
	}
	b.hz = j.at("hz").get<float>();
	b.gainDb = j.value("gainDb", b.gainDb);
	b.q = j.value("q", b.q);
	if (!eqBandUsesGain(b.type) && j.contains("gainDb")) { return where + ": " + j.at("type").get<std::string>() + " は gainDb を使わない"; }
	if (const std::string why = validateEqBand(b); !why.empty()) { return where + ": " + why; }
	out = b;
	return {};
}

inline std::string parseEqBuses(const nlohmann::json& buses, const std::string& where, EqBusSet& out)
{
	if (!buses.is_object()) { return where + ": バスの名前ごとの帯域の並びを { } で書く"; }
	for (const auto& [name, list] : buses.items())
	{
		MixBus bus;
		if (!mixBusFromName(name, bus)) { return where + "." + name + ": バスは master / music / sfx / voice / ui / ambient / user6 / user7"; }
		if (!list.is_array() || list.size() > kEqMaxBands) { return where + "." + name + ": 帯域は 8 本までの配列"; }
		const auto b = static_cast<std::size_t>(bus);
		out.bands[b] = EqBands{};
		for (std::size_t i = 0; i < list.size(); ++i)
		{
			const std::string at = where + "." + name + "[" + std::to_string(i) + "]";
			if (std::string err = parseEqBand(list[i], at, out.bands[b][i]); !err.empty()) { return err; }
		}
		out.has[b] = true;
	}
	return {};
}

inline std::string parseEq(const nlohmann::json& j, BusDspConfig& out)
{
	if (!j.is_object()) { return "eq: { } のオブジェクトで書く"; }
	out.eqFadeSec = j.value("fadeSec", out.eqFadeSec);
	if (!(out.eqFadeSec >= 0.0f && out.eqFadeSec <= 10.0f)) { return "eq.fadeSec は 0 以上、10 以下"; }
	if (j.contains("buses"))
	{
		if (std::string err = parseEqBuses(j.at("buses"), "eq.buses", out.eq); !err.empty()) { return err; }
	}
	for (const auto& z : j.value("zones", nlohmann::json::array()))
	{
		EqZoneSet zone;
		zone.id = z.at("id").get<std::string>();
		if (std::string err = parseEqBuses(z.at("buses"), "eq.zones." + zone.id, zone.set); !err.empty()) { return err; }
		out.eqZones.push_back(std::move(zone));
	}
	return {};
}

inline std::string parseCompressor(const nlohmann::json& j, BusDspConfig& out)
{
	if (!j.is_object()) { return "compressor: バスの名前ごとの設定を { } で書く"; }
	for (const auto& [name, c] : j.items())
	{
		MixBus bus;
		if (!mixBusFromName(name, bus)) { return "compressor." + name + ": バスは master / music / sfx / voice / ui / ambient / user6 / user7"; }
		CompressorSettings s;
		s.thresholdDb = c.value("thresholdDb", s.thresholdDb);
		s.ratio = c.value("ratio", s.ratio);
		s.attackSec = c.value("attackSec", s.attackSec);
		s.releaseSec = c.value("releaseSec", s.releaseSec);
		s.kneeDb = c.value("kneeDb", s.kneeDb);
		s.makeupDb = c.value("makeupDb", s.makeupDb);
		if (const char* why = validateCompressor(s); *why != '\0') { return "compressor." + name + ": " + why; }
		out.compressor[static_cast<std::size_t>(bus)] = s;
	}
	return {};
}

inline std::string parseLimiter(const nlohmann::json& j, BusDspConfig& out)
{
	if (!j.is_object()) { return "limiter: { } のオブジェクトで書く"; }
	LimiterSettings l;
	l.ceilingDb = j.value("ceilingDb", l.ceilingDb);
	l.releaseSec = j.value("releaseSec", l.releaseSec);
	if (const char* why = validateLimiter(l); *why != '\0') { return std::string("limiter: ") + why; }
	out.limiter = l;
	return {};
}

/// @param zoneIds reverb.zones の id。EQ の場所の id はこの中のどれかでなければならない
inline std::string parseDsp(const nlohmann::json& root, const std::vector<std::string>& zoneIds, BusDspConfig& out)
{
	if (root.contains("eq")) { if (std::string err = parseEq(root.at("eq"), out); !err.empty()) { return err; } }
	if (root.contains("compressor")) { if (std::string err = parseCompressor(root.at("compressor"), out); !err.empty()) { return err; } }
	if (root.contains("limiter")) { if (std::string err = parseLimiter(root.at("limiter"), out); !err.empty()) { return err; } }
	for (const EqZoneSet& z : out.eqZones)
	{
		if (std::find(zoneIds.begin(), zoneIds.end(), z.id) == zoneIds.end())
		{
			return "eq.zones: id \"" + z.id + "\" が reverb.zones にありません (EQ の場所は残響の箱を共有します)";
		}
	}
	return {};
}

}  // namespace mitiru::audio::mix::detail
