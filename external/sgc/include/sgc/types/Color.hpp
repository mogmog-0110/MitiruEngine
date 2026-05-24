#pragma once

/// @file Color.hpp
/// @brief RGBA色型 Color<T>
///
/// ゲーム開発で使用する汎用カラー型。
/// 内部は浮動小数点 [0,1] で管理し、RGBA8/Hex/HSV変換を提供する。

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief RGBA色
/// @tparam T 浮動小数点型
///
/// @code
/// auto red = sgc::Colorf::red();
/// auto custom = sgc::Colorf::fromRGBA8(128, 200, 50, 255);
/// auto blended = red.lerp(sgc::Colorf::blue(), 0.5f);
/// @endcode
template <FloatingPoint T>
struct Color
{
	T r{};  ///< 赤成分 [0, 1]
	T g{};  ///< 緑成分 [0, 1]
	T b{};  ///< 青成分 [0, 1]
	T a{T{1}};  ///< アルファ成分 [0, 1]（デフォルト: 不透明）

	/// @brief デフォルトコンストラクタ（黒・不透明）
	constexpr Color() noexcept = default;

	/// @brief RGBA成分を指定して構築する
	/// @param r 赤 [0, 1]
	/// @param g 緑 [0, 1]
	/// @param b 青 [0, 1]
	/// @param a アルファ [0, 1]（デフォルト: 1）
	constexpr Color(T r, T g, T b, T a = T{1}) noexcept
		: r(r), g(g), b(b), a(a) {}

	/// @brief 異なる型のColorから変換する
	template <FloatingPoint U>
	constexpr explicit Color(const Color<U>& other) noexcept
		: r(static_cast<T>(other.r)), g(static_cast<T>(other.g))
		, b(static_cast<T>(other.b)), a(static_cast<T>(other.a)) {}

	[[nodiscard]] constexpr bool operator==(const Color& rhs) const noexcept = default;

	// ── 変換 ────────────────────────────────────────────────

