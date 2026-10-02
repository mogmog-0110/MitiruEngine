#pragma once

/// @file ScenarioScript.hpp
/// @brief VN シナリオスクリプト言語パーサー・エグゼキューター
///
/// @note `mitiru::vn::ScenarioScript` は **テキスト DSL のパーサ** で、
///       `@scene` / `@bg` / `@char` / `@if` / dialogue などを含む `.scenario`
///       ファイルから `ScenarioNode` 列を作り、`ScenarioExecutor` で実行する。
///       C++ コードの中でコマンド列をプログラムで組み立てたい場合は
///       旧 game::VNScript (game/ モジュールは 2026-06-12 に attic/p3-module-cuts へ退避)
///       を使うこと。両者は別系統で、テキスト DSL を主に使うなら本ファイル、
///       procedural にコマンドを積むなら VNGameHybrid。
/// @details テキストベースの DSL をトークン化・パースし、1 ステップずつ実行する。
///          描画やオーディオなどのエンジン側の実装とは、
///          コールバックインターフェースを通して疎結合につなぐ。
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
/// @note 実装は役割ごとに detail/ 配下へ分けてある。本ファイルは
///       umbrella として 4 つの detail ヘッダを束ねるだけの薄いヘッダ。

#include "detail/ScenarioScript_Types.hpp"
#include "detail/ScenarioScript_Lexer.hpp"
#include "detail/ScenarioScript_Parser.hpp"
#include "detail/ScenarioScript_Inline.hpp"
#include "detail/ScenarioScript_Callback.hpp"
#include "detail/ScenarioScript_Executor.hpp"
// _FlagManagerGlue.hpp は必ず最後に置く。後回しにした FlagManager.hpp の include を
// ここで行い、ScenarioExecutor <-> FlagManager の循環依存を切る。
#include "detail/ScenarioScript_FlagManagerGlue.hpp"
