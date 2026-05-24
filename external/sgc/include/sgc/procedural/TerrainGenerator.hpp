#pragma once

/// @file TerrainGenerator.hpp
/// @brief ノイズベースの地形生成
///
/// パーリンノイズ風のアルゴリズムで高さマップを生成し、
/// 高さと湿度からバイオームを自動分類する。
///
/// @code
/// sgc::procedural::TerrainConfig config;
/// config.width = 128;
/// config.height = 128;
/// config.octaves = 4;
/// auto result = sgc::procedural::generateTerrain(config);
/// auto biome = sgc::procedural::classifyBiome(0.8f, 0.5f);
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sgc::procedural
{

/// @brief バイオーム種別
enum class BiomeType : uint8_t
{
	Water,    ///< 水域
	Sand,     ///< 砂地
	Grass,    ///< 草原
	Forest,   ///< 森林
	Mountain, ///< 山岳
	Snow,     ///< 雪原
};

/// @brief 地形生成の設定
struct TerrainConfig
{
	int width = 64;            ///< マップ幅
	int height = 64;           ///< マップ高さ
	int octaves = 4;           ///< オクターブ数
	float persistence = 0.5f;  ///< 振幅減衰率
	float lacunarity = 2.0f;   ///< 周波数増加率
	float scale = 30.0f;       ///< ノイズスケール
	uint32_t seed = 0;         ///< 乱数シード

	/// @brief 水面の閾値
	float waterLevel = 0.3f;
	/// @brief 砂地の閾値
	float sandLevel = 0.4f;
	/// @brief 草原の閾値
	float grassLevel = 0.6f;
	/// @brief 森林の閾値
	float forestLevel = 0.75f;
	/// @brief 山岳の閾値
	float mountainLevel = 0.88f;
};

/// @brief 地形生成結果
struct TerrainResult
{
	std::vector<std::vector<float>> heightmap;  ///< 高さマップ [0,1]
	std::vector<std::vector<float>> moisture;   ///< 湿度マップ [0,1]
	std::vector<std::vector<BiomeType>> biomes; ///< バイオームマップ
	int width = 0;   ///< マップ幅
	int height = 0;  ///< マップ高さ
};

namespace detail
{

/// @brief 簡易ハッシュベースのグラデーションノイズ（内部用）
///
/// 外部ライブラリに依存しない自己完結型の実装。
/// パーリンノイズの近似を提供する。
class SimpleNoise
{
public:
	/// @brief コンストラクタ
	/// @param seed 乱数シード
	explicit SimpleNoise(uint32_t seed = 0) noexcept
	{
		// パーミュテーションテーブルを初期化
		for (int i = 0; i < 256; ++i)
		{
			m_perm[i] = static_cast<uint8_t>(i);
		}
		// Fisher-Yatesシャッフル
		uint32_t s = seed;
		for (int i = 255; i > 0; --i)
		{
			s = xorshift(s);
			const int j = static_cast<int>(s % static_cast<uint32_t>(i + 1));
			const uint8_t tmp = m_perm[i];
			m_perm[i] = m_perm[j];
			m_perm[j] = tmp;
		}
		// テーブルを2倍に複製
		for (int i = 0; i < 256; ++i)
		{
			m_perm[256 + i] = m_perm[i];
		}
	}

	/// @brief 2Dノイズ値を取得する
	/// @param x X座標
	/// @param y Y座標
	/// @return ノイズ値（おおよそ-1〜1）
	[[nodiscard]] float noise2D(float x, float y) const noexcept
	{
		const int xi = static_cast<int>(std::floor(x)) & 255;
		const int yi = static_cast<int>(std::floor(y)) & 255;
		const float xf = x - std::floor(x);
		const float yf = y - std::floor(y);
		const float u = fade(xf);
		const float v = fade(yf);

		const int aa = m_perm[m_perm[xi] + yi];
		const int ab = m_perm[m_perm[xi] + yi + 1];
		const int ba = m_perm[m_perm[xi + 1] + yi];
		const int bb = m_perm[m_perm[xi + 1] + yi + 1];

		const float x1 = lerp(u, grad(aa, xf, yf), grad(ba, xf - 1.0f, yf));
		const float x2 = lerp(u, grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f));
		return lerp(v, x1, x2);
	}

private:
	uint8_t m_perm[512] = {};

	[[nodiscard]] static constexpr float fade(float t) noexcept
	{
		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}

	[[nodiscard]] static constexpr float lerp(float t, float a, float b) noexcept
	{
		return a + t * (b - a);
	}

	[[nodiscard]] static constexpr float grad(int hash, float x, float y) noexcept
	{
		const int h = hash & 3;
		const float u = (h < 2) ? x : y;
		const float v = (h < 2) ? y : x;
		return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
	}

	[[nodiscard]] static constexpr uint32_t xorshift(uint32_t s) noexcept
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
};

/// @brief オクターブ付きノイズ値を取得する
[[nodiscard]] inline float octaveNoise(
	const SimpleNoise& noise,
	float x, float y,
	int octaves,
	float persistence,
	float lacunarity) noexcept
{
	float total = 0.0f;
	float amplitude = 1.0f;
	float frequency = 1.0f;
	float maxVal = 0.0f;

	for (int i = 0; i < octaves; ++i)
	{
		total += noise.noise2D(x * frequency, y * frequency) * amplitude;
		maxVal += amplitude;
		amplitude *= persistence;
		frequency *= lacunarity;
	}

	return (maxVal > 0.0f) ? total / maxVal : 0.0f;
}

} // namespace detail

