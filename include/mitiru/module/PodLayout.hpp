#pragma once

/// @file PodLayout.hpp
/// @brief GameMemory に暗黙の詰め物 (padding) が無いことをコンパイル時に確かめる。
/// @details host は GameMemory をバイト列のまま比較・差分・圧縮する (replay の照合、rewind ring、
/// `/api/ai/diff`)。詰め物の byte は代入で値が保たれない (一時オブジェクトの不定値がそのまま写る)
/// ので、中身が同じ状態でもバイト列が食い違い、決定論の照合が偽の分岐を報告する。
///
/// aggregate を構造化束縛でたどり、葉 (スカラー・enum・union・aggregate でない class) の
/// byte 数を合計して sizeof と比べる。固定長 C 配列、ネストした aggregate、`FixedVec<E,N>`
/// (要素 E の中と count の後ろ)、`PodArena<N>` まで入る。aggregate でない class は中を
/// 見られないので sizeof 全体を値として数える。基底クラスを持つ aggregate は構造化束縛が
/// 通らずコンパイルエラーになる。
///
/// game 側は GameMemory の型を書いたファイルで `MITIRU_ASSERT_NO_PADDING(型)` を 1 行置く。
/// 詰め物があると `GameMemoryPaddingAfterField<持ち主の型, フィールド番号, フィールドの型, byte 数>`
/// の実体化でコンパイルが止まる。フィールド番号は 0 始まりの宣言順。

#include <cstddef>
#include <type_traits>
#include <utility>

#include <mitiru/module/detail/AutoReflectBind.hpp>
#include <mitiru/module/detail/AutoReflectCount.hpp>

