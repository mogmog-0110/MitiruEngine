/// @file DMathPow.hpp
/// @brief 決定論の pow (float)。openlibm (FreeBSD msun、FDLIBM 由来) の e_powf.c を C++ の header に
///        移した物。
///
/// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
/// Developed at SunPro, a Sun Microsystems, Inc. business.
/// Permission to use, copy, modify, and distribute this software is freely granted, provided that
/// this notice is preserved.
///
/// Copyright 1992-2011 The FreeBSD Project. All rights reserved. (2-clause BSD、全文は
/// THIRD_PARTY_NOTICES.txt の openlibm の項)
///
/// 元の C との違い: 浮動小数の例外フラグを立てる式を省き、overflow / underflow は inf / 0 (符号つき)
/// を直接返す。
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include <mitiru/math/detail/DMathBits.hpp>

namespace mitiru::math::detail
{

MITIRU_DMATH_FP_STRICT_BEGIN

namespace powk
{
inline constexpr float bp[] = {1.0f, 1.5f};
inline constexpr float dp_h[] = {0.0f, 5.84960938e-01f};
inline constexpr float dp_l[] = {0.0f, 1.56322085e-06f};
inline constexpr float L1 = 6.0000002384e-01f, L2 = 4.2857143283e-01f, L3 = 3.3333334327e-01f;
inline constexpr float L4 = 2.7272811532e-01f, L5 = 2.3066075146e-01f, L6 = 2.0697501302e-01f;
inline constexpr float P1 = 1.6666667163e-01f, P2 = -2.7777778450e-03f, P3 = 6.6137559770e-05f;
inline constexpr float P4 = -1.6533901999e-06f, P5 = 4.1381369442e-08f;
inline constexpr float lg2 = 6.9314718246e-01f, lg2_h = 6.93145752e-01f, lg2_l = 1.42860654e-06f;
inline constexpr float ovt = 4.2995665694e-08f;
inline constexpr float cp = 9.6179670095e-01f, cp_h = 9.6191406250e-01f, cp_l = -1.1736857402e-04f;
inline constexpr float ivln2 = 1.4426950216e+00f, ivln2_h = 1.4426879883e+00f, ivln2_l = 7.0526075433e-06f;
inline constexpr float inf = std::numeric_limits<float>::infinity();
inline constexpr float nanv = std::numeric_limits<float>::quiet_NaN();

/// 上位 20 bit ほど (下位 12 bit を 0) に切る
inline float hiPart(float v, std::int32_t mask = static_cast<std::int32_t>(0xfffff000))
{
	return floatOf(bitsOf(v) & mask);
}

/// |y| が大きいとき、x が 1 に近いので log2(x) を級数で求める。t1 + t2 が結果
inline void log2NearOne(float ax, float& t1, float& t2)
{
	const float t = ax - 1.0f;
	const float w = (t * t) * (0.5f - t * (3.33333343e-01f - t * 0.25f));
	const float u = ivln2_h * t;
	const float v = t * ivln2_l - w * ivln2;
	t1 = hiPart(u + v);
	t2 = v - (t1 - u);
}

/// 通常の |y| のとき log2(ax) を t1 + t2 に分けて求める (ax は正の有限値)
inline void log2General(float ax, float& t1, float& t2)
{
	int n = 0;
	std::int32_t ix = bitsOf(ax);
	if (ix < 0x00800000)
	{
		ax *= 16777216.0f;
		n -= 24;
		ix = bitsOf(ax);
	}
	n += (ix >> 23) - 0x7f;
	const std::int32_t j = ix & 0x007fffff;
	ix = j | 0x3f800000;
	int k;
	if (j <= 0x1cc471) { k = 0; }
	else if (j < 0x5db3d7) { k = 1; }
	else { k = 0; n += 1; ix -= 0x00800000; }
	ax = floatOf(ix);

	float u = ax - bp[k];
	const float v = 1.0f / (ax + bp[k]);
	const float s = u * v;
	const float s_h = hiPart(s);
	const std::int32_t is = ((ix >> 1) & 0xfffff000) | 0x20000000;
	const float t_h = floatOf(is + 0x00400000 + (k << 21));
	const float t_l = ax - (t_h - bp[k]);
	const float s_l = v * ((u - s_h * t_h) - s_h * t_l);
	float s2 = s * s;
	float r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
	r += s_l * (s_h + s);
	s2 = s_h * s_h;
	float t_h2 = hiPart(3.0f + s2 + r);
	const float t_l2 = r - ((t_h2 - 3.0f) - s2);
	u = s_h * t_h2;
	const float v2 = s_l * t_h2 + t_l2 * s;
	float p_h = hiPart(u + v2);
	const float p_l = v2 - (p_h - u);
	const float z_h = cp_h * p_h;
	const float z_l = cp_l * p_h + p_l * cp + dp_l[k];
	const float t = static_cast<float>(n);
	t1 = hiPart(((z_h + z_l) + dp_h[k]) + t);
	t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
}

/// 2 ** (p_h + p_l) を求める。範囲外 (overflow / underflow) は inf / 0 を返す。sn は結果の符号
inline float exp2Split(float y, float t1, float t2, float sn)
{
	const float y1 = hiPart(y);
	float p_l = (y - y1) * t1 + y * t2;
	float p_h = y1 * t1;
	float z = p_l + p_h;
	std::int32_t j = bitsOf(z);
	if (j > 0x43000000) { return sn * inf; }
	if (j == 0x43000000)
	{
		if (p_l + ovt > z - p_h) { return sn * inf; }
	}
	else if ((j & 0x7fffffff) > 0x43160000) { return sn * 0.0f; }
	else if (j == static_cast<std::int32_t>(0xc3160000))
	{
		if (p_l <= z - p_h) { return sn * 0.0f; }
	}
	const std::int32_t i = j & 0x7fffffff;
	int k = (i >> 23) - 0x7f;
	int n = 0;
	if (i > 0x3f000000)
	{
		n = j + (0x00800000 >> (k + 1));
		k = ((n & 0x7fffffff) >> 23) - 0x7f;
		const float t = floatOf(n & ~(0x007fffff >> k));
		n = ((n & 0x007fffff) | 0x00800000) >> (23 - k);
		if (j < 0) { n = -n; }
		p_h -= t;
	}
	float t = p_l + p_h;
	t = hiPart(t, static_cast<std::int32_t>(0xffff8000));
	const float u = t * lg2_h;
	const float v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
	z = u + v;
	const float w = v - (z - u);
	t = z * z;
	const float t3 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
	const float r = (z * t3) / (t3 - 2.0f) - (w + z * w);
	z = 1.0f - (r - z);
	j = bitsOf(z);
	j += shiftLeft(n, 23);
	if ((j >> 23) <= 0) { z = std::ldexp(z, n); }
	else { z = floatOf(j); }
	return sn * z;
}

/// y が x < 0 のときに整数か (0: 整数でない、1: 奇数、2: 偶数)
inline int yIntKind(std::int32_t iy)
{
	if (iy >= 0x4b800000) { return 2; }
	if (iy >= 0x3f800000)
	{
		const int k = (iy >> 23) - 0x7f;
		const std::int32_t j = iy >> (23 - k);
		if ((j << (23 - k)) == iy) { return 2 - (j & 1); }
	}
	return 0;
}
}  // namespace powk

inline float powImpl(float x, float y)
{
	using namespace powk;
	const std::int32_t hx = bitsOf(x);
	const std::int32_t hy = bitsOf(y);
	const std::int32_t ix = hx & 0x7fffffff;
	const std::int32_t iy = hy & 0x7fffffff;

	if (iy == 0) { return 1.0f; }
	if (hx == 0x3f800000) { return 1.0f; }
	if (ix > 0x7f800000 || iy > 0x7f800000) { return nanv; }

	const int yisint = hx < 0 ? yIntKind(iy) : 0;

	if (iy == 0x7f800000)
	{
		if (ix == 0x3f800000) { return 1.0f; }
		if (ix > 0x3f800000) { return hy >= 0 ? y : 0.0f; }
		return hy < 0 ? -y : 0.0f;
	}
	if (iy == 0x3f800000) { return hy < 0 ? 1.0f / x : x; }
	if (hy == 0x40000000) { return x * x; }
	if (hy == 0x3f000000 && hx >= 0) { return std::sqrt(x); }

	float ax = std::fabs(x);
	if (ix == 0x7f800000 || ix == 0 || ix == 0x3f800000)
	{
		float z = ax;
		if (hy < 0) { z = 1.0f / z; }
		if (hx < 0)
		{
			if (((ix - 0x3f800000) | yisint) == 0) { return nanv; }
			if (yisint == 1) { z = -z; }
		}
		return z;
	}

	std::int32_t n = static_cast<std::int32_t>(static_cast<std::uint32_t>(hx) >> 31) - 1;
	if ((n | yisint) == 0) { return nanv; }
	const float sn = (n | (yisint - 1)) == 0 ? -1.0f : 1.0f;

	float t1;
	float t2;
	if (iy > 0x4d000000)
	{
		if (ix < 0x3f7ffff6) { return hy < 0 ? sn * inf : sn * 0.0f; }
		if (ix > 0x3f800007) { return hy > 0 ? sn * inf : sn * 0.0f; }
		log2NearOne(ax, t1, t2);
	}
	else
	{
		log2General(ax, t1, t2);
	}
	return exp2Split(y, t1, t2, sn);
}

MITIRU_DMATH_FP_STRICT_END

}  // namespace mitiru::math::detail