/// @brief 高さと湿度からバイオームを分類する
/// @param height 高さ値 [0,1]
/// @param moisture 湿度値 [0,1]（森林判定に使用）
/// @return バイオーム種別
[[nodiscard]] constexpr BiomeType classifyBiome(float height, float moisture) noexcept
{
	if (height < 0.3f)
	{
		return BiomeType::Water;
	}
	if (height < 0.4f)
	{
		return BiomeType::Sand;
	}
	if (height < 0.6f)
	{
		return (moisture > 0.5f) ? BiomeType::Forest : BiomeType::Grass;
	}
	if (height < 0.75f)
	{
		return (moisture > 0.4f) ? BiomeType::Forest : BiomeType::Grass;
	}
	if (height < 0.88f)
	{
		return BiomeType::Mountain;
	}
	return BiomeType::Snow;
}

/// @brief 高さと設定の閾値からバイオームを分類する
/// @param height 高さ値 [0,1]
/// @param moisture 湿度値 [0,1]
/// @param config 地形設定（閾値を参照）
/// @return バイオーム種別
[[nodiscard]] constexpr BiomeType classifyBiome(float height, float moisture, const TerrainConfig& config) noexcept
{
	if (height < config.waterLevel)
	{
		return BiomeType::Water;
	}
	if (height < config.sandLevel)
	{
		return BiomeType::Sand;
	}
	if (height < config.grassLevel)
	{
		return (moisture > 0.5f) ? BiomeType::Forest : BiomeType::Grass;
	}
	if (height < config.forestLevel)
	{
		return (moisture > 0.4f) ? BiomeType::Forest : BiomeType::Grass;
	}
	if (height < config.mountainLevel)
	{
		return BiomeType::Mountain;
	}
	return BiomeType::Snow;
}

/// @brief ノイズベースの地形を生成する
/// @param config 地形設定
/// @return 地形生成結果
[[nodiscard]] inline TerrainResult generateTerrain(const TerrainConfig& config)
{
	TerrainResult result;
	result.width = config.width;
	result.height = config.height;

	const auto w = static_cast<size_t>(config.width);
	const auto h = static_cast<size_t>(config.height);

	result.heightmap.assign(h, std::vector<float>(w, 0.0f));
	result.moisture.assign(h, std::vector<float>(w, 0.0f));
	result.biomes.assign(h, std::vector<BiomeType>(w, BiomeType::Water));

	const detail::SimpleNoise heightNoise(config.seed);
	const detail::SimpleNoise moistureNoise(config.seed + 12345);

	const float scale = (config.scale > 0.0f) ? config.scale : 1.0f;

	for (size_t y = 0; y < h; ++y)
	{
		for (size_t x = 0; x < w; ++x)
		{
			const float nx = static_cast<float>(x) / scale;
			const float ny = static_cast<float>(y) / scale;

			// 高さを [-1,1] → [0,1] に正規化
			const float rawHeight = detail::octaveNoise(
				heightNoise, nx, ny,
				config.octaves, config.persistence, config.lacunarity);
			const float normalizedHeight = std::clamp((rawHeight + 1.0f) * 0.5f, 0.0f, 1.0f);

			// 湿度（異なるオフセットで生成）
			const float rawMoisture = detail::octaveNoise(
				moistureNoise, nx + 100.0f, ny + 100.0f,
				config.octaves, config.persistence, config.lacunarity);
			const float normalizedMoisture = std::clamp((rawMoisture + 1.0f) * 0.5f, 0.0f, 1.0f);

			result.heightmap[y][x] = normalizedHeight;
			result.moisture[y][x] = normalizedMoisture;
			result.biomes[y][x] = classifyBiome(normalizedHeight, normalizedMoisture, config);
		}
	}

	return result;
}

} // namespace sgc::procedural
