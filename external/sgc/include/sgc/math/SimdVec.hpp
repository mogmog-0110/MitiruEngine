#pragma once

/// @file SimdVec.hpp
/// @brief SIMD最適化ベクトル演算（オプショナル）
///
/// SSE2が利用可能な環境ではSIMD命令を使用し、
/// それ以外ではスカラーフォールバックを提供する。
/// 既存のVec/Mat型を置き換えず、オプトイン方式で使用する。
///
/// @code
/// sgc::simd::Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
/// sgc::simd::Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
/// auto c = a + b;
/// float d = sgc::simd::dot(a, b);
/// @endcode

#include <cmath>
#include <cstddef>

// SIMD検出
#if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)))
	#define SGC_HAS_SSE2 1
	#include <emmintrin.h>
	#include <xmmintrin.h>
#else
	#define SGC_HAS_SSE2 0
#endif

namespace sgc::simd
{

/// @brief SIMD最適化4要素ベクトル
///
/// SSE2が利用可能ならアライメント16バイトで__m128を使用し、
/// そうでなければスカラーフォールバックを使用する。
class alignas(16) Vec4
{
public:
	/// @brief ゼロベクトルを構築する
	Vec4() noexcept
	{
#if SGC_HAS_SSE2
		m_data = _mm_setzero_ps();
#else
		m_v[0] = m_v[1] = m_v[2] = m_v[3] = 0.0f;
#endif
	}

	/// @brief 4要素を指定して構築する
	/// @param x X要素
	/// @param y Y要素
	/// @param z Z要素
	/// @param w W要素
	Vec4(float x, float y, float z, float w) noexcept
	{
#if SGC_HAS_SSE2
		m_data = _mm_set_ps(w, z, y, x);
#else
		m_v[0] = x;
		m_v[1] = y;
		m_v[2] = z;
		m_v[3] = w;
#endif
	}

	/// @brief 全要素を同じ値で構築する
	/// @param value 全要素に設定する値
	explicit Vec4(float value) noexcept
	{
#if SGC_HAS_SSE2
		m_data = _mm_set1_ps(value);
#else
		m_v[0] = m_v[1] = m_v[2] = m_v[3] = value;
#endif
	}

#if SGC_HAS_SSE2
	/// @brief __m128から構築する（SSE用）
	/// @param data SSEレジスタ値
	explicit Vec4(__m128 data) noexcept
		: m_data(data)
	{
	}
#endif

	/// @brief 要素にアクセスする
	/// @param i インデックス（0-3）
	/// @return 要素の値
	[[nodiscard]] float operator[](std::size_t i) const noexcept
	{
#if SGC_HAS_SSE2
		alignas(16) float tmp[4];
		_mm_store_ps(tmp, m_data);
		return tmp[i];
#else
		return m_v[i];
#endif
	}

	/// @brief X要素を取得する
	[[nodiscard]] float x() const noexcept { return (*this)[0]; }
	/// @brief Y要素を取得する
	[[nodiscard]] float y() const noexcept { return (*this)[1]; }
	/// @brief Z要素を取得する
	[[nodiscard]] float z() const noexcept { return (*this)[2]; }
	/// @brief W要素を取得する
	[[nodiscard]] float w() const noexcept { return (*this)[3]; }

	/// @brief ベクトル加算
	[[nodiscard]] Vec4 operator+(const Vec4& other) const noexcept
	{
#if SGC_HAS_SSE2
		return Vec4{_mm_add_ps(m_data, other.m_data)};
#else
		return Vec4{m_v[0] + other.m_v[0], m_v[1] + other.m_v[1],
			m_v[2] + other.m_v[2], m_v[3] + other.m_v[3]};
#endif
	}

	/// @brief ベクトル減算
	[[nodiscard]] Vec4 operator-(const Vec4& other) const noexcept
	{
#if SGC_HAS_SSE2
		return Vec4{_mm_sub_ps(m_data, other.m_data)};
#else
		return Vec4{m_v[0] - other.m_v[0], m_v[1] - other.m_v[1],
			m_v[2] - other.m_v[2], m_v[3] - other.m_v[3]};
#endif
	}

