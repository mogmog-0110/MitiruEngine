#pragma once

/// @file Noise.hpp
/// @brief パーリンノイズ実装
///
/// Ken Perlinの改良版パーリンノイズ。
/// 1D/2D/3Dノイズとオクターブ（フラクタル）ノイズを提供する。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>

#include "sgc/types/Concepts.hpp"

namespace sgc
{

/// @brief パーリンノイズ生成器
///
/// @code
/// sgc::PerlinNoise noise(42);
/// float val = noise.noise2D(x * 0.01f, y * 0.01f);  // [-1, 1]
/// float terrain = noise.octave2D(x * 0.005f, y * 0.005f, 6, 0.5f);
/// @endcode
class PerlinNoise
{
public:
	/// @brief デフォルトシードで構築する
	PerlinNoise()
	{
		reseed(0);
	}

	/// @brief 指定シードで構築する
	/// @param seed シード値
	explicit PerlinNoise(std::uint32_t seed)
	{
		reseed(seed);
	}

	/// @brief シードを再設定する
	/// @param seed 新しいシード値
	void reseed(std::uint32_t seed)
	{
		// 順列テーブル初期化
		for (int i = 0; i < 256; ++i)
		{
			m_perm[i] = static_cast<std::uint8_t>(i);
		}

		// Fisher-Yates shuffle
		std::uint32_t s = seed;
		for (int i = 255; i > 0; --i)
		{
			s = s * 1664525u + 1013904223u;  // LCG
			const int j = static_cast<int>((s >> 16) % static_cast<std::uint32_t>(i + 1));
			const auto tmp = m_perm[i];
			m_perm[i] = m_perm[j];
			m_perm[j] = tmp;
		}

		// テーブルを複製して参照を簡略化
		for (int i = 0; i < 256; ++i)
		{
			m_perm[256 + i] = m_perm[i];
		}
	}

	/// @brief 1Dパーリンノイズ [-1, 1]
	/// @param x X座標
	template <FloatingPoint T>
	[[nodiscard]] T noise1D(T x) const noexcept
	{
		const int xi = floorToInt(x) & 255;
		const T xf = x - std::floor(x);
		const T u = fade(xf);
		return lerpNoise(u, grad1D(m_perm[xi], xf), grad1D(m_perm[xi + 1], xf - T{1}));
	}

	/// @brief 2Dパーリンノイズ [-1, 1]
	/// @param x X座標
	/// @param y Y座標
	template <FloatingPoint T>
	[[nodiscard]] T noise2D(T x, T y) const noexcept
	{
		const int xi = floorToInt(x) & 255;
		const int yi = floorToInt(y) & 255;
		const T xf = x - std::floor(x);
		const T yf = y - std::floor(y);
		const T u = fade(xf);
		const T v = fade(yf);

		const int aa = m_perm[m_perm[xi] + yi];
		const int ab = m_perm[m_perm[xi] + yi + 1];
		const int ba = m_perm[m_perm[xi + 1] + yi];
		const int bb = m_perm[m_perm[xi + 1] + yi + 1];

		return lerpNoise(v,
			lerpNoise(u, grad2D(aa, xf, yf), grad2D(ba, xf - T{1}, yf)),
			lerpNoise(u, grad2D(ab, xf, yf - T{1}), grad2D(bb, xf - T{1}, yf - T{1})));
	}

	/// @brief 3Dパーリンノイズ [-1, 1]
	/// @param x X座標
	/// @param y Y座標
	/// @param z Z座標
	template <FloatingPoint T>
	[[nodiscard]] T noise3D(T x, T y, T z) const noexcept
	{
		const int xi = floorToInt(x) & 255;
		const int yi = floorToInt(y) & 255;
		const int zi = floorToInt(z) & 255;
		const T xf = x - std::floor(x);
		const T yf = y - std::floor(y);
		const T zf = z - std::floor(z);
		const T u = fade(xf);
		const T v = fade(yf);
		const T w = fade(zf);

		const int a  = m_perm[xi] + yi;
		const int aa = m_perm[a] + zi;
		const int ab = m_perm[a + 1] + zi;
		const int b  = m_perm[xi + 1] + yi;
		const int ba = m_perm[b] + zi;
		const int bb = m_perm[b + 1] + zi;

		return lerpNoise(w,
			lerpNoise(v,
				lerpNoise(u, grad3D(m_perm[aa], xf, yf, zf),
				              grad3D(m_perm[ba], xf - T{1}, yf, zf)),
				lerpNoise(u, grad3D(m_perm[ab], xf, yf - T{1}, zf),
				              grad3D(m_perm[bb], xf - T{1}, yf - T{1}, zf))),
			lerpNoise(v,
				lerpNoise(u, grad3D(m_perm[aa + 1], xf, yf, zf - T{1}),
				              grad3D(m_perm[ba + 1], xf - T{1}, yf, zf - T{1})),
				lerpNoise(u, grad3D(m_perm[ab + 1], xf, yf - T{1}, zf - T{1}),
				              grad3D(m_perm[bb + 1], xf - T{1}, yf - T{1}, zf - T{1}))));
	}

