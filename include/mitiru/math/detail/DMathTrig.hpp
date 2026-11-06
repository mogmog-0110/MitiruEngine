/// @file DMathTrig.hpp
/// @brief 決定論の sin / cos / tan / atan / atan2 (float)。openlibm (FreeBSD msun、FDLIBM 由来) の
///        s_sinf.c、s_cosf.c、s_tanf.c、k_sinf.c、k_cosf.c、k_tanf.c、e_rem_pio2f.c、s_atanf.c、
///        e_atan2f.c を C++ の header に移した物。
///
/// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
/// Developed at SunPro, a Sun Microsystems, Inc. business.
/// Permission to use, copy, modify, and distribute this software is freely granted, provided that
/// this notice is preserved.
///
/// Copyright 1992-2011 The FreeBSD Project. All rights reserved. (2-clause BSD、全文は
/// THIRD_PARTY_NOTICES.txt の openlibm の項)。kernel の最適化は Bruce D. Evans、
/// float への変換は Ian Lance Taylor。
///
/// 元の C との違い: 浮動小数の例外フラグを立てる式 (huge + x > one など) を省き、|x| が約 4.2e8
/// 以上の引数の reduction は k_rem_pio2 を持たず fmod で行う (決定論だが精度は保証しない)。
#pragma once

#include <cmath>
#include <cstdint>

#include <mitiru/math/detail/DMathBits.hpp>