	/// @brief 要素ごとの乗算
	[[nodiscard]] Vec4 operator*(const Vec4& other) const noexcept
	{
#if SGC_HAS_SSE2
		return Vec4{_mm_mul_ps(m_data, other.m_data)};
#else
		return Vec4{m_v[0] * other.m_v[0], m_v[1] * other.m_v[1],
			m_v[2] * other.m_v[2], m_v[3] * other.m_v[3]};
#endif
	}

	/// @brief スカラー乗算
	[[nodiscard]] Vec4 operator*(float scalar) const noexcept
	{
#if SGC_HAS_SSE2
		return Vec4{_mm_mul_ps(m_data, _mm_set1_ps(scalar))};
#else
		return Vec4{m_v[0] * scalar, m_v[1] * scalar,
			m_v[2] * scalar, m_v[3] * scalar};
#endif
	}

	/// @brief 加算代入
	Vec4& operator+=(const Vec4& other) noexcept
	{
#if SGC_HAS_SSE2
		m_data = _mm_add_ps(m_data, other.m_data);
#else
		m_v[0] += other.m_v[0]; m_v[1] += other.m_v[1];
		m_v[2] += other.m_v[2]; m_v[3] += other.m_v[3];
#endif
		return *this;
	}

	/// @brief 減算代入
	Vec4& operator-=(const Vec4& other) noexcept
	{
#if SGC_HAS_SSE2
		m_data = _mm_sub_ps(m_data, other.m_data);
#else
		m_v[0] -= other.m_v[0]; m_v[1] -= other.m_v[1];
		m_v[2] -= other.m_v[2]; m_v[3] -= other.m_v[3];
#endif
		return *this;
	}

#if SGC_HAS_SSE2
	/// @brief SSEレジスタを取得する
	/// @return __m128値
	[[nodiscard]] __m128 raw() const noexcept { return m_data; }
#endif

private:
#if SGC_HAS_SSE2
	__m128 m_data;      ///< SSEレジスタ
#else
	float m_v[4];       ///< スカラーフォールバック
#endif
};

/// @brief 内積を計算する
/// @param a 左辺ベクトル
/// @param b 右辺ベクトル
/// @return 内積の値
[[nodiscard]] inline float dot(const Vec4& a, const Vec4& b) noexcept
{
#if SGC_HAS_SSE2
	__m128 mul = _mm_mul_ps(a.raw(), b.raw());
	// 水平加算: SSE2では_mm_hadd_psがないのでシャッフルで実装
	__m128 shuf = _mm_shuffle_ps(mul, mul, _MM_SHUFFLE(2, 3, 0, 1));
	__m128 sums = _mm_add_ps(mul, shuf);
	shuf = _mm_movehl_ps(shuf, sums);
	sums = _mm_add_ss(sums, shuf);
	float result;
	_mm_store_ss(&result, sums);
	return result;
#else
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
#endif
}

/// @brief ベクトルの長さの2乗を計算する
/// @param v ベクトル
/// @return 長さの2乗
[[nodiscard]] inline float lengthSquared(const Vec4& v) noexcept
{
	return dot(v, v);
}

/// @brief ベクトルの長さを計算する
/// @param v ベクトル
/// @return 長さ
[[nodiscard]] inline float length(const Vec4& v) noexcept
{
	return std::sqrt(lengthSquared(v));
}

/// @brief 正規化ベクトルを計算する
/// @param v ベクトル
/// @return 正規化されたベクトル（ゼロベクトルの場合はゼロを返す）
[[nodiscard]] inline Vec4 normalize(const Vec4& v) noexcept
{
	const float len = length(v);
	if (len < 1e-8f) return Vec4{};
	return v * (1.0f / len);
}

/// @brief スカラー × ベクトル乗算
/// @param scalar スカラー値
/// @param v ベクトル
/// @return 結果ベクトル
[[nodiscard]] inline Vec4 operator*(float scalar, const Vec4& v) noexcept
{
	return v * scalar;
}

} // namespace sgc::simd
