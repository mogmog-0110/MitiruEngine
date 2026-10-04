#pragma once

/// @file OutdoorWorld.hpp
/// @brief 屋外の 1 枚 (地形、材質の層、草、撒いた物、水面) を読み込んだもの。作った後は変えない
/// @details `*.world.json` 1 つから OutdoorWorldLoad.hpp が作る。
///          host の描画とゲーム DLL の当たり判定で同じものを使い、見た目と判定をそろえる。
///          ファイルの内容だけで決まるため、GameMemory には入れない。
///          地形 LOD の四分木と草の区画が heightfield を指すため、OutdoorWorld は動かさず `unique_ptr` で持つ。

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <mitiru/terrain/GrassField.hpp>
#include <mitiru/terrain/Heightfield.hpp>
#include <mitiru/terrain/Scatter.hpp>
#include <mitiru/terrain/TerrainLod.hpp>
#include <mitiru/terrain/Water.hpp>

namespace mitiru::terrain
{

inline constexpr std::size_t kMaxTerrainLayers = 8;

/// @brief splat マップの重みで混ぜる材質層 1 枚
struct TerrainLayer
{
	std::string albedo;            ///< 色の画像 (world.json からの相対を解決したパス)。空なら color だけ
	std::string normal;            ///< 接空間の法線マップ。空なら平ら
	float color[3] = {1.0f, 1.0f, 1.0f};  ///< 画像に掛ける色
	float tile = 8.0f;             ///< 画像 1 枚が覆う大きさ (m)
	float roughness = 0.9f;
	float metallic = 0.0f;
	float normalStrength = 1.0f;
	// splat マップが無いときに重みを作る規則。高さ (世界の y)と傾き (度)の範囲に blend の幅で入る
	bool  hasRule = false;
	float minHeight = -1.0e9f;
	float maxHeight = 1.0e9f;
	float minSlope = 0.0f;
	float maxSlope = 90.0f;
	float blend = 1.0f;            ///< 範囲の縁をぼかす幅 (高さは m、傾きは度に同じ値を使う)
};

/// @brief RGBA8 で 4 層分の重みを持つ splat マップ 1 枚。行 0 が z の小さい側
struct SplatImage
{
	std::uint32_t             width = 0;
	std::uint32_t             height = 0;
	std::vector<std::uint8_t> rgba;

	[[nodiscard]] bool valid() const noexcept
	{
		return width > 0 && height > 0 && rgba.size() == static_cast<std::size_t>(width) * height * 4;
	}
};

/// @brief 草の見た目と風。風は描画だけに使い、drawModel に渡した時刻 (秒)だけで決まる
struct GrassLook
{
	float bladeHeight = 0.55f;
	float bladeWidth = 0.06f;
	float baseColor[3] = {0.05f, 0.14f, 0.03f};  ///< 線形の色 (トーンマップの前)
	float tipColor[3] = {0.22f, 0.42f, 0.07f};
	float windDir[2] = {1.0f, 0.35f};
	float windStrength = 0.25f;  ///< 先端のずれ (m)
	float windSpeed = 1.3f;      ///< 1 秒あたりの波の進み
	float maxSlopeDeg = 40.0f;   ///< これより急な所には生やさない
};

/// @brief 撒く物 1 種類。instances は読み込み時に scatter() で作る
struct ScatterSet
{
	std::string                  model;  ///< drawModel に渡す glb (解決したパス)
	ScatterRule                  rule{};
	std::vector<ScatterInstance> instances;
	float                        collideRadius = 0.0f;  ///< 0 より大きければ当たり判定に縦の柱として入れる半径 (m)
	float                        collideHeight = 2.0f;
};

class OutdoorWorld
{
public:
	OutdoorWorld() = default;
	OutdoorWorld(const OutdoorWorld&) = delete;
	OutdoorWorld& operator=(const OutdoorWorld&) = delete;
	OutdoorWorld(OutdoorWorld&&) = delete;
	OutdoorWorld& operator=(OutdoorWorld&&) = delete;

	std::string               path;      ///< 読んだ world.json
	Heightfield               terrain;
	LodSettings               lod{};
	std::vector<TerrainLayer> layers;
	SplatImage                splat[2];  ///< 層 0-3 と 4-7。使わない方は空
	bool                      hasGrass = false;
	GrassSettings             grass{};
	GrassLook                 grassLook{};
	DensityMap                grassDensity;  ///< 空なら全域
	std::vector<ScatterSet>   scatter;
	std::vector<WaterBody>    water;
	std::vector<std::string>  warnings;   ///< 読めたが無視した指定

	LodQuadtree lodTree;   ///< terrain を指す
	GrassField  grassField;

	[[nodiscard]] std::size_t splatCount() const noexcept { return splat[1].valid() ? 2 : (splat[0].valid() ? 1 : 0); }

	/// @brief (x, z) で最も重い層の番号。足音や足跡の種類に使い、層が無ければ -1 を返す。描画と同じく、
	///        画素の中心を地形の角にそろえた双線形補間で重みを取る
	[[nodiscard]] int dominantLayerAt(float x, float z) const noexcept
	{
		if (layers.empty() || !terrain.valid()) { return -1; }
		int best = 0;
		float bestW = -1.0f;
		for (std::size_t k = 0; k < layers.size() && k < kMaxTerrainLayers; ++k)
		{
			const float w = layerWeightAt(k, x, z);
			if (w > bestW) { bestW = w; best = static_cast<int>(k); }
		}
		return best;
	}

	/// @brief (x, z) での層 layer の splat の重み (0..255、dominantLayerAt と同じ取り方)。splat が無い層は -1
	[[nodiscard]] float layerWeightAt(std::size_t layer, float x, float z) const noexcept
	{
		if (!terrain.valid() || layer >= kMaxTerrainLayers) { return -1.0f; }
		const auto& p = terrain.placement();
		const float u = std::clamp((x - p.originX) / p.sizeX, 0.0f, 1.0f);
		const float v = std::clamp((z - p.originZ) / p.sizeZ, 0.0f, 1.0f);
		return splatWeight(splat[layer / 4], u, v, static_cast<std::uint32_t>(layer % 4));
	}

private:
	[[nodiscard]] static float splatWeight(const SplatImage& img, float u, float v, std::uint32_t channel) noexcept
	{
		if (!img.valid()) { return -1.0f; }
		const float fx = u * static_cast<float>(img.width - 1);
		const float fz = v * static_cast<float>(img.height - 1);
		const auto x0 = static_cast<std::uint32_t>(fx);
		const auto z0 = static_cast<std::uint32_t>(fz);
		const std::uint32_t x1 = std::min(x0 + 1, img.width - 1);
		const std::uint32_t z1 = std::min(z0 + 1, img.height - 1);
		const auto at = [&](std::uint32_t px, std::uint32_t pz) {
			return static_cast<float>(img.rgba[(static_cast<std::size_t>(pz) * img.width + px) * 4 + channel]);
		};
		const float tx = fx - static_cast<float>(x0);
		const float tz = fz - static_cast<float>(z0);
		const float top = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * tx;
		const float bottom = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * tx;
		return top + (bottom - top) * tz;
	}
};

} // namespace mitiru::terrain
