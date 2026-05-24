// mitiru_static_placeholder.cpp
//
// このファイルは MITIRU_HEADER_ONLY=OFF (STATIC ビルドモード) 専用のプレースホルダー。
// CMake は STATIC ライブラリにソースファイルが 1 つも無いと一部のツールチェーンで
// エラーになるため、最低限 1 つの翻訳単位を提供する。
// Phase 2 以降でモジュールの .cpp を追加する際は、このファイルではなく
// src/<module>/<ModuleName>.cpp を新設し CMakeLists.txt に追記すること。
//
// 参照: docs/HEADER_ONLY_MIGRATION.md

namespace {
    // 意図的に空。ODR 違反の心配なし。
    void mitiru_static_placeholder_anchor() {}
} // namespace
