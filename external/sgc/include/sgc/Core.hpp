#pragma once

/// @file Core.hpp
/// @brief ShiggyGameCoreライブラリのバージョン情報とC++20コンパイラ検証

#include <cstdint>

namespace sgc
{

/// @brief ライブラリのバージョン情報を保持する構造体
///
/// セマンティックバージョニング（major.minor.patch）に準拠する。
///
/// @code
/// constexpr auto ver = sgc::LIBRARY_VERSION;
/// // ver.major == 0, ver.minor == 1, ver.patch == 0
/// @endcode
struct Version
{
	std::uint32_t major;  ///< メジャーバージョン（破壊的変更時にインクリメント）
	std::uint32_t minor;  ///< マイナーバージョン（後方互換な機能追加時にインクリメント）
	std::uint32_t patch;  ///< パッチバージョン（バグ修正時にインクリメント）
};

/// @brief 現在のライブラリバージョン
inline constexpr Version LIBRARY_VERSION{0, 1, 0};

/// @brief C++20以上を要求するコンパイル時チェック
/// @note _MSVC_LANGはMSVC専用マクロのため#ifdefで囲む（M-001参照）
#ifdef _MSVC_LANG
static_assert(_MSVC_LANG >= 202002L, "ShiggyGameCore requires C++20 or later");
#else
static_assert(__cplusplus >= 202002L, "ShiggyGameCore requires C++20 or later");
#endif

} // namespace sgc
