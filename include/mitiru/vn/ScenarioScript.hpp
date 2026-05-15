#pragma once

/// @file ScenarioScript.hpp
/// @brief VNシナリオスクリプト言語パーサー・エグゼキューター
///
/// @note `mitiru::vn::ScenarioScript` は **テキスト DSL のパーサ** であり、
///       `@scene` / `@bg` / `@char` / `@if` / dialogue などを含む `.scenario`
///       ファイルから `ScenarioNode` 列を生成し `ScenarioExecutor` で実行する。
///       C++ コード中でプログラム的にコマンド列を組み立てたい場合は
///       `mitiru::game::VNScript` (`include/mitiru/game/VNGameHybrid.hpp`)
///       を使うこと。両者は別系統。テキスト DSL 優先 = 本ファイル、
///       proceduralコマンド積み = VNGameHybrid。
/// @details テキストベースのDSLをトークン化・パース・ステップ実行する。
///          コールバックインターフェースにより、描画やオーディオなどの
///          エンジン側実装と疎結合に連携する。
///
/// スクリプト書式例:
/// @code
/// @scene "chapter1"
/// @bg "classroom.png" fade 1.0
/// @bgm "morning.ogg"
/// @char "sakura" center show fade
/// sakura "おはようございます！"
/// @choice
///   "そうだね" -> agree
///   "別に" -> disagree
/// @label agree
/// @set flag_agreed true
/// sakura "でしょう！"
/// @jump common
/// @label disagree
/// sakura "そう…"
/// @label common
/// @bg "hallway.png" dissolve 0.5
/// @endcode
///
/// @note 実装はロール別に detail/ 配下へ分割されている。本ファイルは
///       umbrella として 4 つの detail ヘッダを束ねるだけのスリムヘッダ。

#include "detail/ScenarioScript_Types.hpp"
#include "detail/ScenarioScript_Lexer.hpp"
#include "detail/ScenarioScript_Parser.hpp"
#include "detail/ScenarioScript_Inline.hpp"
#include "detail/ScenarioScript_Callback.hpp"
#include "detail/ScenarioScript_Executor.hpp"
// _FlagManagerGlue.hpp MUST be last: it triggers the deferred FlagManager.hpp
// include that breaks the ScenarioExecutor <-> FlagManager cyclic dependency.
#include "detail/ScenarioScript_FlagManagerGlue.hpp"