namespace mitiru::math::detail
{

MITIRU_DMATH_FP_STRICT_BEGIN

inline constexpr double kPio2 = 1.57079632679489655800e+00;  // 0x3FF921FB54442D18

inline double kernelSinD(double x)
{
	constexpr double S1 = -0x15555554cbac77.0p-55;
	constexpr double S2 = 0x111110896efbb2.0p-59;
	constexpr double S3 = -0x1a00f9e2cae774.0p-65;
	constexpr double S4 = 0x16cd878c3b46a7.0p-71;
	const double z = x * x;
	const double w = z * z;
	const double r = S3 + z * S4;
	const double s = z * x;
	return (x + s * (S1 + z * S2)) + s * w * r;
}

inline double kernelCosD(double x)
{
	constexpr double C0 = -0x1ffffffd0c5e81.0p-54;
	constexpr double C1 = 0x155553e1053a42.0p-57;
	constexpr double C2 = -0x16c087e80f1e27.0p-62;
	constexpr double C3 = 0x199342e0ee5069.0p-68;
	const double z = x * x;
	const double w = z * z;
	const double r = C2 + z * C3;
	return ((1.0 + z * C0) + w * C1) + (w * z) * r;
}

inline double kernelTanD(double x, int iy)
{
	constexpr double T0 = 0x15554d3418c99f.0p-54;
	constexpr double T1 = 0x1112fd38999f72.0p-55;
	constexpr double T2 = 0x1b54c91d865afe.0p-57;
	constexpr double T3 = 0x191df3908c33ce.0p-58;
	constexpr double T4 = 0x185dadfcecf44e.0p-61;
	constexpr double T5 = 0x1362b9bf971bcd.0p-59;
	const double z = x * x;
	double r = T4 + z * T5;
	const double t = T2 + z * T3;
	const double w = z * z;
	const double s = z * x;
	const double u = T0 + z * T1;
	r = (x + s * u) + (s * w) * (t + w * r);
	return iy == 1 ? r : -1.0 / r;
}

/// x を pi/2 の倍数で割った余りを *y に、商の下位ビットを返す。|x| が大きいときは fmod による近似
inline int remPio2(float x, double* y)
{
	constexpr double invpio2 = 6.36619772367581382433e-01;
	constexpr double pio2_1 = 1.57079631090164184570e+00;
	constexpr double pio2_1t = 1.58932547735281966916e-08;
	const std::int32_t ix = bitsOf(x) & 0x7fffffff;
	if (ix < 0x4dc90fdb)
	{
		double fn = static_cast<double>(x) * invpio2 + 0x1.8p52;
		fn = fn - 0x1.8p52;
		const int n = static_cast<int>(fn);
		const double r = static_cast<double>(x) - fn * pio2_1;
		const double w = fn * pio2_1t;
		*y = r - w;
		return n;
	}
	const double m = std::fmod(static_cast<double>(x), 4.0 * kPio2);
	const double fn = std::floor(m * invpio2 + 0.5);
	*y = (m - fn * pio2_1) - fn * pio2_1t;
	return static_cast<int>(fn) & 3;
}

inline float sinImpl(float x)
{
	const std::int32_t hx = bitsOf(x);
	const std::int32_t ix = hx & 0x7fffffff;
	if (ix <= 0x3f490fda)
	{
		if (ix < 0x39800000 && static_cast<int>(x) == 0) { return x; }
		return static_cast<float>(kernelSinD(x));
	}
	if (ix <= 0x407b53d1)
	{
		if (ix <= 0x4016cbe3)
		{
			return hx > 0 ? static_cast<float>(kernelCosD(x - 1 * kPio2))
			              : -static_cast<float>(kernelCosD(x + 1 * kPio2));
		}
		return static_cast<float>(kernelSinD((hx > 0 ? 2 * kPio2 : -2 * kPio2) - x));
	}
	if (ix <= 0x40e231d5)
	{
		if (ix <= 0x40afeddf)
		{
			return hx > 0 ? -static_cast<float>(kernelCosD(x - 3 * kPio2))
			              : static_cast<float>(kernelCosD(x + 3 * kPio2));
		}
		return static_cast<float>(kernelSinD(x + (hx > 0 ? -4 * kPio2 : 4 * kPio2)));
	}
	if (ix >= 0x7f800000) { return x - x; }
	double y = 0.0;
	switch (remPio2(x, &y) & 3)
	{
	case 0: return static_cast<float>(kernelSinD(y));
	case 1: return static_cast<float>(kernelCosD(y));
	case 2: return static_cast<float>(kernelSinD(-y));
	default: return -static_cast<float>(kernelCosD(y));
	}
}

inline float cosImpl(float x)
{
	const std::int32_t hx = bitsOf(x);
	const std::int32_t ix = hx & 0x7fffffff;
	if (ix <= 0x3f490fda)
	{
		if (ix < 0x39800000 && static_cast<int>(x) == 0) { return 1.0f; }
		return static_cast<float>(kernelCosD(x));
	}
	if (ix <= 0x407b53d1)
	{
		if (ix <= 0x4016cbe3)
		{
			return hx > 0 ? static_cast<float>(kernelSinD(1 * kPio2 - x))
			              : static_cast<float>(kernelSinD(x + 1 * kPio2));
		}
		return -static_cast<float>(kernelCosD(x + (hx > 0 ? -2 * kPio2 : 2 * kPio2)));
	}
	if (ix <= 0x40e231d5)
	{
		if (ix <= 0x40afeddf)
		{
			return hx > 0 ? static_cast<float>(kernelSinD(x - 3 * kPio2))
			              : static_cast<float>(kernelSinD(-3 * kPio2 - x));
		}
		return static_cast<float>(kernelCosD(x + (hx > 0 ? -4 * kPio2 : 4 * kPio2)));
	}
	if (ix >= 0x7f800000) { return x - x; }
	double y = 0.0;
	switch (remPio2(x, &y) & 3)
	{
	case 0: return static_cast<float>(kernelCosD(y));
	case 1: return static_cast<float>(kernelSinD(-y));
	case 2: return -static_cast<float>(kernelCosD(y));
	default: return static_cast<float>(kernelSinD(y));
	}
}

inline float tanImpl(float x)
{
	const std::int32_t hx = bitsOf(x);
	const std::int32_t ix = hx & 0x7fffffff;
	if (ix <= 0x3f490fda)
	{
		if (ix < 0x39800000 && static_cast<int>(x) == 0) { return x; }
		return static_cast<float>(kernelTanD(x, 1));
	}
	if (ix <= 0x407b53d1)
	{
		if (ix <= 0x4016cbe3) { return static_cast<float>(kernelTanD(x + (hx > 0 ? -1 * kPio2 : 1 * kPio2), -1)); }
		return static_cast<float>(kernelTanD(x + (hx > 0 ? -2 * kPio2 : 2 * kPio2), 1));
	}
	if (ix <= 0x40e231d5)
	{
		if (ix <= 0x40afeddf) { return static_cast<float>(kernelTanD(x + (hx > 0 ? -3 * kPio2 : 3 * kPio2), -1)); }
		return static_cast<float>(kernelTanD(x + (hx > 0 ? -4 * kPio2 : 4 * kPio2), 1));
	}
	if (ix >= 0x7f800000) { return x - x; }
	double y = 0.0;
	const int n = remPio2(x, &y);
	return static_cast<float>(kernelTanD(y, 1 - ((n & 1) << 1)));
}

inline float atanImpl(float x)
{
	constexpr float atanhi[] = {4.6364760399e-01f, 7.8539812565e-01f, 9.8279368877e-01f, 1.5707962513e+00f};
	constexpr float atanlo[] = {5.0121582440e-09f, 3.7748947079e-08f, 3.4473217170e-08f, 7.5497894159e-08f};
	constexpr float aT[] = {3.3333328366e-01f, -1.9999158382e-01f, 1.4253635705e-01f, -1.0648017377e-01f,
	                        6.1687607318e-02f};
	const std::int32_t hx = bitsOf(x);
	const std::int32_t ix = hx & 0x7fffffff;
	int id = -1;
	if (ix >= 0x4c800000)
	{
		if (ix > 0x7f800000) { return x + x; }
		return hx > 0 ? atanhi[3] + atanlo[3] : -atanhi[3] - atanlo[3];
	}
	if (ix < 0x3ee00000)
	{
		if (ix < 0x39800000) { return x; }
	}
	else
	{
		x = std::fabs(x);
		if (ix < 0x3f980000)
		{
			if (ix < 0x3f300000) { id = 0; x = (2.0f * x - 1.0f) / (2.0f + x); }
			else { id = 1; x = (x - 1.0f) / (x + 1.0f); }
		}
		else if (ix < 0x401c0000) { id = 2; x = (x - 1.5f) / (1.0f + 1.5f * x); }
		else { id = 3; x = -1.0f / x; }
	}
	float z = x * x;
	const float w = z * z;
	const float s1 = z * (aT[0] + w * (aT[2] + w * aT[4]));
	const float s2 = w * (aT[1] + w * aT[3]);
	if (id < 0) { return x - x * (s1 + s2); }
	z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
	return hx < 0 ? -z : z;
}

inline float atan2Impl(float y, float x)
{
	constexpr float pi_o_4 = 7.8539818525e-01f;
	constexpr float pi_o_2 = 1.5707963705e+00f;
	constexpr float pi = 3.1415927410e+00f;
	constexpr float pi_lo = -8.7422776573e-08f;
	const std::int32_t hx = bitsOf(x);
	const std::int32_t ix = hx & 0x7fffffff;
	const std::int32_t hy = bitsOf(y);
	const std::int32_t iy = hy & 0x7fffffff;
	if (ix > 0x7f800000 || iy > 0x7f800000) { return x + y; }
	if (hx == 0x3f800000) { return atanImpl(y); }
	std::int32_t m = ((hy >> 31) & 1) | ((hx >> 30) & 2);
	if (iy == 0)
	{
		switch (m)
		{
		case 0:
		case 1: return y;
		case 2: return pi;
		default: return -pi;
		}
	}
	if (ix == 0) { return hy < 0 ? -pi_o_2 : pi_o_2; }
	if (ix == 0x7f800000)
	{
		if (iy == 0x7f800000)
		{
			switch (m)
			{
			case 0: return pi_o_4;
			case 1: return -pi_o_4;
			case 2: return 3.0f * pi_o_4;
			default: return -3.0f * pi_o_4;
			}
		}
		switch (m)
		{
		case 0: return 0.0f;
		case 1: return -0.0f;
		case 2: return pi;
		default: return -pi;
		}
	}
	if (iy == 0x7f800000) { return hy < 0 ? -pi_o_2 : pi_o_2; }
	float z = 0.0f;
	const std::int32_t k = (iy - ix) >> 23;
	if (k > 26)
	{
		z = pi_o_2 + 0.5f * pi_lo;
		m &= 1;
	}
	else if (k < -26 && hx < 0) { z = 0.0f; }
	else { z = atanImpl(std::fabs(y / x)); }
	switch (m)
	{
	case 0: return z;
	case 1: return -z;
	case 2: return pi - (z - pi_lo);
	default: return (z - pi_lo) - pi;
	}
}

MITIRU_DMATH_FP_STRICT_END

}  // namespace mitiru::math::detail
