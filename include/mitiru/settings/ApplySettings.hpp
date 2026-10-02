#pragma once

/// @file ApplySettings.hpp
/// @brief 利用者の設定をエンジンへ反映する (host 専用。起動前は EngineConfig へ、動いている間は Engine へ)。
/// @details UI へは "view.settings_*" の名前で値を写す。ゲームの RML は字幕の大きさなどをここから読む
///          (例: data-style-font-size="settings_subtitle_scale * 24 + 'px'")。

#include <cstdint>
#include <string>

#include <mitiru/core/Engine.hpp>
#include <mitiru/settings/UserSettings.hpp>

namespace mitiru::settings
{

[[nodiscard]] constexpr DisplayMode displayModeOf(WindowMode m) noexcept
{
	return m == WindowMode::Borderless ? DisplayMode::BorderlessFullscreen : DisplayMode::Windowed;
}

inline void applyAccessibility(EngineConfig& cfg, const AccessibilitySettings& a) noexcept
{
	cfg.shakeScale = a.screenShake;
	cfg.cameraShake = a.cameraShake;
	cfg.rumbleScale = a.rumble;
	cfg.colorFilter = a.colorFilter;
}

/// @brief 窓を作る前に EngineConfig へ写す。
inline void applyStartupSettings(EngineConfig& cfg, const UserSettings& s)
{
	cfg.windowWidth = s.graphics.width;
	cfg.windowHeight = s.graphics.height;
	cfg.displayMode = displayModeOf(s.graphics.windowMode);
	cfg.vsync = s.graphics.vsync;
	cfg.targetFps = s.graphics.fpsLimit;
	cfg.qualityCaps = s.graphics.caps();
	applyAccessibility(cfg, s.accessibility);
	if (!s.language.empty()) { cfg.language = s.language; }
}

/// @brief UI の data model へ設定の値を写す (設定画面の表示と、字幕の大きさなどの反映)。
inline void pushSettingsToUi(ui_rml::RmlUiHost& ui, const UserSettings& s)
{
	ui.setFloat("view.settings_subtitle_scale", s.accessibility.subtitleScale);
	ui.setFloat("view.settings_subtitle_background", s.accessibility.subtitleBackground);
	ui.setFloat("view.settings_ui_scale", s.accessibility.uiScale);
	ui.setFloat("view.settings_screen_shake", s.accessibility.screenShake);
	ui.setBool("view.settings_camera_shake", s.accessibility.cameraShake);
	ui.setFloat("view.settings_rumble", s.accessibility.rumble);
	ui.setText("view.settings_color_filter", render::colorVisionModeName(s.accessibility.colorFilter.mode));
	for (std::size_t b = 0; b < kBusNames.size(); ++b)
	{
		ui.setFloat(std::string("view.settings_volume_") + kBusNames[b], s.audio.bus[b]);
	}
	ui.setText("view.settings_quality", render::qualityPresetName(s.graphics.quality));
	ui.setBool("view.settings_vsync", s.graphics.vsync);
	ui.setBool("view.settings_borderless", s.graphics.windowMode == WindowMode::Borderless);
	ui.setText("view.settings_language", s.language);
}

/// @brief 動いている間に変わった分 (changes は diffSettings の結果) をエンジンへ反映する。
inline void applyLiveSettings(Engine& engine, const UserSettings& s, std::uint32_t changes)
{
	if (changes & kChangeVsync)
	{
		engine.setVSync(s.graphics.vsync);
		engine.mutableConfig().targetFps = s.graphics.fpsLimit;
	}
	if (changes & kChangeWindow)
	{
		engine.setFullscreen(s.graphics.windowMode == WindowMode::Borderless);
		engine.resizeWindowClient(s.graphics.width, s.graphics.height);
	}
	if (changes & kChangeQuality) { engine.setQualityCaps(s.graphics.caps()); }
	if (changes & kChangeAudio) { engine.setUserBusVolumes(s.audio.bus); }
	if (changes & kChangeAccessibility)
	{
		applyAccessibility(engine.mutableConfig(), s.accessibility);
		engine.uiHost().setUiScale(s.accessibility.uiScale);
	}
	if (changes & kChangeLanguage) { engine.mutableConfig().language = s.language; }
	pushSettingsToUi(engine.uiHost(), s);
}

/// @brief 起動直後 (窓と UI ができた後) に、起動前に写せない分を反映する。
inline void applySettingsAfterStart(Engine& engine, const UserSettings& s)
{
	engine.setUserBusVolumes(s.audio.bus);
	engine.uiHost().setUiScale(s.accessibility.uiScale);
	pushSettingsToUi(engine.uiHost(), s);
}

}  // namespace mitiru::settings