	/// @brief RGBA8 (0-255) から構築する
	/// @param r 赤 [0, 255]
	/// @param g 緑 [0, 255]
	/// @param b 青 [0, 255]
	/// @param a アルファ [0, 255]（デフォルト: 255）
	[[nodiscard]] static constexpr Color fromRGBA8(
		std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept
	{
		constexpr T inv = T{1} / T{255};
		return {
			static_cast<T>(r) * inv,
			static_cast<T>(g) * inv,
			static_cast<T>(b) * inv,
			static_cast<T>(a) * inv
		};
	}

	/// @brief RGBA8 (0-255) にパックして返す（r, g, b, a の順）
	/// @return 4要素の配列 [r, g, b, a]
	struct RGBA8
	{
		std::uint8_t r, g, b, a;
	};

	[[nodiscard]] constexpr RGBA8 toRGBA8() const noexcept
	{
		auto clampByte = [](T v) -> std::uint8_t
		{
			if (v <= T{0}) return 0;
			if (v >= T{1}) return 255;
			return static_cast<std::uint8_t>(v * T{255} + T{0.5});
		};
		return {clampByte(r), clampByte(g), clampByte(b), clampByte(a)};
	}

	/// @brief 0xRRGGBBAA 形式の16進数から構築する
	/// @param hex 16進カラー値（0xRRGGBBAA）
	[[nodiscard]] static constexpr Color fromHex(std::uint32_t hex) noexcept
	{
		return fromRGBA8(
			static_cast<std::uint8_t>((hex >> 24) & 0xFF),
			static_cast<std::uint8_t>((hex >> 16) & 0xFF),
			static_cast<std::uint8_t>((hex >> 8) & 0xFF),
			static_cast<std::uint8_t>(hex & 0xFF)
		);
	}

	/// @brief 0xRRGGBBAA 形式の16進数に変換する
	[[nodiscard]] constexpr std::uint32_t toHex() const noexcept
	{
		const auto c = toRGBA8();
		return (static_cast<std::uint32_t>(c.r) << 24)
			 | (static_cast<std::uint32_t>(c.g) << 16)
			 | (static_cast<std::uint32_t>(c.b) << 8)
			 | static_cast<std::uint32_t>(c.a);
	}

	/// @brief HSV値から構築する
	/// @param h 色相 [0, 360)
	/// @param s 彩度 [0, 1]
	/// @param v 明度 [0, 1]
	/// @param a アルファ [0, 1]（デフォルト: 1）
	[[nodiscard]] static Color fromHSV(T h, T s, T v, T a = T{1}) noexcept
	{
		// 色相を [0, 360) に正規化
		h = std::fmod(h, T{360});
		if (h < T{0}) h += T{360};

		const T c = v * s;
		const T x = c * (T{1} - std::abs(std::fmod(h / T{60}, T{2}) - T{1}));
		const T m = v - c;

		T r1, g1, b1;
		if (h < T{60})       { r1 = c; g1 = x; b1 = T{0}; }
		else if (h < T{120}) { r1 = x; g1 = c; b1 = T{0}; }
		else if (h < T{180}) { r1 = T{0}; g1 = c; b1 = x; }
		else if (h < T{240}) { r1 = T{0}; g1 = x; b1 = c; }
		else if (h < T{300}) { r1 = x; g1 = T{0}; b1 = c; }
		else                 { r1 = c; g1 = T{0}; b1 = x; }

		return {r1 + m, g1 + m, b1 + m, a};
	}

	/// @brief HSV値に変換する
	/// @return {色相 [0, 360), 彩度 [0, 1], 明度 [0, 1]}
	struct HSV
	{
		T h, s, v;
	};

	[[nodiscard]] HSV toHSV() const noexcept
	{
		const T maxC = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
		const T minC = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
		const T delta = maxC - minC;

		T h = T{0};
		if (delta > T{0})
		{
			if (maxC == r)
			{
				h = T{60} * std::fmod((g - b) / delta, T{6});
			}
			else if (maxC == g)
			{
				h = T{60} * ((b - r) / delta + T{2});
			}
			else
			{
				h = T{60} * ((r - g) / delta + T{4});
			}
		}
		if (h < T{0}) h += T{360};

		const T s = (maxC > T{0}) ? (delta / maxC) : T{0};
		return {h, s, maxC};
	}

	// ── 成分操作（不変性: 新しいColorを返す）────────────────

	/// @brief アルファ値を変更した新しいColorを返す
	[[nodiscard]] constexpr Color withAlpha(T newA) const noexcept { return {r, g, b, newA}; }

	/// @brief 赤成分を変更した新しいColorを返す
	[[nodiscard]] constexpr Color withRed(T newR) const noexcept { return {newR, g, b, a}; }

	/// @brief 緑成分を変更した新しいColorを返す
	[[nodiscard]] constexpr Color withGreen(T newG) const noexcept { return {r, newG, b, a}; }

	/// @brief 青成分を変更した新しいColorを返す
	[[nodiscard]] constexpr Color withBlue(T newB) const noexcept { return {r, g, newB, a}; }

	/// @brief 反転色を返す（アルファは保持）
	[[nodiscard]] constexpr Color inverted() const noexcept
	{
		return {T{1} - r, T{1} - g, T{1} - b, a};
	}

	/// @brief プレマルチプライド・アルファ版を返す
	[[nodiscard]] constexpr Color premultiplied() const noexcept
	{
		return {r * a, g * a, b * a, a};
	}

	// ── 明度・彩度操作（HSV経由、不変操作） ─────────────────

	/// @brief 指定量だけ明るくした新しいColorを返す
	/// @param amount 明るさの増加量 [0, 1]
	[[nodiscard]] Color brighten(T amount) const noexcept
	{
		auto hsv = toHSV();
		hsv.v = (hsv.v + amount > T{1}) ? T{1} : hsv.v + amount;
		return fromHSV(hsv.h, hsv.s, hsv.v, a);
	}

	/// @brief 指定量だけ暗くした新しいColorを返す
	/// @param amount 明るさの減少量 [0, 1]
	[[nodiscard]] Color darken(T amount) const noexcept
	{
		auto hsv = toHSV();
		hsv.v = (hsv.v - amount < T{0}) ? T{0} : hsv.v - amount;
		return fromHSV(hsv.h, hsv.s, hsv.v, a);
	}

	/// @brief 指定量だけ彩度を上げた新しいColorを返す
	/// @param amount 彩度の増加量 [0, 1]
	[[nodiscard]] Color saturate(T amount) const noexcept
	{
		auto hsv = toHSV();
		hsv.s = (hsv.s + amount > T{1}) ? T{1} : hsv.s + amount;
		return fromHSV(hsv.h, hsv.s, hsv.v, a);
	}

	/// @brief 指定量だけ彩度を下げた新しいColorを返す
	/// @param amount 彩度の減少量 [0, 1]
	[[nodiscard]] Color desaturate(T amount) const noexcept
	{
		auto hsv = toHSV();
		hsv.s = (hsv.s - amount < T{0}) ? T{0} : hsv.s - amount;
		return fromHSV(hsv.h, hsv.s, hsv.v, a);
	}

	/// @brief 補色を返す（色相を180度回転）
	[[nodiscard]] Color complement() const noexcept
	{
		auto hsv = toHSV();
		hsv.h += T{180};
		if (hsv.h >= T{360}) hsv.h -= T{360};
		return fromHSV(hsv.h, hsv.s, hsv.v, a);
	}

	// ── 補間 ────────────────────────────────────────────────

	/// @brief RGB空間で線形補間する
	/// @param other 補間先の色
	/// @param t 補間係数 [0, 1]
	[[nodiscard]] constexpr Color lerp(const Color& other, T t) const noexcept
	{
		return {
			r + (other.r - r) * t,
			g + (other.g - g) * t,
			b + (other.b - b) * t,
			a + (other.a - a) * t
		};
	}

	/// @brief HSV空間で線形補間する
	/// @param other 補間先の色
	/// @param t 補間係数 [0, 1]
	[[nodiscard]] Color lerpHSV(const Color& other, T t) const noexcept
	{
		const auto hsv1 = toHSV();
		const auto hsv2 = other.toHSV();

		// 色相は最短経路で補間
		T dh = hsv2.h - hsv1.h;
		if (dh > T{180}) dh -= T{360};
		if (dh < T{-180}) dh += T{360};

		const T h = hsv1.h + dh * t;
		const T s = hsv1.s + (hsv2.s - hsv1.s) * t;
		const T v = hsv1.v + (hsv2.v - hsv1.v) * t;
		const T newA = a + (other.a - a) * t;

		return fromHSV(h, s, v, newA);
	}

	// ── 定数 ────────────────────────────────────────────────

	/// @brief 白 (1, 1, 1, 1)
	[[nodiscard]] static constexpr Color white() noexcept { return {T{1}, T{1}, T{1}, T{1}}; }

	/// @brief 黒 (0, 0, 0, 1)
	[[nodiscard]] static constexpr Color black() noexcept { return {T{0}, T{0}, T{0}, T{1}}; }

	/// @brief 赤 (1, 0, 0, 1)
	[[nodiscard]] static constexpr Color red() noexcept { return {T{1}, T{0}, T{0}, T{1}}; }

	/// @brief 緑 (0, 1, 0, 1)
	[[nodiscard]] static constexpr Color green() noexcept { return {T{0}, T{1}, T{0}, T{1}}; }

	/// @brief 青 (0, 0, 1, 1)
	[[nodiscard]] static constexpr Color blue() noexcept { return {T{0}, T{0}, T{1}, T{1}}; }

	/// @brief 黄 (1, 1, 0, 1)
	[[nodiscard]] static constexpr Color yellow() noexcept { return {T{1}, T{1}, T{0}, T{1}}; }

	/// @brief シアン (0, 1, 1, 1)
	[[nodiscard]] static constexpr Color cyan() noexcept { return {T{0}, T{1}, T{1}, T{1}}; }

	/// @brief マゼンタ (1, 0, 1, 1)
	[[nodiscard]] static constexpr Color magenta() noexcept { return {T{1}, T{0}, T{1}, T{1}}; }

	/// @brief 透明 (0, 0, 0, 0)
	[[nodiscard]] static constexpr Color transparent() noexcept { return {T{0}, T{0}, T{0}, T{0}}; }

	// ── シリアライズ ────────────────────────────────────────

	/// @brief シリアライズ（const版）
	template <typename V>
	void visit(V& v) const { v.write("r", r); v.write("g", g); v.write("b", b); v.write("a", a); }

	/// @brief デシリアライズ（非const版）
	template <typename V>
	void visit(V& v) { v.read("r", r); v.read("g", g); v.read("b", b); v.read("a", a); }
};

// ── エイリアス ──────────────────────────────────────────────────

using Colorf = Color<float>;    ///< float版 Color
using Colord = Color<double>;   ///< double版 Color

} // namespace sgc