namespace mitiru::module
{
namespace detail
{

template<class T, std::size_t I>
using FieldTypeAt =
	std::remove_cv_t<std::remove_reference_t<decltype(std::get<I>(tieFields(std::declval<T&>())))>>;

/// 反射では中を見せない型 (`MitiruOpaqueReflect`、PodArena 等) も、詰め物は中まで数える。
template<class M>
inline constexpr bool isLayoutAggregate =
	std::is_class_v<M> && !std::is_union_v<M> && std::is_aggregate_v<M>;

/// fieldCount は 64 で飽和し、それを超える型では構造化束縛が使えなくなる。先に弾いて理由を出す。
template<class M>
inline constexpr bool hasTooManyFields = isAggregateConstructibleN<M, kAutoReflectMaxFields + 1>;

template<class T>
constexpr std::size_t valueBytesOf() noexcept;

template<class T, std::size_t... I>
constexpr std::size_t valueBytesOfFields(std::index_sequence<I...>) noexcept
{
	return (valueBytesOf<FieldTypeAt<T, I>>() + ... + std::size_t{0});
}

/// 値を持つ byte の数 (= sizeof から詰め物を引いたもの)。
template<class T>
constexpr std::size_t valueBytesOf() noexcept
{
	if constexpr (std::is_array_v<T>)
	{
		return std::extent_v<T> * valueBytesOf<std::remove_extent_t<T>>();
	}
	else if constexpr (isLayoutAggregate<T>)
	{
		static_assert(!hasTooManyFields<T>,
			"PodLayout: 65 個以上のフィールドを持つ struct は数えられない。部分 struct に分けること");
		// 空の struct の 1 byte は誰も書かないので値が変わらない。詰め物として数えない。
		if constexpr (fieldCount<T> == 0) { return sizeof(T); }
		else { return valueBytesOfFields<T>(std::make_index_sequence<fieldCount<T>>{}); }
	}
	else { return sizeof(T); }
}

/// T の直下 (ネストの中は含まない) で最初に見つかる隙間。
struct LocalGap
{
	std::size_t fieldIndex;  ///< 隙間の直前にあるフィールドの番号
	std::size_t bytes;       ///< 0 = 隙間なし
};

[[nodiscard]] constexpr std::size_t alignUp(std::size_t v, std::size_t a) noexcept
{
	return (v + a - 1) / a * a;
}

/// 宣言順の配置を再現して隙間を探す。#pragma pack 等で再現が実物と合わないときは位置を
/// 決められないので、合計の差を最後のフィールドの後ろとして返す。
template<class T, std::size_t... I>
constexpr LocalGap firstLocalGapImpl(std::index_sequence<I...>) noexcept
{
	constexpr std::size_t n = sizeof...(I);
	constexpr std::size_t sizes[n + 1]  = { sizeof(FieldTypeAt<T, I>)..., 0 };
	constexpr std::size_t aligns[n + 1] = { alignof(FieldTypeAt<T, I>)..., 1 };
	constexpr std::size_t total = (sizeof(FieldTypeAt<T, I>) + ... + std::size_t{0});
	if (n == 0 || sizeof(T) == total) { return LocalGap{ 0, 0 }; }

	std::size_t end = 0;
	for (std::size_t i = 0; i < n; ++i)
	{
		const std::size_t start = alignUp(end, aligns[i]);
		if (i > 0 && start != end) { return LocalGap{ i - 1, start - end }; }
		end = start + sizes[i];
	}
	const std::size_t last = (n > 0) ? n - 1 : 0;
	return LocalGap{ last, sizeof(T) - total };
}

template<class T>
constexpr LocalGap firstLocalGap() noexcept
{
	return firstLocalGapImpl<T>(std::make_index_sequence<fieldCount<T>>{});
}

/// 失敗したときにコンパイラが出す実体化の名前がそのまま報告になる。
template<class Owner, std::size_t FieldIndex, class FieldType, std::size_t GapBytes>
struct GameMemoryPaddingAfterField
{
	static_assert(GapBytes == 0,
		"GameMemory に暗黙の詰め物がある (テンプレート引数 = 持ち主の型, フィールド番号, フィールドの型, "
		"byte 数)。フィールドを大きい順に並べ替えるか、std::uint8_t reserved[n]{} で埋めること");
	static constexpr bool ok = true;
};

template<class T>
constexpr bool assertNoPaddingIn() noexcept;

template<class M>
constexpr bool assertNoPaddingInField() noexcept
{
	// 別名を挟むと MSVC がエラーに別名のまま型を出すので、実体化の引数は直に書く。
	if constexpr (isLayoutAggregate<std::remove_all_extents_t<M>>)
	{
		return assertNoPaddingIn<std::remove_all_extents_t<M>>();
	}
	else { return true; }
}

template<class T, std::size_t... I>
constexpr bool assertNoPaddingInImpl(std::index_sequence<I...>) noexcept
{
	constexpr LocalGap gap = firstLocalGap<T>();
	return GameMemoryPaddingAfterField<T, gap.fieldIndex,
	                                   std::remove_cvref_t<decltype(std::get<gap.fieldIndex>(tieFields(std::declval<T&>())))>,
	                                   gap.bytes>::ok
	    && (assertNoPaddingInField<FieldTypeAt<T, I>>() && ... && true);
}

template<class T>
constexpr bool assertNoPaddingIn() noexcept
{
	static_assert(!hasTooManyFields<T>,
		"PodLayout: 65 個以上のフィールドを持つ struct は数えられない。部分 struct に分けること");
	if constexpr (fieldCount<T> == 0) { return true; }
	else { return assertNoPaddingInImpl<T>(std::make_index_sequence<fieldCount<T>>{}); }
}

}  // namespace detail

/// T の詰め物の byte 数。ネストした aggregate・配列・FixedVec の要素の中も数える。
template<class T>
[[nodiscard]] constexpr std::size_t paddingBytes() noexcept
{
	static_assert(std::is_trivially_copyable_v<T>, "paddingBytes<T>: T は flat POD であること");
	return sizeof(T) - detail::valueBytesOf<T>();
}

template<class T>
inline constexpr bool hasNoPadding = (paddingBytes<T>() == 0);

/// 詰め物が無いことのコンパイル時確認。あれば最初の隙間の位置をエラーに出す。
template<class T>
[[nodiscard]] constexpr bool assertNoPadding() noexcept
{
	static_assert(std::is_trivially_copyable_v<T>, "assertNoPadding<T>: T は flat POD であること");
	if constexpr (detail::isLayoutAggregate<T>) { return detail::assertNoPaddingIn<T>() && hasNoPadding<T>; }
	else { return hasNoPadding<T>; }
}

}  // namespace mitiru::module

/// 引数は可変長にしてある (Pool<Msg, 4> のようにカンマを含む型をそのまま書けるように)。
#define MITIRU_ASSERT_NO_PADDING(...)                                                      \
	static_assert(::mitiru::module::assertNoPadding<__VA_ARGS__>(),                        \
		"MITIRU_ASSERT_NO_PADDING(" #__VA_ARGS__ "): 詰め物がある (直前のエラーが位置を示す)")
