#pragma once

/// @file SfxBankJson.hpp
/// @brief assets/audio/sounds.json を SfxBank に読み込む。書式は docs/AUDIO.md。
/// @details 項目の型や範囲が正しくないときは、どの音の何が正しくないかを error に書いて失敗する。

#include <initializer_list>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <mitiru/data/JsonDiagnostics.hpp>

#include <mitiru/audio/sfx/SfxBank.hpp>

namespace mitiru::audio::sfx
{

struct SfxBankParse
{
	SfxBank     bank;
	std::string error;   ///< 空なら成功

	[[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

namespace detail
{

using Json = nlohmann::json;

struct BankError
{
	std::string message;
};

[[noreturn]] inline void bankFail(std::string message) { throw BankError{std::move(message)}; }

inline void rejectUnknownKeys(const Json& j, std::initializer_list<std::string_view> known, const std::string& where)
{
	if (std::string e = data::unknownKeyError(j, known, where); !e.empty()) { bankFail(std::move(e)); }
}

inline std::array<float, 2> range(const Json& j, const char* key, std::array<float, 2> fallback,
	const std::string& where)
{
	if (!j.contains(key)) { return fallback; }
	const Json& v = j.at(key);
	if (v.is_number()) { return {v.get<float>(), v.get<float>()}; }
	const std::array<float, 2> r{v.at(0).get<float>(), v.at(1).get<float>()};
	if (r[0] > r[1]) { bankFail(where + ": " + key + " は [小さい方, 大きい方]"); }
	return r;
}

inline AttenuationModel model(const std::string& name, const std::string& where)
{
	if (name == "none")        { return AttenuationModel::None; }
	if (name == "inverse")     { return AttenuationModel::InverseDistance; }
	if (name == "linear")      { return AttenuationModel::LinearDistance; }
	if (name == "exponential") { return AttenuationModel::ExponentialDistance; }
	bankFail(where + ": attenuation.model は none / inverse / linear / exponential");
}

inline SfxAttenuation attenuation(const Json& j, SfxAttenuation a, const std::string& where)
{
	rejectUnknownKeys(j, {"model", "min", "max", "rolloff", "curve"}, where + ".attenuation");
	if (j.contains("model")) { a.model = model(j.at("model").get<std::string>(), where); }
	a.minDistance = j.value("min", a.minDistance);
	a.maxDistance = j.value("max", a.maxDistance);
	a.rolloff = j.value("rolloff", a.rolloff);
	if (a.minDistance < 0.0f || a.maxDistance <= a.minDistance) { bankFail(where + ": attenuation は 0 <= min < max"); }
	if (j.contains("curve"))
	{
		a.curve.clear();
		for (const auto& p : j.at("curve"))
		{
			const float d = p.at(0).get<float>();
			if (!a.curve.empty() && d < a.curve.back()[0]) { bankFail(where + ": attenuation.curve の距離は昇順"); }
			a.curve.push_back({d, p.at(1).get<float>()});
		}
	}
	return a;
}

inline StealMode steal(const std::string& name, const std::string& where)
{
	if (name == "oldest")   { return StealMode::Oldest; }
	if (name == "quietest") { return StealMode::Quietest; }
	if (name == "none")     { return StealMode::None; }
	bankFail(where + ": steal は oldest / quietest / none");
}

inline SfxDef def(const Json& j, SfxDef d, const std::string& where)
{
	rejectUnknownKeys(j, {"variants", "volumeDb", "pitchSemitones", "attenuation", "priority", "maxInstances", "steal",
	                      "doppler", "occlusionDb", "occlusionLowpassHz"}, where);
	if (j.contains("variants"))
	{
		d.variants.clear();
		for (const auto& v : j.at("variants")) { d.variants.push_back(v.get<std::string>()); }
	}
	d.volumeDb = range(j, "volumeDb", d.volumeDb, where);
	d.pitchSemitones = range(j, "pitchSemitones", d.pitchSemitones, where);
	if (j.contains("attenuation")) { d.attenuation = attenuation(j.at("attenuation"), d.attenuation, where); }
	const int priority = j.value("priority", static_cast<int>(d.priority));
	if (priority < 0 || priority > 255) { bankFail(where + ": priority は 0..255"); }
	d.priority = static_cast<std::uint8_t>(priority);
	d.maxInstances = j.value("maxInstances", d.maxInstances);
	if (j.contains("steal")) { d.steal = steal(j.at("steal").get<std::string>(), where); }
	d.dopplerFactor = j.value("doppler", d.dopplerFactor);
	d.occlusionDb = j.value("occlusionDb", d.occlusionDb);
	d.occlusionLowpassHz = j.value("occlusionLowpassHz", d.occlusionLowpassHz);
	return d;
}

inline void voices(const Json& j, SfxBank& b)
{
	rejectUnknownKeys(j, {"maxReal", "maxVirtual", "virtualBelowDb"}, "voices");
	b.maxReal = j.value("maxReal", b.maxReal);
	b.maxVirtual = j.value("maxVirtual", b.maxVirtual);
	b.virtualBelowDb = j.value("virtualBelowDb", b.virtualBelowDb);
	if (b.maxReal <= 0 || b.maxVirtual < b.maxReal) { bankFail("voices: 0 < maxReal <= maxVirtual"); }
}

}  // namespace detail

[[nodiscard]] inline SfxBankParse parseSfxBank(std::string_view text)
{
	SfxBankParse out;
	std::string syntaxError;
	const auto root = data::parseJsonText(text, "sounds.json", syntaxError);
	if (root.is_discarded() || !root.is_object())
	{
		out.error = syntaxError.empty() ? "JSON の書き方が間違っているか、一番外側が { } のオブジェクトではありません" : syntaxError;
		return out;
	}
	try
	{
		detail::rejectUnknownKeys(root, {"seed", "voices", "defaults", "sounds"}, "sounds.json");
		out.bank.seed = root.value("seed", 0u);
		if (root.contains("voices")) { detail::voices(root.at("voices"), out.bank); }
		if (root.contains("defaults")) { out.bank.defaults = detail::def(root.at("defaults"), SfxDef{}, "defaults"); }
		const nlohmann::json sounds = root.value("sounds", nlohmann::json::object());
		for (const auto& [id, j] : sounds.items())
		{
			SfxDef d = detail::def(j, out.bank.defaults, "sounds." + id);
			d.id = id;
			out.bank.sounds.push_back(std::move(d));
		}
	}
	catch (const detail::BankError& e) { out.error = e.message; }
	catch (const nlohmann::json::exception& e) { out.error = std::string("sounds.json の項目の型か必須項目が違う: ") + e.what(); }
	if (!out.ok()) { out.bank = {}; }
	return out;
}

}  // namespace mitiru::audio::sfx
