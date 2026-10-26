#pragma once

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <mitiru/debug/DebugMenuModel.hpp>

namespace mitiru::debug
{

inline constexpr int kBuiltinShowInfo = 1;
inline constexpr int kBuiltinPauseWhileOpen = 2;
inline constexpr int kBuiltinStep = 3;
inline constexpr int kBuiltinSpeed = 4;
inline constexpr int kBuiltinLofi = 5;
inline constexpr int kBuiltinScreenshot = 6;
inline constexpr int kBuiltinBugRecord = 7;
inline constexpr int kBuiltinOpenLog = 8;
inline constexpr int kBuiltinOpenSave = 9;
inline constexpr int kBuiltinAudioOutput = 10;

inline constexpr int kBuiltinToolInspector = 100;
inline constexpr int kBuiltinToolPerformance = 101;
inline constexpr int kBuiltinToolRewind = 102;
inline constexpr int kBuiltinToolInput = 103;
inline constexpr int kBuiltinToolMixer = 104;
inline constexpr int kBuiltinToolSceneTree = 105;

inline constexpr std::array<float, 5> kBuiltinSpeeds = {
	0.25f,
	0.5f,
	1.0f,
	2.0f,
	4.0f,
};

struct DebugMenuBuiltinState
{
	bool showInfo = false;
	bool pauseWhileOpen = true;
	bool paused = false;
	float timeScale = 1.0f;
	bool lofi = false;
	bool toolAvailable = false;
	std::vector<std::string> audioOutputs;   ///< 空なら「Audio output」の行を出さない (host が機器を選べないとき)
	int audioOutputSelected = 0;
};

[[nodiscard]] inline int nearestBuiltinSpeed(float scale) noexcept
{
	int nearest = 0;
	float nearestDistance = std::abs(scale - kBuiltinSpeeds[0]);
	for (int i = 1; i < static_cast<int>(kBuiltinSpeeds.size()); ++i)
	{
		const float distance = std::abs(scale - kBuiltinSpeeds[i]);
		if (distance < nearestDistance)
		{
			nearest = i;
			nearestDistance = distance;
		}
	}
	return nearest;
}

[[nodiscard]] inline MenuRow builtinRow(
	std::uint8_t kind,
	int id,
	const char* label,
	float value = 0.0f,
	const char* text = "",
	bool disabled = false)
{
	MenuRow row;
	row.kind = kind;
	row.flags = disabled ? module::kDebugItemDisabled : 0;
	row.builtin = true;
	row.id = id;
	row.label = label;
	row.text = text;
	row.value = value;
	return row;
}

inline void appendDebugMenuBuiltins(
	std::vector<MenuRow>& rows,
	const DebugMenuBuiltinState& state)
{
	rows.push_back(builtinRow(module::kDebugMenuSection, 0, "Engine"));
	rows.push_back(builtinRow(
		module::kDebugMenuToggle, kBuiltinShowInfo, "Show FPS and info",
		state.showInfo ? 1.0f : 0.0f));
	rows.push_back(builtinRow(
		module::kDebugMenuToggle, kBuiltinPauseWhileOpen, "Pause while open",
		state.pauseWhileOpen ? 1.0f : 0.0f));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinStep, "Step one frame",
		0.0f, "", !state.paused));
	rows.push_back(builtinRow(
		module::kDebugMenuChoice, kBuiltinSpeed, "Speed",
		static_cast<float>(nearestBuiltinSpeed(state.timeScale)),
		"0.25x|0.5x|1x|2x|4x"));
	rows.push_back(builtinRow(
		module::kDebugMenuToggle, kBuiltinLofi, "Lo-fi look",
		state.lofi ? 1.0f : 0.0f));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinScreenshot, "Save screenshot"));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinBugRecord, "Save bug record"));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinOpenLog, "Open log folder"));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinOpenSave, "Open save folder"));
	if (!state.audioOutputs.empty())
	{
		std::string options;
		for (const std::string& name : state.audioOutputs)
		{
			if (!options.empty()) { options += '|'; }
			options += name;
		}
		rows.push_back(builtinRow(
			module::kDebugMenuChoice, kBuiltinAudioOutput, "Audio output",
			static_cast<float>(state.audioOutputSelected), options.c_str()));
	}

	rows.push_back(builtinRow(module::kDebugMenuSection, 0, "Tools"));
	const bool disabled = !state.toolAvailable;
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinToolInspector, "Inspector",
		0.0f, "", disabled));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinToolPerformance, "Performance",
		0.0f, "", disabled));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinToolRewind, "Rewind",
		0.0f, "", disabled));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinToolInput, "Input monitor",
		0.0f, "", disabled));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinToolMixer, "Audio mixer",
		0.0f, "", disabled));
	rows.push_back(builtinRow(
		module::kDebugMenuButton, kBuiltinToolSceneTree, "Scene tree",
		0.0f, "", disabled));
}

}  // namespace mitiru::debug
