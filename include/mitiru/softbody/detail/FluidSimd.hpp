#pragma once

/// @file FluidSimd.hpp
/// @brief 4 つの float をまとめて計算する型 (SSE2。無い環境では同じ演算を 1 つずつ行う)。
/// @details どちらの実装も各レーンで同じ IEEE の演算 (加減乗除と平方根) だけを行い、4 レーンの合計は
///          (0 + 1) + (2 + 3) の順に決めてあるので、SSE2 の有無で結果のビットは変わらない。積和をまとめる命令は使わない。

#if defined(_M_X64) || defined(__SSE2__)
#	include <emmintrin.h>
#	define MITIRU_FLUID_SSE2 1
#else
#	include <cmath>
#endif

namespace mitiru::softbody::detail
{

#if defined(MITIRU_FLUID_SSE2)

struct F4
{
	__m128 v;
	static F4 set(float a, float b, float c, float d) noexcept { return {_mm_setr_ps(a, b, c, d)}; }
	static F4 splat(float a) noexcept { return {_mm_set1_ps(a)}; }
	static F4 load(const float* p) noexcept { return {_mm_loadu_ps(p)}; }
	friend F4 operator+(F4 a, F4 b) noexcept { return {_mm_add_ps(a.v, b.v)}; }
	friend F4 operator-(F4 a, F4 b) noexcept { return {_mm_sub_ps(a.v, b.v)}; }
	friend F4 operator*(F4 a, F4 b) noexcept { return {_mm_mul_ps(a.v, b.v)}; }
	friend F4 operator/(F4 a, F4 b) noexcept { return {_mm_div_ps(a.v, b.v)}; }
	friend F4 sqrt(F4 a) noexcept { return {_mm_sqrt_ps(a.v)}; }
	friend F4 max(F4 a, F4 b) noexcept { return {_mm_max_ps(a.v, b.v)}; }
	friend F4 min(F4 a, F4 b) noexcept { return {_mm_min_ps(a.v, b.v)}; }
	/// a < b のレーンは x、それ以外は y
	friend F4 ifLess(F4 a, F4 b, F4 x, F4 y) noexcept
	{
		const __m128 m = _mm_cmplt_ps(a.v, b.v);
		return {_mm_or_ps(_mm_and_ps(m, x.v), _mm_andnot_ps(m, y.v))};
	}
	float sum() const noexcept
	{
		alignas(16) float l[4];
		_mm_store_ps(l, v);
		return (l[0] + l[1]) + (l[2] + l[3]);
	}
	void store(float* out) const noexcept { _mm_storeu_ps(out, v); }
	friend F4 abs(F4 a) noexcept { return {_mm_andnot_ps(_mm_set1_ps(-0.0f), a.v)}; }
	/// fastLog2 と同じ演算の並び
	friend F4 log2(F4 x) noexcept
	{
		const __m128i bits = _mm_castps_si128(x.v);
		__m128i e = _mm_sub_epi32(_mm_and_si128(_mm_srli_epi32(bits, 23), _mm_set1_epi32(0xFF)), _mm_set1_epi32(127));
		__m128 m = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(bits, _mm_set1_epi32(0x007FFFFF)), _mm_set1_epi32(0x3F800000)));
		const __m128 big = _mm_cmpgt_ps(m, _mm_set1_ps(1.41421356f));
		m = _mm_or_ps(_mm_and_ps(big, _mm_mul_ps(m, _mm_set1_ps(0.5f))), _mm_andnot_ps(big, m));
		e = _mm_sub_epi32(e, _mm_castps_si128(big));
		const F4 mm{m};
		const F4 t = (mm - splat(1.0f)) / (mm + splat(1.0f));
		const F4 t2 = t * t;
		const F4 series = t * (splat(2.0f) + t2 * (splat(2.0f / 3.0f) + t2 * (splat(2.0f / 5.0f) + t2 * splat(2.0f / 7.0f))));
		return F4{_mm_cvtepi32_ps(e)} + series * splat(1.44269504f);
	}
	/// fastExp2 と同じ演算の並び
	friend F4 exp2(F4 y) noexcept
	{
		const __m128 low = _mm_cmplt_ps(y.v, _mm_set1_ps(-126.0f));
		const __m128 yc = _mm_min_ps(y.v, _mm_set1_ps(127.0f));
		const __m128 half = _mm_add_ps(yc, _mm_set1_ps(0.5f));
		__m128i ni = _mm_cvttps_epi32(half);
		ni = _mm_add_epi32(ni, _mm_castps_si128(_mm_cmpgt_ps(_mm_cvtepi32_ps(ni), half)));
		const F4 x = (F4{yc} - F4{_mm_cvtepi32_ps(ni)}) * splat(0.693147181f);
		const F4 p = splat(1.0f) + x * (splat(1.0f) + x * (splat(0.5f) + x * (splat(1.0f / 6.0f) + x * (splat(1.0f / 24.0f) +
		             x * (splat(1.0f / 120.0f) + x * splat(1.0f / 720.0f))))));
		const __m128 scale = _mm_castsi128_ps(_mm_slli_epi32(_mm_add_epi32(ni, _mm_set1_epi32(127)), 23));
		return {_mm_andnot_ps(low, _mm_mul_ps(p.v, scale))};
	}
};

