/// @file DMathBits.hpp
/// @brief DMath の内部で使う float のビット操作と、FMA の融合を止めるマーカー。
#pragma once

#include <cstdint>
#include <cstring>

// 融合すると x * y + z の丸めが 1 回になり、/arch:AVX2 のビルドと無しのビルドで結果が変わる。
// 元の C は融合しない前提で係数を決めている。
#if defined(_MSC_VER) && !defined(__clang__)
#define MITIRU_DMATH_FP_STRICT_BEGIN __pragma(float_control(precise, on, push)) __pragma(fp_contract(off))
#define MITIRU_DMATH_FP_STRICT_END __pragma(float_control(pop))
#else
#define MITIRU_DMATH_FP_STRICT_BEGIN
#define MITIRU_DMATH_FP_STRICT_END
#endif

namespace mitiru::math::detail
{

[[nodiscard]] inline std::int32_t bitsOf(float f) noexcept
{
	std::int32_t u;
	std::memcpy(&u, &f, sizeof u);
	return u;
}

/// 負の値の左シフトは C++17 までは未定義なので、符号なしで動かして戻す
[[nodiscard]] inline std::int32_t shiftLeft(std::int32_t v, int n) noexcept
{
	return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << n);
}

[[nodiscard]] inline float floatOf(std::int32_t u) noexcept
{
	float f;
	std::memcpy(&f, &u, sizeof f);
	return f;
}

}  // namespace mitiru::math::detail
