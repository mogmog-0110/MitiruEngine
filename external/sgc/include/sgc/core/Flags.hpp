#pragma once

/// @file Flags.hpp
/// @brief 型安全なビットフラグ演算
///
/// enum classに対してビット演算子を有効化する。
/// ゲーム開発でよく使うフラグ管理を型安全に行える。
///
/// @code
/// enum class Layer : unsigned {
///     None     = 0,
///     Player   = 1 << 0,
///     Enemy    = 1 << 1,
///     Bullet   = 1 << 2,
///     Wall     = 1 << 3,
/// };
/// SGC_ENABLE_FLAGS(Layer);
///
/// auto mask = Layer::Player | Layer::Enemy;
/// if (sgc::hasFlag(mask, Layer::Player)) { /* ... */ }
/// @endcode

#include <type_traits>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief フラグにビットが含まれているか判定する
/// @tparam E enum class型
/// @param flags フラグ値
/// @param flag 検査するビット
/// @return flagが含まれていれば true
template <ScopedEnum E>
[[nodiscard]] constexpr bool hasFlag(E flags, E flag) noexcept
{
	using U = std::underlying_type_t<E>;
	return (static_cast<U>(flags) & static_cast<U>(flag)) == static_cast<U>(flag);
}

/// @brief フラグにビットを追加する
/// @tparam E enum class型
/// @param flags 元のフラグ値
/// @param flag 追加するビット
/// @return ビットが追加されたフラグ値
template <ScopedEnum E>
[[nodiscard]] constexpr E setFlag(E flags, E flag) noexcept
{
	using U = std::underlying_type_t<E>;
	return static_cast<E>(static_cast<U>(flags) | static_cast<U>(flag));
}

/// @brief フラグからビットを除去する
/// @tparam E enum class型
/// @param flags 元のフラグ値
/// @param flag 除去するビット
/// @return ビットが除去されたフラグ値
template <ScopedEnum E>
[[nodiscard]] constexpr E clearFlag(E flags, E flag) noexcept
{
	using U = std::underlying_type_t<E>;
	return static_cast<E>(static_cast<U>(flags) & ~static_cast<U>(flag));
}

/// @brief フラグのビットをトグルする
/// @tparam E enum class型
/// @param flags 元のフラグ値
/// @param flag トグルするビット
/// @return ビットがトグルされたフラグ値
template <ScopedEnum E>
[[nodiscard]] constexpr E toggleFlag(E flags, E flag) noexcept
{
	using U = std::underlying_type_t<E>;
	return static_cast<E>(static_cast<U>(flags) ^ static_cast<U>(flag));
}

} // namespace sgc

/// @brief enum classにビット演算子を有効化するマクロ
///
/// enum class定義の直後に記述する。
/// |, &, ^, ~ 演算子がそのenum classで使えるようになる。
#define SGC_ENABLE_FLAGS(EnumType)                                             \
	[[nodiscard]] constexpr EnumType operator|(EnumType a, EnumType b) noexcept \
	{                                                                          \
		using U = std::underlying_type_t<EnumType>;                            \
		return static_cast<EnumType>(static_cast<U>(a) | static_cast<U>(b));   \
	}                                                                          \
	[[nodiscard]] constexpr EnumType operator&(EnumType a, EnumType b) noexcept \
	{                                                                          \
		using U = std::underlying_type_t<EnumType>;                            \
		return static_cast<EnumType>(static_cast<U>(a) & static_cast<U>(b));   \
	}                                                                          \
	[[nodiscard]] constexpr EnumType operator^(EnumType a, EnumType b) noexcept \
	{                                                                          \
		using U = std::underlying_type_t<EnumType>;                            \
		return static_cast<EnumType>(static_cast<U>(a) ^ static_cast<U>(b));   \
	}                                                                          \
	[[nodiscard]] constexpr EnumType operator~(EnumType a) noexcept            \
	{                                                                          \
		using U = std::underlying_type_t<EnumType>;                            \
		return static_cast<EnumType>(~static_cast<U>(a));                      \
	}                                                                          \
	constexpr EnumType& operator|=(EnumType& a, EnumType b) noexcept           \
	{                                                                          \
		return a = a | b;                                                       \
	}                                                                          \
	constexpr EnumType& operator&=(EnumType& a, EnumType b) noexcept           \
	{                                                                          \
		return a = a & b;                                                       \
	}                                                                          \
	constexpr EnumType& operator^=(EnumType& a, EnumType b) noexcept           \
	{                                                                          \
		return a = a ^ b;                                                       \
	}
