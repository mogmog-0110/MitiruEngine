#pragma once

/// @file ToolRegistry.hpp
/// @brief 開ける独立ウィンドウ (inspector 等) を定義する唯一の場所。Tool enum + spawn 表。
/// @details
/// game 側 (`Game.hpp` の `hud.open`) と host 側 (`InspectorLauncher` の `openTool`)
/// が同じ表を共有する。プラットフォーム依存のヘッダを一切 include しない (game DLL で使っても安全)。
///
/// アトミックツール哲学: ツール窓は「host を書く人が使うと決めた物」だけを開く。
/// 独立ウィンドウを増やすときは、enum に値を 1 つと kToolTable に行を 1 つ足すだけ。

namespace mitiru
{

/// コードから開ける「別窓のツール」。全ツール窓は汎用ホスト mitiru_tool (RmlUi) を
/// --page <name> で spawn して開く。ゲーム窓は汚さない。開く判断は host 側
/// コード (main.cpp) が持つ。pulled UI。
enum class Tool
{
	Inspector,      ///< 状態 inspector — 観察データ (hud.watch) を全部見る (--page inspect)
	InputMonitor,   ///< 入力モニタ — 入力の生値 (--page input)
	Rewind,         ///< 巻き戻し scrubber — 過去フレームへ戻す (--page rewind)
	SceneTree,      ///< シーンツリー — 観察データの階層構造を tree 表示 (--page scene)
	Replay,         ///< リプレイ scrubber — .mtrr 録画を frame 単位で観る (--page replay)
	Perf,           ///< パフォーマンス — fps / frameMs を観る (--page perf)
	AudioMixer,     ///< ミキサー — master volume + 再生中チャンネル VU (--page mixer)
	SceneView,      ///< シーンビュー — ゲーム画面 + オブジェクト枠、ドラッグで分岐 (--page scene_view、ADR 0035 O2/O3)
	WhyView,        ///< なぜビュー — field を選んで blame + 値推移を見る (--page why_view、ADR 0035 O6)
	FrameView,      ///< 1 フレームの解剖図 — 入力→書込→描画→音を 1 画面 (--page frame_view、P10)
	SideState,      ///< GameMemory の外の状態 — 窓口ごとの bytes / hash / リング、replay の食い違い (--page side_state、ADR 0054)
	Ai,             ///< 敵の AI — 選んだ敵のビヘイビアツリーの状態、知覚、攻撃トークン (--page ai)
	Nav,            ///< 群衆とナビメッシュ — 上から見た地図に agent と障害物 (--page nav)
	Anim,           ///< アニメの姿勢 — 選んだモデルのレイヤ、このフレームのイベント、ルートモーション (--page anim)
	// ★ 独立ウィンドウを増やすとき: ここに enum 値を 1 つ足し、下の kToolTable に
	//   1 行 ({Tool::X, "tool", "--page x"}) + apps/mitiru_tool/assets/x.rml を足し、
	//   ページの振る舞い (apps/mitiru_tool/ui/pages/) を 1 つ書く。
};

namespace detail
{
/// Tool → spawn する exe 名 + 引数の対応表 (開ける窓の単一の真実)。
/// 全ツール窓は mitiru_tool の RML ページ (assets/<name>.rml)。エンジンの UI 層 (軸①) を
/// devtool 自身がドッグフードする。増やすときは 1 行。
struct ToolSpec { Tool tool; const char* exe; const char* args; };
inline constexpr ToolSpec kToolTable[] = {
	{ Tool::Inspector,    "tool",  "--page inspect" },
	{ Tool::InputMonitor, "tool",  "--page input" },
	{ Tool::Rewind,       "tool",  "--page rewind" },
	{ Tool::SceneTree,    "tool",  "--page scene" },
	{ Tool::Replay,       "tool",  "--page replay" },
	{ Tool::Perf,         "tool",  "--page perf" },
	{ Tool::AudioMixer,   "tool",  "--page mixer" },
	{ Tool::SceneView,    "tool",  "--page scene_view" },
	{ Tool::WhyView,      "tool",  "--page why_view" },
	{ Tool::FrameView,    "tool",  "--page frame_view" },
	{ Tool::SideState,    "tool",  "--page side_state" },
	{ Tool::Ai,           "tool",  "--page ai" },
	{ Tool::Nav,          "tool",  "--page nav" },
	{ Tool::Anim,         "tool",  "--page anim" },
};
}  // namespace detail

}  // namespace mitiru
