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

/// コードから開ける「別窓のツール」。全ツール窓は汎用 CEF ホスト mitiru_tool_cef を
/// --page <name> で spawn して開く (ADR 0014)。ゲーム窓は汚さない。開く判断は host 側
/// コード (main.cpp) が持つ — pulled UI。
enum class Tool
{
	Inspector,      ///< 状態 inspector — 観察データ (hud.watch) を全部見る (--page inspect)
	InputMonitor,   ///< 入力モニタ — 入力の生値 (--page input)
	Rewind,         ///< 巻き戻し scrubber — 過去フレームへ戻す (--page rewind)
	SceneTree,      ///< シーンツリー — 観察データの階層構造を tree 表示 (--page scene)
	Replay,         ///< リプレイ scrubber — .mtrr 録画を frame 単位で観る (--page replay)
	Perf,           ///< パフォーマンス — fps / frameMs を観る (--page perf)
	AudioMixer,     ///< ミキサー — master volume + 再生中チャンネル VU (--page mixer)
	// ★ 独立ウィンドウを増やすとき: ここに enum 値を 1 つ足し、下の kToolTable に
	//   1 行 ({Tool::X, "tool_cef", "--page x"}) + assets/x.html を足すだけ。
};

namespace detail
{
/// Tool → spawn する exe 名 + 引数の対応表 (開ける窓の単一の真実)。
/// 全ツール窓は mitiru_tool_cef の HTML ページに統一 (assets/<name>.html)。HTML/CSS なので
/// 見た目が綺麗で、エンジンの UI 層 (軸①) を devtool 自身がドッグフードする。増やすときは 1 行。
struct ToolSpec { Tool tool; const char* exe; const char* args; };
inline constexpr ToolSpec kToolTable[] = {
	{ Tool::Inspector,    "tool_cef",  "--page inspect" },
	{ Tool::InputMonitor, "tool_cef",  "--page input" },
	{ Tool::Rewind,       "tool_cef",  "--page rewind" },
	{ Tool::SceneTree,    "tool_cef",  "--page scene" },
	{ Tool::Replay,       "tool_cef",  "--page replay" },
	{ Tool::Perf,         "tool_cef",  "--page perf" },
	{ Tool::AudioMixer,   "tool_cef",  "--page mixer" },
};
}  // namespace detail

}  // namespace mitiru
