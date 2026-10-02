#pragma once

/// @file SettingsFile.hpp
/// @brief settings.json の読み書き。範囲外の値は丸め、読めない値は既定値に戻し、何をしたかを warnings で返す。
/// @details 書き込みは AtomicFile で差し替え、前の版を .bak に残す。本命が壊れていれば .bak から読む。
///          設定画面からの変更は "audio.music" のような点区切りの名前と値で受け (applySettingValue)、
///          JSON に戻してから読み直すので、ファイルから読む時と同じ検査を通る。

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/save/AtomicFile.hpp>
#include <mitiru/settings/UserSettings.hpp>

namespace mitiru::settings
{

struct SettingsLoad
{
	UserSettings settings;
	std::vector<std::string> warnings;
	bool fromFile = false;      ///< ファイル (か .bak) から読めた
	bool fromBackup = false;
};

namespace detail
{

using Json = nlohmann::json;

struct Reader
{
	std::vector<std::string>& warnings;

	template <typename T>
	void number(const Json& obj, const char* key, T& out, T lo, T hi, const std::string& where)
	{
		if (!obj.contains(key)) { return; }
		const Json& v = obj[key];
		if (!v.is_number()) { warnings.push_back(where + "." + key + " が数でない。既定値のまま"); return; }
		const T raw = v.get<T>();
		out = std::clamp(raw, lo, hi);
		if (out != raw) { warnings.push_back(where + "." + key + " を範囲 [" + std::to_string(lo) + ", " + std::to_string(hi) + "] に丸めた"); }
	}

	void boolean(const Json& obj, const char* key, bool& out, const std::string& where)
	{
		if (!obj.contains(key)) { return; }
		if (obj[key].is_boolean()) { out = obj[key].get<bool>(); }
		else { warnings.push_back(where + "." + key + " が true / false でない。既定値のまま"); }
	}