#else

struct F4
{
	float l[4];
	static F4 set(float a, float b, float c, float d) noexcept { return {{a, b, c, d}}; }
	static F4 splat(float a) noexcept { return {{a, a, a, a}}; }
	static F4 load(const float* p) noexcept { return {{p[0], p[1], p[2], p[3]}}; }
	template <class Op>
	static F4 map(F4 a, F4 b, Op op) noexcept { return {{op(a.l[0], b.l[0]), op(a.l[1], b.l[1]), op(a.l[2], b.l[2]), op(a.l[3], b.l[3])}}; }
	friend F4 operator+(F4 a, F4 b) noexcept { return map(a, b, [](float x, float y) { return x + y; }); }
	friend F4 operator-(F4 a, F4 b) noexcept { return map(a, b, [](float x, float y) { return x - y; }); }
	friend F4 operator*(F4 a, F4 b) noexcept { return map(a, b, [](float x, float y) { return x * y; }); }
	friend F4 operator/(F4 a, F4 b) noexcept { return map(a, b, [](float x, float y) { return x / y; }); }
	friend F4 sqrt(F4 a) noexcept { return {{std::sqrt(a.l[0]), std::sqrt(a.l[1]), std::sqrt(a.l[2]), std::sqrt(a.l[3])}}; }
	friend F4 max(F4 a, F4 b) noexcept { return map(a, b, [](float x, float y) { return x > y ? x : y; }); }
	friend F4 min(F4 a, F4 b) noexcept { return map(a, b, [](float x, float y) { return x < y ? x : y; }); }
	friend F4 ifLess(F4 a, F4 b, F4 x, F4 y) noexcept
	{
		return {{a.l[0] < b.l[0] ? x.l[0] : y.l[0], a.l[1] < b.l[1] ? x.l[1] : y.l[1], a.l[2] < b.l[2] ? x.l[2] : y.l[2], a.l[3] < b.l[3] ? x.l[3] : y.l[3]}};
	}
	float sum() const noexcept { return (l[0] + l[1]) + (l[2] + l[3]); }
	void store(float* out) const noexcept { for (int i = 0; i < 4; ++i) { out[i] = l[i]; } }
	friend F4 abs(F4 a) noexcept { return {{std::fabs(a.l[0]), std::fabs(a.l[1]), std::fabs(a.l[2]), std::fabs(a.l[3])}}; }
	friend F4 log2(F4 a) noexcept { return {{fastLog2(a.l[0]), fastLog2(a.l[1]), fastLog2(a.l[2]), fastLog2(a.l[3])}}; }
	friend F4 exp2(F4 a) noexcept { return {{fastExp2(a.l[0]), fastExp2(a.l[1]), fastExp2(a.l[2]), fastExp2(a.l[3])}}; }
};

#endif

/// @brief x の e 乗 (x ≥ 0、fastPow と同じ)
[[nodiscard]] inline F4 pow4(F4 x, float e) noexcept
{
	return ifLess(x, F4::splat(1e-30f), F4::splat(0.0f), exp2(F4::splat(e) * log2(x)));
}

[[nodiscard]] inline F4 clamp01x4(F4 x) noexcept { return min(max(x, F4::splat(0.0f)), F4::splat(1.0f)); }

/// @brief 3 本の F4 (x, y, z)
struct F4x3
{
	F4 x, y, z;
};

/// @brief 粒の番号 j[0..3] の位置を SoA の配列から 4 レーンへ集める
[[nodiscard]] inline F4x3 fluidGather(const float* px, const float* py, const float* pz, const int (&j)[4]) noexcept
{
	return {F4::set(px[j[0]], px[j[1]], px[j[2]], px[j[3]]), F4::set(py[j[0]], py[j[1]], py[j[2]], py[j[3]]),
	        F4::set(pz[j[0]], pz[j[1]], pz[j[2]], pz[j[3]])};
}

} // namespace mitiru::softbody::detail
