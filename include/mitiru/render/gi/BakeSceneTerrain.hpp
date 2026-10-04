#pragma once

/// @file BakeSceneTerrain.hpp
/// @brief world.json の地形 (高さマップ) を光を焼く場面に足す。光を遮り、跳ね返す面になる
/// @details 三角形は当たり判定と同じ分け方 (Heightfield.hpp の対角 b-c) で、step マスごとにまとめられる。
///          色は層ごとの平均色 (画像の平均 x color、どちらも線形) を splat の重みで混ぜた値を三角形の重心で取る。
///          描画の層の塗り分けを三角形 1 枚の色にならした近似で、草の葉、撒いた物、水面は入らない。
///          同じ色は 1 つの材質にまとめる。並べる順は高さマップの順で決まるので、同じ入力から同じ場面ができる。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <stb_image.h>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/render/ColorSpace.hpp>
#include <mitiru/render/gi/BakeScene.hpp>
#include <mitiru/terrain/OutdoorWorldLoad.hpp>

namespace mitiru::render::gi
{

namespace detail
{

/// @brief 画像の平均色 (線形)。読めなければ白
[[nodiscard]] inline Vec3 meanLinearColor(const std::string& path)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes || bytes->empty()) { return {1.0f, 1.0f, 1.0f}; }
	int w = 0, h = 0, channels = 0;
	stbi_uc* px = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &w, &h, &channels, 3);
	if (px == nullptr || w <= 0 || h <= 0)
	{
		stbi_image_free(px);
		return {1.0f, 1.0f, 1.0f};
	}
	std::array<float, 256> lut{};
	for (int i = 0; i < 256; ++i) { lut[static_cast<std::size_t>(i)] = srgbToLinear(static_cast<float>(i) / 255.0f); }
	double sum[3] = {};
	const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
	for (std::size_t i = 0; i < count * 3; ++i) { sum[i % 3] += lut[px[i]]; }
	stbi_image_free(px);
	const double inv = 1.0 / static_cast<double>(count);
	return {static_cast<float>(sum[0] * inv), static_cast<float>(sum[1] * inv), static_cast<float>(sum[2] * inv)};
}

/// @brief 層ごとの平均の反射率。描画と同じく color は sRGB で書いた値を線形にして画像に掛ける
[[nodiscard]] inline std::vector<Vec3> terrainLayerAlbedos(const terrain::OutdoorWorld& world)
{
	std::vector<Vec3> out;
	for (std::size_t k = 0; k < world.layers.size() && k < terrain::kMaxTerrainLayers; ++k)
	{
		const terrain::TerrainLayer& l = world.layers[k];
		const auto c = linearRgb(l.color[0], l.color[1], l.color[2]);
		const Vec3 image = l.albedo.empty() ? Vec3{1.0f, 1.0f, 1.0f} : meanLinearColor(l.albedo);
		out.push_back(mul(image, Vec3{c[0], c[1], c[2]}));
	}
	return out;
}

/// @brief (x, z) の反射率。splat の重みで層を混ぜる。splat が無ければ層 0、層が無ければ灰色
[[nodiscard]] inline Vec3 terrainAlbedoAt(const terrain::OutdoorWorld& world, const std::vector<Vec3>& layers, float x, float z)
{
	if (layers.empty()) { return {0.5f, 0.5f, 0.5f}; }
	if (world.splatCount() == 0) { return layers[0]; }
	Vec3 sum{};
	float total = 0.0f;
	for (std::size_t k = 0; k < layers.size(); ++k)
	{
		const float w = world.layerWeightAt(k, x, z);
		if (w <= 0.0f) { continue; }
		sum = sum + layers[k] * w;
		total += w;
	}
	return total > 0.0f ? sum * (1.0f / total) : layers[0];
}

/// @brief 色を 1/1024 刻みにして同じ材質を使い回す
class TerrainMaterials
{
public:
	explicit TerrainMaterials(BakeScene& scene) : m_scene(scene) {}

	[[nodiscard]] std::uint32_t of(Vec3 albedo)
	{
		const auto q = [](float v) { return static_cast<std::uint64_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 1023.0f)); };
		const std::uint64_t key = (q(albedo.x) << 20) | (q(albedo.y) << 10) | q(albedo.z);
		const auto it = m_ids.find(key);
		if (it != m_ids.end()) { return it->second; }
		const std::uint32_t id = m_scene.addMaterial({albedo, {}});
		m_ids.emplace(key, id);
		return id;
	}

private:
	BakeScene& m_scene;
	std::map<std::uint64_t, std::uint32_t> m_ids;
};

} // namespace detail

/// @brief 読み込んだ地形を step マスごとの三角形で場面に足す。足した三角形の数を返す
inline std::size_t addTerrain(BakeScene& scene, const terrain::OutdoorWorld& world, std::uint32_t step = 1)
{
	const terrain::Heightfield& h = world.terrain;
	if (!h.valid()) { return 0; }
	step = std::max(1u, step);
	const std::vector<Vec3> layers = detail::terrainLayerAlbedos(world);
	detail::TerrainMaterials materials(scene);
	const std::size_t before = scene.triangleCount();
	const auto at = [&](std::uint32_t ix, std::uint32_t iz) {
		const auto v = h.vertex(ix, iz);
		return Vec3{v.x, v.y, v.z};
	};
	const auto face = [&](Vec3 a, Vec3 b, Vec3 c) {
		const Vec3 mid = (a + b + c) * (1.0f / 3.0f);
		scene.addTriangle(a, b, c, materials.of(detail::terrainAlbedoAt(world, layers, mid.x, mid.z)));
	};
	for (std::uint32_t iz = 0; iz + 1 < h.depth(); iz += step)
	{
		const std::uint32_t iz1 = std::min(iz + step, h.depth() - 1);
		for (std::uint32_t ix = 0; ix + 1 < h.width(); ix += step)
		{
			const std::uint32_t ix1 = std::min(ix + step, h.width() - 1);
			// 上から見て反時計回りが表 (上向き)
			face(at(ix, iz), at(ix, iz1), at(ix1, iz));
			face(at(ix1, iz1), at(ix1, iz), at(ix, iz1));
		}
	}
	return scene.triangleCount() - before;
}

/// @brief world.json を読んで地形を足す。読めなければ false と理由
[[nodiscard]] inline bool addTerrainWorld(BakeScene& scene, const std::filesystem::path& worldJson, std::uint32_t step,
                                          std::string& error)
{
	const std::u8string u8 = worldJson.generic_u8string();
	const auto loaded = terrain::loadOutdoorWorld(std::string(reinterpret_cast<const char*>(u8.data()), u8.size()));
	if (!loaded.world)
	{
		error = "地形を読めない: " + loaded.error;
		return false;
	}
	if (addTerrain(scene, *loaded.world, step) == 0)
	{
		error = "地形に三角形が無い: " + worldJson.string();
		return false;
	}
	return true;
}

} // namespace mitiru::render::gi