	[[nodiscard]] static const Json& section(const Json& root, const char* key)
	{
		static const Json empty = Json::object();
		return (root.contains(key) && root[key].is_object()) ? root[key] : empty;
	}
};

[[nodiscard]] inline render::QualityPreset qualityFromName(std::string_view n, bool& ok) noexcept
{
	ok = true;
	for (const auto p : { render::QualityPreset::Low, render::QualityPreset::Medium, render::QualityPreset::High,
	                      render::QualityPreset::Custom })
	{
		if (render::qualityPresetName(p) == n) { return p; }
	}
	ok = false;
	return render::QualityPreset::High;
}

inline void readGraphics(const Json& root, GraphicsSettings& g, Reader& r)
{
	const Json& j = Reader::section(root, "graphics");
	r.number(j, "width", g.width, 640, 16384, "graphics");
	r.number(j, "height", g.height, 360, 16384, "graphics");
	r.boolean(j, "vsync", g.vsync, "graphics");
	r.number(j, "fpsLimit", g.fpsLimit, 0, 1000, "graphics");
	if (j.contains("windowMode"))
	{
		const std::string m = j["windowMode"].is_string() ? j["windowMode"].get<std::string>() : std::string();
		if (m == "windowed") { g.windowMode = WindowMode::Windowed; }
		else if (m == "borderless" || m == "fullscreen") { g.windowMode = WindowMode::Borderless; }
		else { r.warnings.push_back("graphics.windowMode \"" + m + "\" は使えない (windowed / borderless)"); }
		if (m == "fullscreen") { r.warnings.push_back("graphics.windowMode: 排他フルスクリーンは無いので borderless で開く"); }
	}
	if (j.contains("quality"))
	{
		bool ok = false;
		g.quality = qualityFromName(j["quality"].is_string() ? j["quality"].get<std::string>() : std::string(), ok);
		if (!ok) { r.warnings.push_back("graphics.quality が分からない (low / medium / high / custom)。high にした"); }
	}
	const Json& c = Reader::section(j, "custom");
	r.boolean(c, "ambientOcclusion", g.custom.ambientOcclusion, "graphics.custom");
	r.boolean(c, "bloom", g.custom.bloom, "graphics.custom");
	r.boolean(c, "depthOfField", g.custom.depthOfField, "graphics.custom");
	r.number(c, "shadowCascades", g.custom.maxShadowCascades, 1, 3, "graphics.custom");
}

inline void readAudio(const Json& root, AudioSettings& a, Reader& r)
{
	const Json& j = Reader::section(root, "audio");
	for (std::size_t b = 0; b < kBusNames.size(); ++b) { r.number(j, kBusNames[b], a.bus[b], 0.0f, 1.0f, "audio"); }
}

inline void readBindings(const Json& j, input::BindingOverrides& out, Reader& r)
{
	if (!j.contains("bindings") || !j["bindings"].is_object()) { return; }
	for (const auto& [action, list] : j["bindings"].items())
	{
		std::vector<input::InputSource> sources;
		for (const auto& v : list.is_array() ? list : Json::array())
		{
			const std::string text = v.is_string() ? v.get<std::string>() : std::string();
			if (const auto s = input::parseInputSource(text)) { sources.push_back(*s); }
			else { r.warnings.push_back("input.bindings." + action + ": \"" + text + "\" は入力の名前でない。外した"); }
		}
		out[action] = std::move(sources);
	}
}

inline void readInput(const Json& root, InputSettings& in, Reader& r)
{
	const Json& j = Reader::section(root, "input");
	r.number(j, "mouseSensitivity", in.mouseSensitivity, 0.05f, 10.0f, "input");
	r.boolean(j, "invertMouseY", in.invertMouseY, "input");
	if (j.contains("toggle") && j["toggle"].is_array())
	{
		in.toggleActions.clear();
		for (const auto& v : j["toggle"]) { if (v.is_string()) { in.toggleActions.push_back(v.get<std::string>()); } }
	}
	readBindings(j, in.bindings, r);
}

inline void readColorFilter(const Json& a, render::ColorFilterSettings& f, Reader& r)
{
	const Json& j = Reader::section(a, "colorFilter");
	if (j.contains("mode"))
	{
		const std::string m = j["mode"].is_string() ? j["mode"].get<std::string>() : std::string();
		bool known = false;
		for (const auto mode : { render::ColorVisionMode::Off, render::ColorVisionMode::Protanopia,
		                         render::ColorVisionMode::Deuteranopia, render::ColorVisionMode::Tritanopia })
		{
			if (render::colorVisionModeName(mode) == m) { f.mode = mode; known = true; }
		}
		if (!known) { r.warnings.push_back("accessibility.colorFilter.mode \"" + m + "\" が分からない。off のまま"); }
	}
	if (j.contains("kind") && j["kind"].is_string())
	{
		f.kind = j["kind"].get<std::string>() == "simulate" ? render::ColorFilterKind::Simulate : render::ColorFilterKind::Correct;
	}
	r.number(j, "strength", f.strength, 0.0f, 1.0f, "accessibility.colorFilter");
}

inline void readAccessibility(const Json& root, AccessibilitySettings& a, Reader& r)
{
	const Json& j = Reader::section(root, "accessibility");
	r.number(j, "subtitleScale", a.subtitleScale, 0.5f, 3.0f, "accessibility");
	r.number(j, "subtitleBackground", a.subtitleBackground, 0.0f, 1.0f, "accessibility");
	r.number(j, "uiScale", a.uiScale, 0.5f, 2.5f, "accessibility");
	r.number(j, "screenShake", a.screenShake, 0.0f, 1.0f, "accessibility");
	r.boolean(j, "cameraShake", a.cameraShake, "accessibility");
	r.number(j, "rumble", a.rumble, 0.0f, 1.0f, "accessibility");
	readColorFilter(j, a.colorFilter, r);
}

}  // namespace detail

/// @brief JSON の文字列から読む。読めない所は defaults の値を使い、warnings に書く。
[[nodiscard]] inline SettingsLoad parseUserSettings(std::string_view json, const UserSettings& defaults)
{
	SettingsLoad out;
	out.settings = defaults;
	const auto root = nlohmann::json::parse(json, nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		out.warnings.push_back("settings.json が JSON として読めない。既定値を使う");
		return out;
	}
	detail::Reader r{ out.warnings };
	if (root.value("version", UserSettings::kVersion) > UserSettings::kVersion)
	{
		out.warnings.push_back("settings.json は新しい版のゲームが書いた。分かる項目だけ読む");
	}
	detail::readGraphics(root, out.settings.graphics, r);
	detail::readAudio(root, out.settings.audio, r);
	detail::readInput(root, out.settings.input, r);
	detail::readAccessibility(root, out.settings.accessibility, r);
	if (root.contains("language") && root["language"].is_string()) { out.settings.language = root["language"].get<std::string>(); }
	out.fromFile = true;
	return out;
}

namespace detail
{

[[nodiscard]] inline Json graphicsJson(const GraphicsSettings& g)
{
	return Json{
		{ "width", g.width }, { "height", g.height },
		{ "windowMode", g.windowMode == WindowMode::Borderless ? "borderless" : "windowed" },
		{ "vsync", g.vsync }, { "fpsLimit", g.fpsLimit },
		{ "quality", std::string(render::qualityPresetName(g.quality)) },
		{ "custom", Json{ { "ambientOcclusion", g.custom.ambientOcclusion }, { "bloom", g.custom.bloom },
		                  { "depthOfField", g.custom.depthOfField }, { "shadowCascades", g.custom.maxShadowCascades } } },
	};
}

[[nodiscard]] inline Json inputJson(const InputSettings& in)
{
	Json bindings = Json::object();
	for (const auto& [action, list] : in.bindings)
	{
		Json arr = Json::array();
		for (const auto& s : list) { arr.push_back(input::formatInputSource(s)); }
		bindings[action] = arr;
	}
	return Json{ { "mouseSensitivity", in.mouseSensitivity }, { "invertMouseY", in.invertMouseY },
	             { "toggle", in.toggleActions }, { "bindings", bindings } };
}

[[nodiscard]] inline Json accessibilityJson(const AccessibilitySettings& a)
{
	const auto& f = a.colorFilter;
	return Json{
		{ "subtitleScale", a.subtitleScale }, { "subtitleBackground", a.subtitleBackground }, { "uiScale", a.uiScale },
		{ "screenShake", a.screenShake }, { "cameraShake", a.cameraShake }, { "rumble", a.rumble },
		{ "colorFilter", Json{ { "mode", std::string(render::colorVisionModeName(f.mode)) },
		                       { "kind", f.kind == render::ColorFilterKind::Simulate ? "simulate" : "correct" },
		                       { "strength", f.strength } } },
	};
}

}  // namespace detail

[[nodiscard]] inline nlohmann::json userSettingsJson(const UserSettings& s)
{
	nlohmann::json audio = nlohmann::json::object();
	for (std::size_t b = 0; b < kBusNames.size(); ++b) { audio[kBusNames[b]] = s.audio.bus[b]; }
	return nlohmann::json{
		{ "version", UserSettings::kVersion }, { "language", s.language },
		{ "graphics", detail::graphicsJson(s.graphics) }, { "audio", audio },
		{ "input", detail::inputJson(s.input) }, { "accessibility", detail::accessibilityJson(s.accessibility) },
	};
}

/// @brief settings.json を読む。無ければ defaults (fromFile = false)。本命が壊れていれば .bak を読む。
[[nodiscard]] inline SettingsLoad loadUserSettings(const std::filesystem::path& path, const UserSettings& defaults)
{
	if (const auto bytes = save::readWholeFile(path))
	{
		SettingsLoad r = parseUserSettings(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), defaults);
		if (r.fromFile) { return r; }
		if (const auto bak = save::readWholeFile(save::withSuffix(path, ".bak")))
		{
			SettingsLoad b = parseUserSettings(std::string_view(reinterpret_cast<const char*>(bak->data()), bak->size()), defaults);
			if (b.fromFile)
			{
				b.fromBackup = true;
				b.warnings.insert(b.warnings.begin(), path.string() + " が読めないので 1 つ前の版 (.bak) を使う");
				return b;
			}
		}
		return r;
	}
	SettingsLoad none;
	none.settings = defaults;
	return none;
}

