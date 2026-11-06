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
#include <type_traits>
#include <utility>

namespace mitiru::module::detail
{

inline constexpr std::size_t kAutoReflectMaxFields = 64;  // 1 つの aggregate の直下メンバ数の上限 (探索の深さ)

/// @brief 任意の型へ変換できる番兵 (body 不要、`requires{}` の中でしか使わない)。
/// @details 参照へ変換する。値へ変換すると、コンストラクタを持つメンバ (sgc::Vec2f 等) の
///          コピーとムーブのどちらを選ぶかが曖昧になり、そのメンバで数えるのが止まってしまう。
struct UniversalType
{
	template<class U> constexpr operator U&() const noexcept;
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
	static constexpr std::size_t value = std::conditional_t<
		isAggregateConstructibleN<T, N>,
		std::integral_constant<std::size_t, N>,
		FieldCountImpl<T, N - 1>>::value;
};

template<class T>
struct FieldCountImpl<T, 0>
{
	static constexpr std::size_t value = 0;
};

/// `T{}` が通る型は、残りのメンバーを `{}` で初期化できるので、個数 N を増やすと aggregate init は
/// 「通る」から「通らない」へ 1 回だけ変わる。二分探索で FieldCountImpl と同じ答えが出る。
/// 分岐は三項演算子でなく conditional_t で選ぶ。三項演算子は左右の型を実体化するので、判定が全 N 個分走る。
template<class T, std::size_t Lo, std::size_t Hi>
struct FieldCountBisect
{
	static constexpr std::size_t mid = Lo + (Hi - Lo + 1) / 2;
	static constexpr std::size_t value = std::conditional_t<
		isAggregateConstructibleN<T, mid>,
		FieldCountBisect<T, mid, Hi>,
		FieldCountBisect<T, Lo, mid - 1>>::value;
};

template<class T, std::size_t N>
struct FieldCountBisect<T, N, N>
{
	static constexpr std::size_t value = N;
};

/// 参照メンバーや既定構築できないメンバーを持つ型は、少ない個数でも通らず、二分探索の前提が崩れる。
template<class T>
inline constexpr bool isBracedEmptyConstructible = requires { T{}; };

/// @brief T の全フィールド数 (aggregate init が通る最大個数)。上限 64 (超過分は 64 に飽和し、
///        呼び出し側の static_assert で検出される)。
template<class T>
inline constexpr std::size_t fieldCount = std::conditional_t<
	isBracedEmptyConstructible<T>,
	FieldCountBisect<T, 0, kAutoReflectMaxFields>,
	FieldCountImpl<T, kAutoReflectMaxFields>>::value;

}  // namespace mitiru::module::detail
