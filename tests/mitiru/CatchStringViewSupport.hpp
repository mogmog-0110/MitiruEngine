#pragma once

/// @file CatchStringViewSupport.hpp
/// @brief Catch2 v3のstd::string_viewサポート
/// @details Catch2ライブラリがstring_view StringMakerの定義を含まない場合の補完。
///          コンパイラがC++17を検出しCATCH_CONFIG_CPP17_STRING_VIEWを定義するが、
///          Catch2ライブラリ側でその定義がコンパイルされていないケースに対応する。

#include <catch2/catch_tostring.hpp>
#include <string>
#include <string_view>

#ifdef CATCH_CONFIG_CPP17_STRING_VIEW
/// @brief StringMaker<std::string_view>::convertのインライン定義
/// @details Catch2ヘッダーで宣言済みだがライブラリに実体がない場合に補完する。
///          複数TUからインクルード可能なようinline指定する。
inline std::string Catch::StringMaker<std::string_view>::convert(std::string_view sv)
{
	return std::string(sv);
}
#endif
