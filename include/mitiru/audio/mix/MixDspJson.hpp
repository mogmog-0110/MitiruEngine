#pragma once

/// @file MixDspJson.hpp
/// @brief mix.json の "eq" "compressor" "limiter" を BusDspConfig に読む。書式は docs/AUDIO.md
/// @details 範囲を外れた値は黙って丸めず、書いた場所つきで誤りにする (帯域の設定を誤ると音が割れるため)。

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/audio/mix/BusDspConfig.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

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

/// 1 バスに 1 つの効果 (distortion / pitchShift / chorus / delay) を読む共通の形。read は設定を埋めて誤りの理由を返す
template <class Settings, class Read>
std::string parseBusEffects(const nlohmann::json& j, const char* key,
                            std::array<std::optional<Settings>, kMixBusCount>& out, Read read)
{
	if (!j.is_object()) { return std::string(key) + ": バスの名前ごとの設定を { } で書く"; }
	for (const auto& [name, c] : j.items())
	{
		const std::string at = std::string(key) + "." + name;
		MixBus bus;
		if (!mixBusFromName(name, bus)) { return at + ": バスは music / sfx / voice / ui / ambient / user6 / user7"; }
		if (bus == MixBus::Master) { return at + ": master には掛けない (master は eq / compressor / limiter だけ)"; }
		if (!c.is_object()) { return at + ": { } のオブジェクトで書く"; }
		Settings s;
		if (const std::string why = read(c, s); !why.empty()) { return at + ": " + why; }
		out[static_cast<std::size_t>(bus)] = s;
	}
	return {};
}

inline std::string readDistortion(const nlohmann::json& c, DistortionSettings& s)
{
	if (std::string e = data::unknownKeyError(c, {"driveDb", "mix", "levelDb"}, "設定"); !e.empty()) { return e; }
	s.driveDb = c.value("driveDb", s.driveDb);
	s.mix = c.value("mix", s.mix);
	s.levelDb = c.value("levelDb", s.levelDb);
	return validateDistortion(s);
}

inline std::string readPitchShift(const nlohmann::json& c, PitchShiftSettings& s)
{
	if (std::string e = data::unknownKeyError(c, {"semitones", "algorithm", "blockMs", "grainMs", "tonalityLimitHz"}, "設定"); !e.empty()) { return e; }
	s.semitones = c.value("semitones", s.semitones);
	s.blockMs = c.value("blockMs", s.blockMs);
	s.grainMs = c.value("grainMs", s.grainMs);
	s.tonalityLimitHz = c.value("tonalityLimitHz", s.tonalityLimitHz);
	if (c.contains("algorithm") && !pitchShiftAlgorithmFromName(c.at("algorithm").get<std::string>(), s.algorithm))
	{
		return "algorithm は signalsmith / grain";
	}
	return validatePitchShift(s);
}

inline std::string readChorus(const nlohmann::json& c, ChorusSettings& s)
{
	if (std::string e = data::unknownKeyError(c, {"rateHz", "depthMs", "delayMs", "mix"}, "設定"); !e.empty()) { return e; }
	s.rateHz = c.value("rateHz", s.rateHz);
	s.depthMs = c.value("depthMs", s.depthMs);
	s.delayMs = c.value("delayMs", s.delayMs);
	s.mix = c.value("mix", s.mix);
	return validateChorus(s);
}

inline std::string readDelay(const nlohmann::json& c, DelaySettings& s)
{
	if (std::string e = data::unknownKeyError(c, {"timeSec", "feedback", "wet", "dampHz"}, "設定"); !e.empty()) { return e; }
	s.timeSec = c.value("timeSec", s.timeSec);
	s.feedback = c.value("feedback", s.feedback);
	s.wet = c.value("wet", s.wet);
	s.dampHz = c.value("dampHz", s.dampHz);
	return validateDelay(s);
}

inline std::string parseTimeFlow(const nlohmann::json& j, BusDspConfig& out)
{
	if (!j.is_object()) { return "timeFlow: { } のオブジェクトで書く"; }
	if (std::string e = data::unknownKeyError(j, {"bus", "pitchAtStop", "lowPassHzAtStop", "rampSec", "followHitStop", "followTimeScale"}, "timeFlow"); !e.empty()) { return e; }
	TimeFlowSettings t;
	if (j.contains("bus") && !mixBusFromName(j.at("bus").get<std::string>(), t.bus)) { return "timeFlow.bus: バスは music / sfx / ui / ambient / user6 / user7"; }
	t.pitchAtStop = j.value("pitchAtStop", t.pitchAtStop);
	t.lowPassHzAtStop = j.value("lowPassHzAtStop", t.lowPassHzAtStop);
	t.rampSec = j.value("rampSec", t.rampSec);
	t.followHitStop = j.value("followHitStop", t.followHitStop);
	t.followTimeScale = j.value("followTimeScale", t.followTimeScale);
	if (const char* why = validateTimeFlow(t); *why != '\0') { return std::string("timeFlow: ") + why; }
	out.timeFlow = t;
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
	auto effects = [&](const char* key, auto& slots, auto read) -> std::string {
		return root.contains(key) ? parseBusEffects(root.at(key), key, slots, read) : std::string{};
	};
	if (std::string err = effects("distortion", out.distortion, readDistortion); !err.empty()) { return err; }
	if (std::string err = effects("pitchShift", out.pitchShift, readPitchShift); !err.empty()) { return err; }
	if (std::string err = effects("chorus", out.chorus, readChorus); !err.empty()) { return err; }
	if (std::string err = effects("delay", out.delay, readDelay); !err.empty()) { return err; }
	if (root.contains("timeFlow")) { if (std::string err = parseTimeFlow(root.at("timeFlow"), out); !err.empty()) { return err; } }
	if (root.contains("binaural"))
	{
		if (!root.at("binaural").is_boolean()) { return "binaural: true か false で書く"; }
		out.binaural = root.at("binaural").get<bool>();
	}
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
