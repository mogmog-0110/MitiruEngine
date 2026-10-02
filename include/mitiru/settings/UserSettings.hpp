#pragma once

/// @file UserSettings.hpp
/// @brief 利用者の設定 (画面・音量・キー割り当て・アクセシビリティ・言語)。セーブとは別のファイルに置く。
/// @details 設定を GameMemory に置くと、ロードや巻き戻しで設定まで戻る。host が settings.json を持ち、
///          起動時と変更時にエンジンへ反映する。ファイルの読み書きと JSON の形は SettingsFile.hpp。

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <mitiru/input/ActionMap.hpp>
#include <mitiru/module/ModuleApi.hpp>
#include <mitiru/render/ColorVision.hpp>
#include <mitiru/render/QualityCaps.hpp>

namespace mitiru::settings
{

/// 排他フルスクリーンは持たない (DX12 では勧められず、Alt+Tab が遅い)。"fullscreen" は Borderless として読む。
enum class WindowMode : std::uint8_t { Windowed, Borderless };

struct GraphicsSettings
{
	int width = 1280;
	int height = 720;
	WindowMode windowMode = WindowMode::Windowed;
	bool vsync = true;
	int fpsLimit = 0;                          ///< 0 = 上限なし (vsync が先に効く)
	render::QualityPreset quality = render::QualityPreset::High;
	render::QualityCaps custom{};             ///< quality = Custom の時の上限

	[[nodiscard]] render::QualityCaps caps() const noexcept
	{
		return quality == render::QualityPreset::Custom ? custom : render::qualityCapsFor(quality);
	}
	[[nodiscard]] bool operator==(const GraphicsSettings&) const noexcept = default;
};

/// 添字は module::kSoundBus*。0 は全体。
inline constexpr std::array<const char*, module::kSoundBusCount> kBusNames = {
	"master", "music", "sfx", "voice", "ui", "ambient", "bus6", "bus7",
};

struct AudioSettings
{
	std::array<float, module::kSoundBusCount> bus{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

	[[nodiscard]] bool operator==(const AudioSettings&) const noexcept = default;
};

struct InputSettings
{
	input::BindingOverrides bindings;          ///< 初期値から変えた割り当てだけ
	float mouseSensitivity = 1.0f;
	bool invertMouseY = false;
	std::vector<std::string> toggleActions;    ///< 押し続ける代わりに押すたびに切り替える操作

	[[nodiscard]] bool operator==(const InputSettings&) const noexcept = default;
};

struct AccessibilitySettings
{
	float subtitleScale = 1.0f;                ///< 字幕の文字の大きさの倍率 (UI の data model へ渡す)
	float subtitleBackground = 0.5f;           ///< 字幕の下地の不透明度 0..1
	float uiScale = 1.0f;                      ///< UI (RmlUi) の全体の倍率
	float screenShake = 1.0f;                  ///< 画面揺れの倍率。0 で揺らさない
	bool cameraShake = true;                   ///< 3D のカメラを揺らすか
	float rumble = 1.0f;                       ///< パッドの振動の倍率
	render::ColorFilterSettings colorFilter{};

	[[nodiscard]] bool operator==(const AccessibilitySettings&) const noexcept = default;
};

struct UserSettings
{
	static constexpr int kVersion = 1;

	GraphicsSettings graphics;
	AudioSettings audio;
	InputSettings input;
	AccessibilitySettings accessibility;
	std::string language;                      ///< 空 = ゲームの既定の言語

	[[nodiscard]] bool operator==(const UserSettings&) const noexcept = default;
};

/// @brief 2 つの設定で何が変わったか (反映の範囲を絞るためのビット)。
enum SettingsChange : std::uint32_t
{
	kChangeNone          = 0,
	kChangeWindow        = 1u << 0,   ///< 窓の大きさ・表示の仕方
	kChangeVsync         = 1u << 1,
	kChangeQuality       = 1u << 2,
	kChangeAudio         = 1u << 3,
	kChangeInput         = 1u << 4,
	kChangeAccessibility = 1u << 5,
	kChangeLanguage      = 1u << 6,
};

[[nodiscard]] inline std::uint32_t diffSettings(const UserSettings& a, const UserSettings& b) noexcept
{
	std::uint32_t c = kChangeNone;
	const auto& ga = a.graphics;
	const auto& gb = b.graphics;
	if (ga.width != gb.width || ga.height != gb.height || ga.windowMode != gb.windowMode) { c |= kChangeWindow; }
	if (ga.vsync != gb.vsync || ga.fpsLimit != gb.fpsLimit) { c |= kChangeVsync; }
	if (ga.caps() != gb.caps() || ga.quality != gb.quality) { c |= kChangeQuality; }
	if (!(a.audio == b.audio)) { c |= kChangeAudio; }
	if (!(a.input == b.input)) { c |= kChangeInput; }
	if (!(a.accessibility == b.accessibility)) { c |= kChangeAccessibility; }
	if (a.language != b.language) { c |= kChangeLanguage; }
	return c;
}

}  // namespace mitiru::settings
