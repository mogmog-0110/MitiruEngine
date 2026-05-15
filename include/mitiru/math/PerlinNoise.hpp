#pragma once

/// @file PerlinNoise.hpp
/// @brief パーリンノイズ生成器（Siv3D PerlinNoise風）
/// @details Ken Perlinの改良ノイズアルゴリズムを実装。
///          2D/3Dノイズとオクターブ合成をサポートする。
///
/// @code
/// mitiru::math::PerlinNoise noise(42);
/// float val = noise.noise2D(1.5f, 2.3f);       // [-1, 1]
/// float oct = noise.octave2D(x, y, 6, 0.5f);   // オクターブ合成
/// float n01 = noise.noise2D01(x, y);            // [0, 1]
/// @endcode

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>

namespace mitiru::math
{

/// @brief パーリンノイズ生成器
/// @details シードによる再現性のあるノイズを生成する。
///          2Dおよび3Dのノイズ関数とオクターブ合成を提供する。
class PerlinNoise
{
public:
	/// @brief コンストラクタ
	/// @param seed 乱数シード
	explicit PerlinNoise(uint32_t seed = 0)
	{
		std::iota(m_p.begin(), m_p.begin() + 256, 0);

		// Fisher-Yatesシャッフル（LCG使用）
		uint32_t rng = seed;
		for (int i = 255; i > 0; --i)
		{
			rng = rng * 1103515245u + 12345u;
			const int j = static_cast<int>((rng >> 16) % static_cast<uint32_t>(i + 1));
			std::swap(m_p[i], m_p[j]);
		}

		// テーブルを複製する
		for (int i = 0; i < 256; ++i)
		{
			m_p[256 + i] = m_p[i];
		}
	}

	/// @brief 2Dパーリンノイズ値を取得する
	/// @param x X座標
	/// @param y Y座標
	/// @return ノイズ値 [-1, 1]
	[[nodiscard]] float noise2D(float x, float y) const noexcept
	{
		const int xi = static_cast<int>(std::floor(x)) & 255;
		const int yi = static_cast<int>(std::floor(y)) & 255;
		const float xf = x - std::floor(x);
		const float yf = y - std::floor(y);
		const float u = fade(xf);
		const float v = fade(yf);

		const int aa = m_p[m_p[xi] + yi];
		const int ab = m_p[m_p[xi] + yi + 1];
		const int ba = m_p[m_p[xi + 1] + yi];
		const int bb = m_p[m_p[xi + 1] + yi + 1];

		return lerp(v,
			lerp(u, grad2D(aa, xf, yf), grad2D(ba, xf - 1, yf)),
			lerp(u, grad2D(ab, xf, yf - 1), grad2D(bb, xf - 1, yf - 1)));
	}

	/// @brief 2Dパーリンノイズ値を[0,1]で取得する
	/// @param x X座標
	/// @param y Y座標
	/// @return ノイズ値 [0, 1]
	[[nodiscard]] float noise2D01(float x, float y) const noexcept
	{
		return (noise2D(x, y) + 1.0f) * 0.5f;
	}

	/// @brief 3Dパーリンノイズ値を取得する
	/// @param x X座標
	/// @param y Y座標
	/// @param z Z座標
	/// @return ノイズ値 [-1, 1]
	[[nodiscard]] float noise3D(float x, float y, float z) const noexcept
	{
		const int xi = static_cast<int>(std::floor(x)) & 255;
		const int yi = static_cast<int>(std::floor(y)) & 255;
		const int zi = static_cast<int>(std::floor(z)) & 255;
		const float xf = x - std::floor(x);
		const float yf = y - std::floor(y);
		const float zf = z - std::floor(z);
		const float u = fade(xf);
		const float v = fade(yf);
		const float w = fade(zf);

		const int a  = m_p[xi] + yi;
		const int aa = m_p[a] + zi;
		const int ab = m_p[a + 1] + zi;
		const int b  = m_p[xi + 1] + yi;
		const int ba = m_p[b] + zi;
		const int bb = m_p[b + 1] + zi;

		return lerp(w,
			lerp(v,
				lerp(u, grad3D(m_p[aa], xf, yf, zf),
				        grad3D(m_p[ba], xf - 1, yf, zf)),
				lerp(u, grad3D(m_p[ab], xf, yf - 1, zf),
				        grad3D(m_p[bb], xf - 1, yf - 1, zf))),
			lerp(v,
				lerp(u, grad3D(m_p[aa + 1], xf, yf, zf - 1),
				        grad3D(m_p[ba + 1], xf - 1, yf, zf - 1)),
				lerp(u, grad3D(m_p[ab + 1], xf, yf - 1, zf - 1),
				        grad3D(m_p[bb + 1], xf - 1, yf - 1, zf - 1))));
	}

	/// @brief オクターブ付き2Dノイズを取得する
	/// @param x X座標
	/// @param y Y座標
	/// @param octaves オクターブ数
	/// @param persistence 振幅減衰率（0.5が一般的）
	/// @return 正規化されたノイズ値 [-1, 1]
	[[nodiscard]] float octave2D(float x, float y, int octaves,
		float persistence = 0.5f) const noexcept
	{
		float total = 0;
		float amplitude = 1;
		float frequency = 1;
		float maxVal = 0;
		for (int i = 0; i < octaves; ++i)
		{
			total += noise2D(x * frequency, y * frequency) * amplitude;
			maxVal += amplitude;
			amplitude *= persistence;
			frequency *= 2;
		}
		return total / maxVal;
	}

	/// @brief オクターブ付き3Dノイズを取得する
	/// @param x X座標
	/// @param y Y座標
	/// @param z Z座標
	/// @param octaves オクターブ数
	/// @param persistence 振幅減衰率
	/// @return 正規化されたノイズ値 [-1, 1]
	[[nodiscard]] float octave3D(float x, float y, float z, int octaves,
		float persistence = 0.5f) const noexcept
	{
		float total = 0;
		float amplitude = 1;
		float frequency = 1;
		float maxVal = 0;
		for (int i = 0; i < octaves; ++i)
		{
			total += noise3D(x * frequency, y * frequency, z * frequency) * amplitude;
			maxVal += amplitude;
			amplitude *= persistence;
			frequency *= 2;
		}
		return total / maxVal;
	}

private:
	/// @brief 5次エルミート補間
	static float fade(float t) noexcept
	{
		return t * t * t * (t * (t * 6 - 15) + 10);
	}

	/// @brief 線形補間
	static float lerp(float t, float a, float b) noexcept
	{
		return a + t * (b - a);
	}

	/// @brief 2D勾配関数
	static float grad2D(int hash, float x, float y) noexcept
	{
		const int h = hash & 3;
		const float u = (h < 2) ? x : y;
		const float v = (h < 2) ? y : x;
		return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
	}

	/// @brief 3D勾配関数
	static float grad3D(int hash, float x, float y, float z) noexcept
	{
		const int h = hash & 15;
		const float u = (h < 8) ? x : y;
		const float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
		return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
	}

	std::array<int, 512> m_p{}; ///< 順列テーブル（256要素を2回繰り返す）
};

} // namespace mitiru::math
