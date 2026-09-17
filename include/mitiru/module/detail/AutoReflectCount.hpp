#pragma once

/// @file AutoReflectCount.hpp
/// @brief aggregate T のフィールド数を `T{ {Any{}}, {Any{}}, ... }` の aggregate init が
///        何個まで通るかで検出する (Boost.PFR と同じ発想)。各引数を `{}` で 1 段包むのが
///        要点: 包まないと配列メンバ / ネスト aggregate メンバが brace elision で複数個の
///        引数を消費してしまい (`float arr[4]` が 4 引数分を飲み込む等)、トップレベルの
///        フィールド数を過大に数えてしまう。1 個ずつ `{}` で囲めば、各引数はそのメンバの
///        「先頭要素だけを埋めて残りは既定初期化」に閉じ込められるので 1 引数 = 1 メンバに
///        固定できる。

#include <cstddef>
#include <utility>

namespace mitiru::module::detail
{

inline constexpr std::size_t kAutoReflectMaxFields = 64;  // ModuleApi::reflectFields の容量と一致

/// @brief 任意の型へ変換できる番兵 (body 不要、`requires{}` の中でしか使わない)。
struct UniversalType
{
	template<class U> constexpr operator U() const noexcept;
};

template<class T, std::size_t... I>
constexpr bool isAggregateConstructibleWith(std::index_sequence<I...>) noexcept
{
	return requires { T{ { ((void)I, UniversalType{}) }... }; };
}

template<class T, std::size_t N>
inline constexpr bool isAggregateConstructibleN =
	isAggregateConstructibleWith<T>(std::make_index_sequence<N>{});

template<class T, std::size_t N>
struct FieldCountImpl
{
	static constexpr std::size_t value =
		isAggregateConstructibleN<T, N> ? N : FieldCountImpl<T, N - 1>::value;
};

template<class T>
struct FieldCountImpl<T, 0>
{
	static constexpr std::size_t value = 0;
};

/// @brief T の全フィールド数 (aggregate init が通る最大個数)。上限 64 (超過分は 64 に飽和し、
///        呼び出し側の static_assert で検出される)。
template<class T>
inline constexpr std::size_t fieldCount = FieldCountImpl<T, kAutoReflectMaxFields>::value;

}  // namespace mitiru::module::detail
