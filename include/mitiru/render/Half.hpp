#pragma once

/// @file Half.hpp
/// @brief IEEE 754 の半精度 (16 bit) と float の相互変換。RGBA16F と BC6H の画素を数で扱う所が共有する

#include <cstdint>
#include <cstring>

namespace mitiru::render
{

/// @brief float を 16 bit 浮動小数へ (最近接偶数へ丸める)
[[nodiscard]] inline std::uint16_t floatToHalf(float f) noexcept
{
	std::uint32_t x = 0;
	std::memcpy(&x, &f, sizeof(x));
	const std::uint32_t sign = (x >> 16) & 0x8000u;
	const std::uint32_t absx = x & 0x7FFFFFFFu;
	if (absx >= 0x7F800000u) { return static_cast<std::uint16_t>(sign | (absx > 0x7F800000u ? 0x7E00u : 0x7C00u)); }
	if (absx >= 0x477FF000u) { return static_cast<std::uint16_t>(sign | 0x7C00u); }
	if (absx < 0x38800000u)
	{
		// 非正規化数: 仮数を右へずらして丸める
		const std::uint32_t shift = 113u - (absx >> 23);
		if (shift > 24u) { return static_cast<std::uint16_t>(sign); }
		const std::uint32_t mant = (absx & 0x7FFFFFu) | 0x800000u;
		const std::uint32_t half = mant >> (shift + 13u);
		const std::uint32_t rem = mant & ((1u << (shift + 13u)) - 1u);
		const std::uint32_t mid = 1u << (shift + 12u);
		const std::uint32_t up = (rem > mid || (rem == mid && (half & 1u))) ? 1u : 0u;
		return static_cast<std::uint16_t>(sign | (half + up));
	}
	const std::uint32_t biased = absx - 0x38000000u;
	const std::uint32_t half = biased >> 13;
	const std::uint32_t rem = biased & 0x1FFFu;
	const std::uint32_t up = (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) ? 1u : 0u;
	return static_cast<std::uint16_t>(sign | (half + up));
}

[[nodiscard]] inline float halfToFloat(std::uint16_t h) noexcept
{
	const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
	const std::uint32_t exponent = (h >> 10) & 0x1Fu;
	const std::uint32_t mantissa = h & 0x3FFu;
	std::uint32_t bits = sign;
	if (exponent == 31)
	{
		bits = sign | 0x7F800000u | (mantissa << 13);
	}
	else if (exponent != 0)
	{
		bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
	}
	else if (mantissa != 0)
	{
		// 非正規化数: 先頭の 1 が現れるまで左へずらす
		std::uint32_t m = mantissa;
		std::uint32_t e = 127 - 15 + 1;
		while ((m & 0x400u) == 0)
		{
			m <<= 1;
			--e;
		}
		bits = sign | (e << 23) | ((m & 0x3FFu) << 13);
	}
	float f = 0.0f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}

} // namespace mitiru::render
