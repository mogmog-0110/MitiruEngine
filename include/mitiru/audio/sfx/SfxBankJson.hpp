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

inline SfxCaption caption(const Json& j, const std::string& where)
{
	const std::string at = where + ".caption";
	if (!j.is_object()) { bankFail(at + " は { } のオブジェクト"); }
	rejectUnknownKeys(j, {"textKey", "kind", "directional", "speakerKey"}, at);
	SfxCaption c;
	c.textKey = j.value("textKey", std::string{});
	if (c.textKey.empty()) { bankFail(at + ": textKey (strings.json の鍵) が要る"); }
	c.speakerKey = j.value("speakerKey", std::string{});
	c.directional = j.value("directional", false);
	const std::string kind = j.value("kind", std::string("ambient"));
	const auto parsed = parseCaptionKind(kind);
	if (!parsed) { bankFail(at + ": kind は dialogue / ambient / music / alert"); }
	c.kind = *parsed;
	return c;
}

/// avoidRepeat と shuffle。defaults から受け継いだ値は、その音が自分で書いた方を優先して捨てる。変種の数との照合は、その音が書いたときだけ
inline void repeatAvoidance(const Json& j, SfxDef& d, const std::string& where)
{
	const bool wroteAvoid = j.contains("avoidRepeat");
	const bool wroteShuffle = j.contains("shuffle");
	if (wroteAvoid)
	{
		const int avoid = j.at("avoidRepeat").get<int>();
		if (avoid < 0 || avoid > 16) { bankFail(where + ": avoidRepeat は 0..16"); }
		d.avoidRepeat = static_cast<std::uint8_t>(avoid);
		if (!wroteShuffle && avoid > 0) { d.shuffle = false; }
	}
	if (wroteShuffle)
	{
		d.shuffle = j.at("shuffle").get<bool>();
		if (d.shuffle && !wroteAvoid) { d.avoidRepeat = 0; }
	}
	if (d.shuffle && d.avoidRepeat > 0) { bankFail(where + ": shuffle と avoidRepeat は同時に書けない (shuffle は 1 周するまで重ねない)"); }
	const std::size_t n = d.variants.size();
	if (wroteAvoid && n > 0 && d.avoidRepeat >= n)
	{
		bankFail(where + ": avoidRepeat は variants の数 - 1 以下 (variants は " + std::to_string(n) + " 個)");
	}
}

inline SfxDef def(const Json& j, SfxDef d, const std::string& where)
{
	rejectUnknownKeys(j, {"variants", "volumeDb", "pitchSemitones", "avoidRepeat", "shuffle", "attenuation", "priority", "maxInstances",
	                      "steal", "doppler", "occlusionDb", "occlusionLowpassHz", "caption"}, where);
	d.caption = j.contains("caption") ? caption(j.at("caption"), where) : SfxCaption{};
	if (j.contains("variants"))
	{
		d.variants.clear();
		for (const auto& v : j.at("variants")) { d.variants.push_back(v.get<std::string>()); }
	}
	d.volumeDb = range(j, "volumeDb", d.volumeDb, where);
	d.pitchSemitones = range(j, "pitchSemitones", d.pitchSemitones, where);
	repeatAvoidance(j, d, where);
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

inline void autoOcclusion(const Json& j, SfxBank& b)
{
	if (!j.is_object()) { bankFail("autoOcclusion は { } のオブジェクト"); }
	rejectUnknownKeys(j, {"enabled", "intervalSteps", "maxRaysPerStep", "smoothSec", "marginM"}, "autoOcclusion");
	AutoOcclusion& a = b.autoOcclusion;
	a.enabled = j.value("enabled", true);   // 書いたら使う (書かなければ使わない)
	a.intervalSteps = j.value("intervalSteps", a.intervalSteps);
	a.maxRaysPerStep = j.value("maxRaysPerStep", a.maxRaysPerStep);
	a.smoothSec = j.value("smoothSec", a.smoothSec);
	a.marginM = j.value("marginM", a.marginM);
	if (a.intervalSteps < 1 || a.intervalSteps > 600) { bankFail("autoOcclusion: intervalSteps は 1..600"); }
	if (a.maxRaysPerStep < 1 || a.maxRaysPerStep > 256) { bankFail("autoOcclusion: maxRaysPerStep は 1..256"); }
	if (!(a.smoothSec >= 0.0f && a.smoothSec <= 5.0f)) { bankFail("autoOcclusion: smoothSec は 0..5"); }
	if (!(a.marginM >= 0.0f && a.marginM <= 5.0f)) { bankFail("autoOcclusion: marginM は 0..5"); }
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
		detail::rejectUnknownKeys(root, {"seed", "voices", "autoOcclusion", "defaults", "sounds"}, "sounds.json");
		out.bank.seed = root.value("seed", 0u);
		if (root.contains("voices")) { detail::voices(root.at("voices"), out.bank); }
		if (root.contains("autoOcclusion")) { detail::autoOcclusion(root.at("autoOcclusion"), out.bank); }
		if (root.contains("defaults"))
		{
			if (root.at("defaults").contains("caption")) { detail::bankFail("defaults: caption は音ごとに書く (全部の音に同じ字幕は付けられない)"); }
			out.bank.defaults = detail::def(root.at("defaults"), SfxDef{}, "defaults");
		}
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
