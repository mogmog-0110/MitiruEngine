// mitiru::Engine の detail header。直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module.hpp
/// @brief Engine の module loader 部分の集約 header (v0.2.0 step 2-3)
/// @details
/// 実装は意味のまとまりで次の 4 つに分割されている (800 行ルール)。
///   - Engine_Module_Loader.hpp。loadModule / unloadModule / reloadModule /
///     accessor 群 / rewind 用 GameMemory ring
///   - Engine_Module_Adapter.hpp。runModule (ModuleAdapter) と、host と game を
///     C の関数と生データだけで繋ぐ per-frame signal flow。中身は次のとおり。
///       - InputSnapshot 構築 (host が input + action events を POD に詰める)
///       - FrameIntents drain (DLL の要求を host が解釈して engine 操作に変換)
///       - 必要なら StateStore + SharedSnapshot を遅延生成
///   - Engine_Module_Fault.hpp。game が callback の中で落ちた時の停止と報告
///   - Engine_Module_SideState.hpp。GameMemory の外に持つ状態 (窓口) の記録と復元、画面なしの 1 フレーム
///   - Engine_Module_Boundary.hpp。ABI v48 で開いた入口 (パッドの拡張・スロット・設定・曲・カメラの切り替え)
///   - Engine_Module_Boundary50.hpp。ABI v50 で開いた入口 (先読み・人ごとの操作・今の機器・カットシーンの印・story の範囲)

#include <mitiru/core/detail/Engine_Module_Loader.hpp>
#include <mitiru/core/detail/Engine_Module_Adapter.hpp>
#include <mitiru/core/detail/Engine_Module_Fault.hpp>
#include <mitiru/core/detail/Engine_Module_SideState.hpp>
#include <mitiru/core/detail/Engine_Module_Boundary.hpp>
#include <mitiru/core/detail/Engine_Module_Boundary50.hpp>