[[nodiscard]] inline bool saveUserSettings(const std::filesystem::path& path, const UserSettings& s, std::string* error = nullptr)
{
	const std::string text = userSettingsJson(s).dump(2) + "\n";
	const auto* p = reinterpret_cast<const std::uint8_t*>(text.data());
	return save::writeFileAtomic(path, std::span<const std::uint8_t>(p, text.size()), /*keepBackup*/ true, error);
}

/// @brief 点区切りの名前 ("audio.music"、"accessibility.colorFilter.mode") の値を差し替えた設定を返す。
///        名前が無い・型が違う時は nullopt と error。値の丸めは parseUserSettings と同じ。
[[nodiscard]] inline std::optional<UserSettings> applySettingValue(const UserSettings& s, std::string_view key,
                                                                   const nlohmann::json& value, std::string* error = nullptr)
{
	nlohmann::json j = userSettingsJson(s);
	constexpr std::string_view kBindings = "input.bindings.";
	if (key.rfind(kBindings, 0) == 0)
	{
		// 割り当ては変えた操作だけがファイルに載るので、まだ無い名前も受ける
		j["input"]["bindings"][std::string(key.substr(kBindings.size()))] = value;
		SettingsLoad r = parseUserSettings(j.dump(), s);
		if (error != nullptr && !r.warnings.empty()) { *error = r.warnings.front(); }
		return r.settings;
	}
	std::string pointer = "/";
	for (const char c : key) { pointer.push_back(c == '.' ? '/' : c); }
	const nlohmann::json::json_pointer ptr(pointer);
	const bool sameType = j.contains(ptr) && (j[ptr].type() == value.type() || (j[ptr].is_number() && value.is_number()));
	if (!sameType)
	{
		if (error != nullptr) { *error = "設定の名前か値の型が違う: " + std::string(key); }
		return std::nullopt;
	}
	j[ptr] = value;
	SettingsLoad r = parseUserSettings(j.dump(), s);
	if (error != nullptr && !r.warnings.empty()) { *error = r.warnings.front(); }
	return r.settings;
}

}  // namespace mitiru::settings
