#pragma once

/// @file NumberAppend.hpp
/// @brief 数値を std::to_chars で文字列へ直接追記するユーティリティ
/// @details std::to_string は毎回一時 string を確保してからコピーする。JSON 文字列を
///          手組みする hot path ではその一時確保が無駄なので、出力バッファへ直接書く。

#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace mitiru::observe
{

/// @brief 非有限浮動小数点を表す JSON 文字列リテラル名 ("NaN"/"Inf"/"-Inf"、引用符なし)。
///        JSON の数値には NaN/Inf を表す構文が無いため (RFC 8259)、null に潰さず区別を
///        残すのに使う。`GET /api/ai/state` や `replay --diff` が「NaN なのか Inf なのか」を
///        読めるようにするための表現で、C++ の `std::isnan` / 符号判定と 1:1 対応する。
template <class T>
[[nodiscard]] inline const char* nonFiniteJsonName(T v) noexcept
{
	if (std::isnan(v)) { return "NaN"; }
	return (v > 0) ? "Inf" : "-Inf";
}

/// @brief `nonFiniteJsonName` の逆変換。"NaN"/"Inf"/"-Inf" 以外は false を返す。
[[nodiscard]] inline bool parseNonFiniteJsonName(std::string_view s, double& out) noexcept
{
	if (s == "NaN")  { out = std::numeric_limits<double>::quiet_NaN(); return true; }
	if (s == "Inf")  { out = std::numeric_limits<double>::infinity(); return true; }
	if (s == "-Inf") { out = -std::numeric_limits<double>::infinity(); return true; }
	return false;
}

/// @brief 数値 v を out の末尾へ追記する。非有限浮動小数点は `nonFiniteJsonName` を
///        引用符付きの JSON 文字列として書く (null に潰さない)。
template <class T>
inline void appendNumber(std::string& out, T v)
{
	if constexpr (std::is_floating_point_v<T>)
	{
		if (!std::isfinite(v)) { out += '"'; out += nonFiniteJsonName(v); out += '"'; return; }
	}
	char buf[48];
	const auto res = std::to_chars(buf, buf + sizeof(buf), v);
	out.append(buf, res.ptr);
}

}  // namespace mitiru::observe