	/// @brief 2Dノイズ [0, 1] 範囲
	template <FloatingPoint T>
	[[nodiscard]] T noise2D_01(T x, T y) const noexcept
	{
		return (noise2D(x, y) + T{1}) / T{2};
	}

	/// @brief 3Dノイズ [0, 1] 範囲
	template <FloatingPoint T>
	[[nodiscard]] T noise3D_01(T x, T y, T z) const noexcept
	{
		return (noise3D(x, y, z) + T{1}) / T{2};
	}

	/// @brief 2Dオクターブ（フラクタル）ノイズ
	/// @param x X座標
	/// @param y Y座標
	/// @param octaves オクターブ数
	/// @param persistence 永続性（各オクターブでの振幅減衰率）
	template <FloatingPoint T>
	[[nodiscard]] T octave2D(T x, T y, int octaves, T persistence = T{0.5}) const noexcept
	{
		T total = T{0};
		T frequency = T{1};
		T amplitude = T{1};
		T maxValue = T{0};

		for (int i = 0; i < octaves; ++i)
		{
			total += noise2D(x * frequency, y * frequency) * amplitude;
			maxValue += amplitude;
			amplitude *= persistence;
			frequency *= T{2};
		}

		return total / maxValue;
	}

	/// @brief 3Dオクターブ（フラクタル）ノイズ
	template <FloatingPoint T>
	[[nodiscard]] T octave3D(T x, T y, T z, int octaves, T persistence = T{0.5}) const noexcept
	{
		T total = T{0};
		T frequency = T{1};
		T amplitude = T{1};
		T maxValue = T{0};

		for (int i = 0; i < octaves; ++i)
		{
			total += noise3D(x * frequency, y * frequency, z * frequency) * amplitude;
			maxValue += amplitude;
			amplitude *= persistence;
			frequency *= T{2};
		}

		return total / maxValue;
	}

private:
	std::array<int, 512> m_perm;  ///< 順列テーブル（256要素 x 2）

	/// @brief Ken Perlinの5次フェード関数
	template <FloatingPoint T>
	[[nodiscard]] static T fade(T t) noexcept
	{
		return t * t * t * (t * (t * T{6} - T{15}) + T{10});
	}

	/// @brief 線形補間
	template <FloatingPoint T>
	[[nodiscard]] static T lerpNoise(T t, T a, T b) noexcept
	{
		return a + t * (b - a);
	}

	/// @brief 1D勾配関数
	template <FloatingPoint T>
	[[nodiscard]] static T grad1D(int hash, T x) noexcept
	{
		return (hash & 1) ? -x : x;
	}

	/// @brief 2D勾配関数
	template <FloatingPoint T>
	[[nodiscard]] static T grad2D(int hash, T x, T y) noexcept
	{
		const int h = hash & 3;
		return ((h & 1) ? -x : x) + ((h & 2) ? -y : y);
	}

	/// @brief 3D勾配関数（Ken Perlin改良版）
	template <FloatingPoint T>
	[[nodiscard]] static T grad3D(int hash, T x, T y, T z) noexcept
	{
		const int h = hash & 15;
		const T u = (h < 8) ? x : y;
		const T v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
		return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
	}

	/// @brief 小数部を取るためのfloor→int変換
	[[nodiscard]] static int floorToInt(double x) noexcept
	{
		const int xi = static_cast<int>(x);
		return (x < xi) ? xi - 1 : xi;
	}

	[[nodiscard]] static int floorToInt(float x) noexcept
	{
		const int xi = static_cast<int>(x);
		return (x < xi) ? xi - 1 : xi;
	}
};

} // namespace sgc
