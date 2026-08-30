// mitiru::Engine の detail header - 直接 include しないこと。core/Engine.hpp 経由で include される
#pragma once

/// @file Engine_Module.hpp
/// @brief Engine の module loader 部分の集約 header (v0.2.0 step 2-3)
/// @details
/// 実装は意味のまとまりで 2 分割されている (800 行ルール):
///   - Engine_Module_Loader.hpp。loadModule / unloadModule / reloadModule /
///     accessor 群 / time-travel GameMemory ring
///   - Engine_Module_Adapter.hpp。runModule (ModuleAdapter) と、host と game を
///     C の関数と生データだけで繋ぐ per-frame signal flow:
///       - InputSnapshot 構築 (host が input + action events を POD に詰める)
///       - FrameIntents drain (DLL の要求を host が解釈して engine 操作に変換)
///       - 必要なら StateStore + SharedSnapshot を遅延生成

#include <mitiru/core/detail/Engine_Module_Loader.hpp>
#include <mitiru/core/detail/Engine_Module_Adapter.hpp>
