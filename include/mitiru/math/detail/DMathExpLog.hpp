/// @file DMathExpLog.hpp
/// @brief 決定論の exp / log / pow / sqrt (float)。openlibm (FreeBSD msun、FDLIBM 由来) の
///        e_expf.c、e_logf.c、e_powf.c を C++ の header に移した物。
///
/// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
/// Developed at SunPro, a Sun Microsystems, Inc. business.
/// Permission to use, copy, modify, and distribute this software is freely granted, provided that
/// this notice is preserved.
///
/// Copyright 1992-2011 The FreeBSD Project. All rights reserved. (2-clause BSD、全文は
/// THIRD_PARTY_NOTICES.txt の openlibm の項)
///
/// 元の C との違い: 浮動小数の例外フラグを立てる式を省き、overflow / underflow は inf / 0 を直接返す。
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include <mitiru/math/detail/DMathBits.hpp>

namespace mitiru::math::detail
{

MITIRU_DMATH_FP_STRICT_BEGIN

inline float expImpl(float x)
{
	constexpr float o_threshold = 8.8721679688e+01f;
	constexpr float u_threshold = -1.0397208405e+02f;
	constexpr float ln2HI[2] = {6.9314575195e-01f, -6.9314575195e-01f};
	constexpr float ln2LO[2] = {1.4286067653e-06f, -1.4286067653e-06f};
	constexpr float halF[2] = {0.5f, -0.5f};
	constexpr float invln2 = 1.4426950216e+00f;
	constexpr float P1 = 1.6666625440e-1f;
	constexpr float P2 = -2.7667332906e-3f;
	constexpr float twom100 = 7.8886090522e-31f;
	constexpr float inf = std::numeric_limits<float>::infinity();

	float hi = 0.0f;
	float lo = 0.0f;
	int k = 0;
	std::uint32_t hx = static_cast<std::uint32_t>(bitsOf(x));
	const int xsb = static_cast<int>((hx >> 31) & 1);
	hx &= 0x7fffffff;

	if (hx >= 0x42b17218)
	{
		if (hx > 0x7f800000) { return x + x; }
		if (hx == 0x7f800000) { return xsb == 0 ? x : 0.0f; }
		if (x > o_threshold) { return inf; }
		if (x < u_threshold) { return 0.0f; }
	}

	if (hx > 0x3eb17218)
	{
		if (hx < 0x3F851592)
		{
			hi = x - ln2HI[xsb];
			lo = ln2LO[xsb];
			k = 1 - xsb - xsb;
		}
		else
		{
			k = static_cast<int>(invln2 * x + halF[xsb]);
			const float t = static_cast<float>(k);
			hi = x - t * ln2HI[0];
			lo = t * ln2LO[0];
		}
		x = hi - lo;
	}
	else if (hx < 0x39000000) { return 1.0f + x; }

	const float t = x * x;
	const float twopk = k >= -125 ? floatOf(0x3f800000 + shiftLeft(k, 23)) : floatOf(0x3f800000 + shiftLeft(k + 100, 23));
	const float c = x - t * (P1 + t * P2);
	float y;
	if (k == 0) { return 1.0f - ((x * c) / (c - 2.0f) - x); }
	y = 1.0f - ((lo - (x * c) / (2.0f - c)) - hi);
	if (k >= -125)
	{
		if (k == 128) { return y * 2.0f * 0x1p127f; }
		return y * twopk;
	}
	return y * twopk * twom100;
}

inline float logImpl(float x)
{
	constexpr float ln2_hi = 6.9313812256e-01f;
	constexpr float ln2_lo = 9.0580006145e-06f;
	constexpr float two25 = 3.355443200e+07f;
	constexpr float Lg1 = 0xaaaaaa.0p-24f;
	constexpr float Lg2 = 0xccce13.0p-25f;
	constexpr float Lg3 = 0x91e9ee.0p-25f;
	constexpr float Lg4 = 0xf89e26.0p-26f;
	constexpr float inf = std::numeric_limits<float>::infinity();
	constexpr float nan = std::numeric_limits<float>::quiet_NaN();

	std::int32_t ix = bitsOf(x);
	int k = 0;
	if (ix < 0x00800000)
	{
		if ((ix & 0x7fffffff) == 0) { return -inf; }
		if (ix < 0) { return nan; }
		k -= 25;
		x *= two25;
		ix = bitsOf(x);
	}
	if (ix >= 0x7f800000) { return x + x; }
	k += (ix >> 23) - 127;
	ix &= 0x007fffff;
	std::int32_t i = (ix + (0x95f64 << 3)) & 0x800000;
	x = floatOf(ix | (i ^ 0x3f800000));
	k += (i >> 23);
	const float f = x - 1.0f;
	if ((0x007fffff & (0x8000 + ix)) < 0xc000)
	{
		if (f == 0.0f)
		{
			if (k == 0) { return 0.0f; }
			const float dk = static_cast<float>(k);
			return dk * ln2_hi + dk * ln2_lo;
		}
		const float R = f * f * (0.5f - 0.33333333333333333f * f);
		if (k == 0) { return f - R; }
		const float dk = static_cast<float>(k);
		return dk * ln2_hi - ((R - dk * ln2_lo) - f);
	}
	const float s = f / (2.0f + f);
	const float dk = static_cast<float>(k);
	const float z = s * s;
	i = ix - (0x6147a << 3);
	const float w = z * z;
	const std::int32_t j = (0x6b851 << 3) - ix;
	const float t1 = w * (Lg2 + w * Lg4);
	const float t2 = z * (Lg1 + w * Lg3);
	i |= j;
	const float R = t2 + t1;
	if (i > 0)
	{
		const float hfsq = 0.5f * f * f;
		if (k == 0) { return f - (hfsq - s * (hfsq + R)); }
		return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
	}
	if (k == 0) { return f - s * (f - R); }
	return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
}

MITIRU_DMATH_FP_STRICT_END

}  // namespace mitiru::math::detail
