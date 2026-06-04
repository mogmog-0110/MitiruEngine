#pragma once

/// @file ToolRegistry.hpp
/// @brief 開ける独立ウィンドウ (inspector 等) の単一の真実 — Tool enum + spawn 表。
/// @details
/// game 側 (`Game.hpp` の `hud.open`) と host 側 (`InspectorLauncher` の `openTool`)
/// が同じ表を共有する。プラットフォーム依存ヘッダを一切引かない (game DLL に安全)。
///
/// アトミックツール哲学: ツール窓は「host を書く人が使うと決めた物」だけ開く。
/// 独立ウィンドウを増やすとき → enum に 1 値 + kToolTable に 1 行を足すだけ。

namespace mitiru
{

/// コードから開ける「別窓のツール」(host が mitiru_<exe>.exe を別窓 spawn する、ADR 0014)。
/// ゲーム窓は汚さない。開く判断は host 側コード (main.cpp) が持つ — pulled UI。
enum class Tool
{
	Inspector,      ///< 状態 inspector — 観察データ (hud.watch) を全部見る
	InputMonitor,   ///< 入力モニタ — inspector の "input" 観察にフォーカス
	TimeTravel,     ///< タイムトラベル scrubber — inspector の "timetravel" にフォーカス
	SceneTree,      ///< シーンツリー — 観察データの階層構造を tree 表示 (別 exe)
	Replay,         ///< リプレイ scrubber — .mtrr 録画を frame 単位で観る (file 駆動、別 exe)
	Perf,           ///< パフォーマンス — host 併記の fps / frameMs を観る (別 exe)
	AudioMixer,     ///< ミキサー — host 併記の master volume を観る (最小版、別 exe)
	// ★ 独立ウィンドウを増やすとき: ここに enum 値を 1 つ足し、下の kToolTable に
	//   対応する 1 行 ({Tool::X, "<exe>", "<args>"}) を足すだけ。
};

namespace detail
{
/// Tool → spawn する exe 名 + 引数の対応表 (開ける窓の単一の真実)。
/// 増やすときはここに 1 行。今は inspector exe が panel 切替で 3 役を兼ねている。
struct ToolSpec { Tool tool; const char* exe; const char* args; };
inline constexpr ToolSpec kToolTable[] = {
	{ Tool::Inspector,    "inspector",  "" },
	{ Tool::InputMonitor, "inspector",  "--inspectable input" },
	{ Tool::TimeTravel,   "inspector",  "--inspectable timetravel" },
	{ Tool::SceneTree,    "scene_tree", "" },
	{ Tool::Replay,       "replay",     "" },
	{ Tool::Perf,         "perf",       "" },
	{ Tool::AudioMixer,   "mixer",      "" },
};
}  // namespace detail

}  // namespace mitiru
