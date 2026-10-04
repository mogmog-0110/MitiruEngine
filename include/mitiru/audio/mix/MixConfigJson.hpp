#pragma once

/// @file MixConfigJson.hpp
/// @brief assets/audio/mix.json にあるバス間のダッキングと場所ごとの残響を読む
/// @details 書式は docs/AUDIO.md に記載する。ファイルが無いか "ducking" の記載が無いときは defaultDuckRules() を使う。

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/audio/mix/BusDucker.hpp>
#include <mitiru/audio/mix/ReverbZones.hpp>

namespace mitiru::audio::mix
{

/// @brief 何も書かないときのダッキング。台詞の間は BGM を 6 dB 下げる。大きな効果音 (音量 0.7 以上) の直後は BGM を半分にし、0.4 秒で戻す。
[[nodiscard]] inline std::vector<DuckRule> defaultDuckRules()
{
	return {
		DuckRule{MixBus::Voice, MixBus::Music, -6.0f, 0.1f, 0.0f, 0.5f, 0.0f},
		DuckRule{MixBus::Sfx, MixBus::Music, -6.02f, 0.0f, 0.0f, 0.4f, 0.7f},
	};
}

struct MixConfig
{
	std::vector<DuckRule> ducking = defaultDuckRules();
	ReverbSettings        reverb;
};

struct MixConfigParse
{
	MixConfig   config;
	std::string error;   ///< 空なら成功

	[[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

namespace detail
{

inline bool parseBus(const nlohmann::json& j, const char* key, MixBus& out)
{
	static constexpr std::pair<const char*, MixBus> kNames[] = {
		{"master", MixBus::Master}, {"music", MixBus::Music}, {"sfx", MixBus::Sfx}, {"voice", MixBus::Voice},
		{"ui", MixBus::Ui}, {"ambient", MixBus::Ambient}, {"user6", MixBus::User6}, {"user7", MixBus::User7}};
	const std::string name = j.at(key).get<std::string>();
	for (const auto& [n, b] : kNames)
	{
		if (name == n) { out = b; return true; }
	}
	return false;
}

inline std::string parseDucking(const nlohmann::json& list, std::vector<DuckRule>& out)
{
	out.clear();
	for (const auto& j : list)
	{
		DuckRule r;
		if (!parseBus(j, "sidechain", r.sidechain) || !parseBus(j, "target", r.target))
		{
			return "ducking: sidechain / target は master / music / sfx / voice / ui / ambient / user6 / user7";
		}
		r.depthDb = j.value("depthDb", r.depthDb);
		r.attackSec = j.value("attackSec", r.attackSec);
		r.holdSec = j.value("holdSec", r.holdSec);
		r.releaseSec = j.value("releaseSec", r.releaseSec);
		r.threshold = j.value("threshold", r.threshold);
		if (r.depthDb > 0.0f || r.attackSec < 0.0f || r.holdSec < 0.0f || r.releaseSec < 0.0f)
		{
			return "ducking: depthDb は 0 以下、秒は 0 以上";
		}
		out.push_back(r);
	}
	return {};
}

inline ReverbPreset parsePreset(const nlohmann::json& j, ReverbPreset p)
{
	p.roomSize = j.value("roomSize", p.roomSize);
	p.damping = j.value("damping", p.damping);
	p.send = j.value("send", p.send);
	return p;
}

inline std::string parseReverb(const nlohmann::json& j, ReverbSettings& out)
{
	out.fadeSec = j.value("fadeSec", out.fadeSec);
	if (j.contains("outside")) { out.outside = parsePreset(j.at("outside"), out.outside); }
	for (const auto& z : j.value("zones", nlohmann::json::array()))
	{
		ReverbZone zone;
		zone.id = z.at("id").get<std::string>();
		for (int i = 0; i < 3; ++i)
		{
			zone.min[static_cast<std::size_t>(i)] = z.at("min").at(i).get<float>();
			zone.max[static_cast<std::size_t>(i)] = z.at("max").at(i).get<float>();
			if (zone.min[static_cast<std::size_t>(i)] > zone.max[static_cast<std::size_t>(i)])
			{
				return "reverb.zones." + zone.id + ": min が max より大きい";
			}
		}
		zone.preset = parsePreset(z, ReverbPreset{0.5f, 0.5f, 0.3f});
		zone.priority = z.value("priority", 0);
		out.zones.push_back(std::move(zone));
	}
	return {};
}

}  // namespace detail

[[nodiscard]] inline MixConfigParse parseMixConfig(std::string_view text)
{
	MixConfigParse out;
	std::string syntaxError;
	const auto root = data::parseJsonText(text, "mix.json", syntaxError);
	if (root.is_discarded() || !root.is_object())
	{
		out.error = syntaxError.empty() ? "JSON の書き方が間違っているか、一番外側が { } のオブジェクトではありません" : syntaxError;
		return out;
	}
	try
	{
		if (root.contains("ducking")) { out.error = detail::parseDucking(root.at("ducking"), out.config.ducking); }
		if (out.ok() && root.contains("reverb")) { out.error = detail::parseReverb(root.at("reverb"), out.config.reverb); }
	}
	catch (const nlohmann::json::exception& e)
	{
		out.error = std::string("mix.json の項目の型か必須項目が違う: ") + e.what();
	}
	if (!out.ok()) { out.config = MixConfig{}; }
	return out;
}

}  // namespace mitiru::audio::mix
