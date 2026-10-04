#pragma once

/// @file LayoutWalk.hpp
/// @brief GameMemory の型から float / double の位置と暗黙の詰め物のバイト数を数える。
/// @details `MITIRU_GAME` が `mitiru_module_float_offsets` / `mitiru_module_padding_bytes` として自動で export する。
/// host は反射 (`MITIRU_REFLECT*`) のない game でも NaN と詰め物を調べられる。
/// たどる範囲は LayoutFingerprint.hpp と同じ。
/// 構造化束縛でたどれない型 (基底クラス・65 個以上のフィールド)、union、再現した大きさが sizeof と
/// 一致しない型 (#pragma pack 等) の中は見ない。

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <mitiru/module/LayoutFingerprint.hpp>

namespace mitiru::module
{

/// double の位置には最上位 bit を立て、残りの bit にバイト位置を入れる。
inline constexpr std::uint32_t kFloatOffsetDouble = 0x80000000u;
/// GameMemory の型の中をたどれず、詰め物を数えられないときの値。
inline constexpr std::uint32_t kPaddingBytesUnknown = 0xFFFFFFFFu;

namespace detail
{

template<class T, std::size_t... I>
constexpr std::array<std::size_t, sizeof...(I)> declaredOffsetsImpl(std::index_sequence<I...>) noexcept
{
	constexpr std::size_t sizes[sizeof...(I)]  = { sizeof(FieldTypeAt<T, I>)... };
	constexpr std::size_t aligns[sizeof...(I)] = { alignof(FieldTypeAt<T, I>)... };
	std::array<std::size_t, sizeof...(I)> out{};
	std::size_t end = 0;
	for (std::size_t i = 0; i < sizeof...(I); ++i)
	{
		out[i] = alignUp(end, aligns[i]);
		end = out[i] + sizes[i];
	}
	return out;
}

/// 各フィールドの位置を宣言順に並べる。
template<class T>
inline constexpr auto kDeclaredOffsets = declaredOffsetsImpl<T>(std::make_index_sequence<fieldCount<T>>{});

/// 再現した配置の終わりを型の揃えで切り上げた値が sizeof と一致する型だけ、中をたどる。
template<class T>
consteval bool isWalkable() noexcept
{
	if constexpr (!isFingerprintTraversable<T> || fieldCount<T> == 0) { return false; }
	else
	{
		constexpr std::size_t n = fieldCount<T>;
		return alignUp(kDeclaredOffsets<T>[n - 1] + sizeof(FieldTypeAt<T, n - 1>), alignof(T)) == sizeof(T);
	}
}

template<class T>
consteval bool containsFloat() noexcept;

template<class T, std::size_t... I>
consteval bool fieldsContainFloat(std::index_sequence<I...>) noexcept
{
	return (containsFloat<FieldTypeAt<T, I>>() || ... || false);
}

/// 整数だけの大きな配列を 1 要素ずつ調べないため、float を含む型だけをたどる。
template<class T>
consteval bool containsFloat() noexcept
{
	if constexpr (std::is_floating_point_v<T>) { return std::is_same_v<T, float> || std::is_same_v<T, double>; }
	else if constexpr (std::is_array_v<T>) { return containsFloat<std::remove_extent_t<T>>(); }
	else if constexpr (isWalkable<T>()) { return fieldsContainFloat<T>(std::make_index_sequence<fieldCount<T>>{}); }
	else { return false; }
}

/// out には最大 cap 個の位置を書き、件数は全件を数える。
struct FloatOffsetSink
{
	std::uint32_t* out = nullptr;
	std::int32_t   cap = 0;
	std::int32_t   count = 0;

	void push(std::size_t offset, bool isDouble) noexcept
	{
		if (out != nullptr && count < cap)
		{
			out[count] = static_cast<std::uint32_t>(offset) | (isDouble ? kFloatOffsetDouble : 0u);
		}
		++count;
	}
};

template<class T>
void walkFloatOffsets(std::size_t base, FloatOffsetSink& sink) noexcept;

template<class T, std::size_t... I>
void walkFloatFields(std::size_t base, FloatOffsetSink& sink, std::index_sequence<I...>) noexcept
{
	(walkFloatOffsets<FieldTypeAt<T, I>>(base + kDeclaredOffsets<T>[I], sink), ...);
}

template<class T>
void walkFloatOffsets(std::size_t base, FloatOffsetSink& sink) noexcept
{
	if constexpr (!containsFloat<T>()) { (void)base; (void)sink; }
	else if constexpr (std::is_floating_point_v<T>) { sink.push(base, std::is_same_v<T, double>); }
	else if constexpr (std::is_array_v<T>)
	{
		using E = std::remove_extent_t<T>;
		for (std::size_t i = 0; i < std::extent_v<T>; ++i) { walkFloatOffsets<E>(base + i * sizeof(E), sink); }
	}
	else { walkFloatFields<T>(base, sink, std::make_index_sequence<fieldCount<T>>{}); }
}

template<class T>
consteval std::size_t valueBytesWalked() noexcept;

template<class T, std::size_t... I>
consteval std::size_t fieldsValueBytes(std::index_sequence<I...>) noexcept
{
	return (valueBytesWalked<FieldTypeAt<T, I>>() + ... + std::size_t{0});
}

/// 値を持つバイト数。たどれない型は sizeof 全体を値として数え、詰め物を少なく見積もる。
template<class T>
consteval std::size_t valueBytesWalked() noexcept
{
	if constexpr (std::is_array_v<T>) { return std::extent_v<T> * valueBytesWalked<std::remove_extent_t<T>>(); }
	else if constexpr (isWalkable<T>()) { return fieldsValueBytes<T>(std::make_index_sequence<fieldCount<T>>{}); }
	else { return sizeof(T); }
}

}  // namespace detail

/// T の float / double の位置を out に最大 cap 個書き、全件数を返す。out が nullptr なら件数だけを返す。
template<class T>
std::int32_t floatOffsetsOf(std::uint32_t* out, std::int32_t cap) noexcept
{
	detail::FloatOffsetSink sink{out, cap, 0};
	detail::walkFloatOffsets<T>(0, sink);
	return sink.count;
}

/// T の暗黙の詰め物のバイト数。T の中をたどれなければ kPaddingBytesUnknown。
template<class T>
[[nodiscard]] consteval std::uint32_t paddingBytesWalked() noexcept
{
	if constexpr (!detail::isWalkable<T>()) { return kPaddingBytesUnknown; }
	else { return static_cast<std::uint32_t>(sizeof(T) - detail::valueBytesWalked<T>()); }
}

}  // namespace mitiru::module
