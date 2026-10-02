#pragma once

/// @file LayoutFingerprint.hpp
/// @brief GameMemory の型の形 (葉の種類・大きさ・揃え・並び・配列の長さ) をコンパイル時に hash にする。
/// @details 反射 (`MITIRU_REFLECT*`) を宣言しない game でも、ホットリロードとセーブのロードで
/// 「同じ sizeof のまま並びや型が変わった」ことを見分けるために使う。`MITIRU_GAME` が
/// `mitiru_module_layout_hash` として自動で export する。フィールド名は含まないので、同じ型の
/// フィールドどうしの入れ替えは見分けられない (名前まで見たいときは反射を宣言する)。
/// 構造化束縛でたどれない型 (基底クラスを持つ、65 個以上のフィールド等) は sizeof と alignof
/// だけを畳み、コンパイルを止めない。

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <mitiru/module/PodLayout.hpp>

namespace mitiru::module
{
namespace detail
{

inline constexpr std::uint64_t kFingerprintSeed  = 14695981039346656037ull;
inline constexpr std::uint64_t kFingerprintPrime = 1099511628211ull;

[[nodiscard]] constexpr std::uint64_t foldFingerprint(std::uint64_t h, std::uint64_t v) noexcept
{
	for (int i = 0; i < 8; ++i)
	{
		h ^= (v >> (i * 8)) & 0xFFu;
		h *= kFingerprintPrime;
	}
	return h;
}

/// 標準レイアウトでない aggregate (基底クラスにもメンバがある等) は構造化束縛の数が
/// fieldCount と合わないことがあるので、中をたどらない。
template<class M>
inline constexpr bool isFingerprintTraversable =
	isLayoutAggregate<M> && std::is_standard_layout_v<M> && !hasTooManyFields<M>;

template<class T>
constexpr std::uint64_t fingerprintOf(std::uint64_t h) noexcept;

template<class T, std::size_t... I>
constexpr std::uint64_t fingerprintFields(std::uint64_t h, std::index_sequence<I...>) noexcept
{
	((h = fingerprintOf<FieldTypeAt<T, I>>(h)), ...);
	return h;
}

/// 葉の種類の符号。整数と浮動小数・符号の有無・bool・enum を分ける (同じ大きさでも別物)。
template<class T>
[[nodiscard]] constexpr std::uint64_t leafKind() noexcept
{
	if constexpr (std::is_same_v<T, bool>)      { return 'b'; }
	else if constexpr (std::is_floating_point_v<T>) { return 'f'; }
	else if constexpr (std::is_enum_v<T>)       { return 'e'; }
	else if constexpr (std::is_pointer_v<T>)    { return 'p'; }
	else if constexpr (std::is_integral_v<T>)   { return std::is_signed_v<T> ? 'i' : 'u'; }
	else                                        { return 'o'; }
}

template<class T>
constexpr std::uint64_t fingerprintOf(std::uint64_t h) noexcept
{
	if constexpr (std::is_array_v<T>)
	{
		h = foldFingerprint(h, '[');
		h = foldFingerprint(h, std::extent_v<T>);
		h = fingerprintOf<std::remove_extent_t<T>>(h);
		return foldFingerprint(h, ']');
	}
	else if constexpr (isFingerprintTraversable<T>)
	{
		h = foldFingerprint(h, '{');
		h = foldFingerprint(h, sizeof(T));
		h = fingerprintFields<T>(h, std::make_index_sequence<fieldCount<T>>{});
		return foldFingerprint(h, '}');
	}
	else
	{
		h = foldFingerprint(h, leafKind<T>());
		h = foldFingerprint(h, sizeof(T));
		return foldFingerprint(h, alignof(T));
	}
}

}  // namespace detail

/// T の形の hash。0 は「申告なし」の番兵に使うので返さない。
template<class T>
[[nodiscard]] constexpr std::uint64_t layoutFingerprint() noexcept
{
	const std::uint64_t h = detail::fingerprintOf<T>(detail::kFingerprintSeed);
	return (h != 0) ? h : 1;
}

}  // namespace mitiru::module
