#pragma once

/// @file GameSettings.hpp
/// @brief 永続化される標準ゲーム設定 (display / vsync / volumes)
/// @details EngineConfig の一部フィールドを JSON で読み書きする helper。
///          %APPDATA%/<title>/settings.json (Windows) / ~/.config/<title>/settings.json (Linux/macOS)
///          に保存される。Engine::run() の冒頭で loadInto() が呼ばれ、
///          EngineConfig 既定値を上書きする。

#include <mitiru/core/Config.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h>
#  include <ShlObj.h>
#endif

namespace mitiru
{

/// @brief ユーザー設定の永続化 helper
class GameSettings
{
public:
	/// @brief 設定ファイルのフルパスを取得する
	/// @param appName アプリ名 (`EngineConfig::title` を想定)
	/// @param fileName ファイル名 (デフォルト "settings.json")
	[[nodiscard]] static std::filesystem::path resolvePath(
		const std::string& appName,
		const std::string& fileName = "settings.json")
	{
#ifdef _WIN32
		PWSTR appData = nullptr;
		std::filesystem::path base;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
		{
			base = appData;
			CoTaskMemFree(appData);
		}
		else
		{
			base = std::filesystem::current_path();
		}
#else
		const char* xdg = std::getenv("XDG_CONFIG_HOME");
		const char* home = std::getenv("HOME");
		std::filesystem::path base;
		if (xdg && *xdg) { base = xdg; }
		else if (home && *home) { base = std::filesystem::path(home) / ".config"; }
		else { base = std::filesystem::current_path(); }
#endif
		return base / sanitize(appName) / fileName;
	}

	/// @brief 既存ファイルから読み込んで EngineConfig を上書きする
	/// @return 読み込み成功なら true (ファイル無し/破損は false)
	static bool loadInto(EngineConfig& config)
	{
		const auto path = resolvePath(config.title, config.settingsFileName);
		std::ifstream f(path);
		if (!f.is_open()) { return false; }

		nlohmann::json j;
		try { f >> j; }
		catch (...) { return false; }

		/// display
		if (j.contains("display"))
		{
			const auto& d = j["display"];
			if (d.contains("width"))  { config.windowWidth  = d["width"].get<int>(); }
			if (d.contains("height")) { config.windowHeight = d["height"].get<int>(); }
			if (d.contains("mode"))
			{
				const auto m = d["mode"].get<std::string>();
				if (m == "borderless") { config.displayMode = DisplayMode::BorderlessFullscreen; }
				else                   { config.displayMode = DisplayMode::Windowed; }
			}
			if (d.contains("vsync"))     { config.vsync     = d["vsync"].get<bool>(); }
			if (d.contains("targetFps")) { config.targetFps = d["targetFps"].get<int>(); }
		}

		/// audio
		if (j.contains("audio"))
		{
			const auto& a = j["audio"];
			if (a.contains("master")) { config.masterVolume = a["master"].get<float>(); }
			if (a.contains("bgm"))    { config.bgmVolume    = a["bgm"].get<float>(); }
			if (a.contains("se"))     { config.seVolume     = a["se"].get<float>(); }
			if (a.contains("voice"))  { config.voiceVolume  = a["voice"].get<float>(); }
		}

		/// language
		if (j.contains("language") && j["language"].is_string())
		{
			config.language = j["language"].get<std::string>();
		}

		/// keyBindings
		if (j.contains("keyBindings") && j["keyBindings"].is_object())
		{
			for (auto it = j["keyBindings"].begin(); it != j["keyBindings"].end(); ++it)
			{
				if (it.value().is_number_integer())
				{
					config.keyBindings[it.key()] = it.value().get<int>();
				}
			}
		}

		return true;
	}

	/// @brief EngineConfig の現在値をファイルに保存する
	/// @return 書き込み成功なら true
	static bool saveFrom(const EngineConfig& config)
	{
		const auto path = resolvePath(config.title, config.settingsFileName);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		nlohmann::json j;
		j["display"] = {
			{"width",     config.windowWidth},
			{"height",    config.windowHeight},
			{"mode",      config.displayMode == DisplayMode::BorderlessFullscreen
			                ? "borderless" : "windowed"},
			{"vsync",     config.vsync},
			{"targetFps", config.targetFps},
		};
		j["audio"] = {
			{"master", config.masterVolume},
			{"bgm",    config.bgmVolume},
			{"se",     config.seVolume},
			{"voice",  config.voiceVolume},
		};
		j["language"] = config.language;
		nlohmann::json kb = nlohmann::json::object();
		for (const auto& [action, key] : config.keyBindings)
		{
			kb[action] = key;
		}
		j["keyBindings"] = kb;

		std::ofstream f(path);
		if (!f.is_open()) { return false; }
		f << j.dump(2);
		return f.good();
	}

private:
	/// ファイルシステムで安全な名前に変換する (空白→アンダースコア、特殊文字除去)
	[[nodiscard]] static std::string sanitize(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		for (char c : s)
		{
			if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
			{
				out += c;
			}
			else if (c == ' ')
			{
				out += '_';
			}
		}
		if (out.empty()) { out = "MitiruGame"; }
		return out;
	}
};

} // namespace mitiru
