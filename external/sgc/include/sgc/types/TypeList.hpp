#pragma once

/// @file TypeList.hpp
/// @brief コンパイル時型リスト操作メタプログラミングユーティリティ
///
/// テンプレートメタプログラミングで型のリストを操作する。
/// ECS、シリアライゼーション、バリアント操作等で活用する。

#include <cstddef>
#include <type_traits>

namespace sgc
{

/// @brief 型リスト — 型のパックをまとめて保持する
/// @tparam Ts 型パック
template <typename... Ts>
struct TypeList
{
};

/// @brief 型リストの要素数を返す
template <typename List>
struct TypeListSize;

template <typename... Ts>
struct TypeListSize<TypeList<Ts...>>
	: std::integral_constant<std::size_t, sizeof...(Ts)>
{
};

/// @brief 型リストの要素数（変数テンプレート版）
template <typename List>
inline constexpr std::size_t TYPE_LIST_SIZE = TypeListSize<List>::value;

/// @brief 型リストのN番目の型を取得する
template <std::size_t N, typename List>
struct TypeListAt;

template <std::size_t N, typename Head, typename... Tail>
struct TypeListAt<N, TypeList<Head, Tail...>>
	: TypeListAt<N - 1, TypeList<Tail...>>
{
};

template <typename Head, typename... Tail>
struct TypeListAt<0, TypeList<Head, Tail...>>
{
	using Type = Head;
};

/// @brief 型リストのN番目の型（エイリアス版）
template <std::size_t N, typename List>
using TypeListAtT = typename TypeListAt<N, List>::Type;

/// @brief 型リストに型が含まれているか判定する
template <typename T, typename List>
struct TypeListContains;

template <typename T, typename... Ts>
struct TypeListContains<T, TypeList<Ts...>>
	: std::bool_constant<(std::is_same_v<T, Ts> || ...)>
{
};

/// @brief 型リストに型が含まれているか（変数テンプレート版）
template <typename T, typename List>
inline constexpr bool TYPE_LIST_CONTAINS = TypeListContains<T, List>::value;

/// @brief 型リスト内での型のインデックスを返す
template <typename T, typename List>
struct TypeListIndex;

template <typename T, typename Head, typename... Tail>
struct TypeListIndex<T, TypeList<Head, Tail...>>
	: std::integral_constant<std::size_t,
		std::is_same_v<T, Head> ? 0 : 1 + TypeListIndex<T, TypeList<Tail...>>::value>
{
};

template <typename T>
struct TypeListIndex<T, TypeList<>>
	: std::integral_constant<std::size_t, 0>
{
};

/// @brief 型リスト内での型のインデックス（変数テンプレート版）
template <typename T, typename List>
inline constexpr std::size_t TYPE_LIST_INDEX = TypeListIndex<T, List>::value;

/// @brief 型リストの先頭に型を追加する
template <typename T, typename List>
struct TypeListPushFront;

template <typename T, typename... Ts>
struct TypeListPushFront<T, TypeList<Ts...>>
{
	using Type = TypeList<T, Ts...>;
};

/// @brief 型リストの末尾に型を追加する
template <typename T, typename List>
struct TypeListPushBack;

template <typename T, typename... Ts>
struct TypeListPushBack<T, TypeList<Ts...>>
{
	using Type = TypeList<Ts..., T>;
};

} // namespace sgc
