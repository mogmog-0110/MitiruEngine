// src/core/Engine.cpp
//
// 3-F Phase 2 (pilot module split) — `MITIRU_HEADER_ONLY=OFF` STATIC ビルド時のみ
// この TU が `mitiru::Engine` の out-of-class 関数定義を 1 度だけ提供する。
// header-only モード (default `MITIRU_HEADER_ONLY=ON`) ではこのファイルは
// CMake のソースリストに含まれず、定義は Engine.hpp 経由で各 TU が inline で持つ。
//
// 関連: docs/HEADER_ONLY_MIGRATION.md, docs/ROADMAP_BIG_ROCKS.md §3-F

#include <mitiru/core/Engine.hpp>

// Engine.hpp は MITIRU_HEADER_ONLY=ON のときだけ detail headers を引く。
// STATIC モード時はここで明示的に展開して、out-of-class 定義を 1 つの TU に集める。
#if !defined(MITIRU_HEADER_ONLY)
#include <mitiru/core/detail/Engine_Accessors.hpp>
#include <mitiru/core/detail/Engine_Audio.hpp>
#include <mitiru/core/detail/Engine_Settings.hpp>
#include <mitiru/core/detail/Engine_Init_Font.hpp>
#include <mitiru/core/detail/Engine_Init_Input.hpp>
#include <mitiru/core/detail/Engine_Init_Lifecycle.hpp>
#include <mitiru/core/detail/Engine_Init_Pipeline.hpp>
#include <mitiru/core/detail/Engine_AutoTest.hpp>
#include <mitiru/core/detail/Engine_Http.hpp>
#include <mitiru/core/detail/Engine_Cef.hpp>
#endif
