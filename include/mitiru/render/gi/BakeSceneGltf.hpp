#pragma once

/// @file BakeSceneGltf.hpp
/// @brief レベルの glb / glTF の見た目のメッシュを BakeScene へ足す (光を焼くツールが使う)
/// @details 描くノードの判定は drawModel と LevelLoader と同じ (`mitiru_type` の無いノード)。反射率は glTF の
///          基本色の係数 (線形) に、基本色の画像の平均 (sRGB を線形にしてから平均) を掛けたもの。自発光も同じ作り方。

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <cgltf.h>

#include <mitiru/level/LevelExtras.hpp>
#include <mitiru/render/ColorSpace.hpp>
#include <mitiru/render/GltfLoader.hpp>
#include <mitiru/render/gi/BakeScene.hpp>

namespace mitiru::render::gi
{

namespace detail
{

/// @brief 画像の平均の色 (線形)。画像が無ければ 1
[[nodiscard]] inline Vec3 averageLinear(const CpuTexture& tex)
{
	if (!tex.valid()) { return {1.0f, 1.0f, 1.0f}; }
	double sum[3] = {0.0, 0.0, 0.0};
	const std::size_t n = static_cast<std::size_t>(tex.width) * static_cast<std::size_t>(tex.height);
	for (std::size_t i = 0; i < n; ++i)
	{
		for (int k = 0; k < 3; ++k) { sum[k] += srgbToLinear(tex.rgba[i * 4 + static_cast<std::size_t>(k)] / 255.0f); }
	}
	const double inv = 1.0 / static_cast<double>(n);
	return {static_cast<float>(sum[0] * inv), static_cast<float>(sum[1] * inv), static_cast<float>(sum[2] * inv)};
}

[[nodiscard]] inline BakeMaterial bakeMaterialOf(const cgltf_material* mat, const std::string& basePath)
{
	BakeMaterial m;
	if (mat == nullptr) { return m; }
	const GltfMaterialData g = render::detail::convertMaterial(*mat, basePath);
	m.albedo = mul(Vec3{g.baseColor.r, g.baseColor.g, g.baseColor.b}, averageLinear(g.baseColorTexture));
	m.emissive = Vec3{g.emissiveFactor[0], g.emissiveFactor[1], g.emissiveFactor[2]};
	if (g.emissiveTexture.valid()) { m.emissive = mul(m.emissive, averageLinear(g.emissiveTexture)); }
	m.doubleSided = g.doubleSided;
	return m;
}

[[nodiscard]] inline Vec3 transformPoint(const float m[16], Vec3 p) noexcept
{
	return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
	        m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}

/// @brief 三角形のプリミティブ 1 個を足す。鏡映の行列なら頂点の並びを逆にして表を保つ
inline void addPrimitive(BakeScene& scene, const cgltf_primitive& prim, const float world[16], float scale, std::uint32_t material)
{
	const cgltf_accessor* pos = nullptr;
	for (cgltf_size a = 0; a < prim.attributes_count; ++a)
	{
		if (prim.attributes[a].type == cgltf_attribute_type_position) { pos = prim.attributes[a].data; }
	}
	if (pos == nullptr || prim.type != cgltf_primitive_type_triangles) { return; }
	std::vector<Vec3> v(pos->count);
	for (cgltf_size i = 0; i < pos->count; ++i)
	{
		float p[3] = {};
		cgltf_accessor_read_float(pos, i, p, 3);
		v[i] = transformPoint(world, Vec3{p[0], p[1], p[2]}) * scale;
	}
	const float det = world[0] * (world[5] * world[10] - world[9] * world[6]) - world[4] * (world[1] * world[10] - world[9] * world[2]) +
	                  world[8] * (world[1] * world[6] - world[5] * world[2]);
	const cgltf_size count = prim.indices != nullptr ? prim.indices->count : pos->count;
	const auto index = [&](cgltf_size i) {
		return prim.indices != nullptr ? static_cast<std::size_t>(cgltf_accessor_read_index(prim.indices, i)) : i;
	};
	for (cgltf_size t = 0; t + 2 < count; t += 3)
	{
		const std::size_t a = index(t), b = index(t + 1), c = index(t + 2);
		if (a >= v.size() || b >= v.size() || c >= v.size()) { continue; }
		if (det < 0.0f) { scene.addTriangle(v[a], v[c], v[b], material); }
		else { scene.addTriangle(v[a], v[b], v[c], material); }
	}
}

} // namespace detail

/// @brief glb / glTF のレベルの描くメッシュを scene へ足す。読めなければ false と理由
/// @param scale drawModel に渡すのと同じ倍率
[[nodiscard]] inline bool addGltfLevel(BakeScene& scene, const std::filesystem::path& path, float scale, std::string& error)
{
	std::ifstream in(path, std::ios::binary);
	std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	cgltf_options options{};
	cgltf_data* data = nullptr;
	const std::string pathText = path.string();
	if (bytes.empty() || cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success)
	{
		error = "レベルを読めない (glb / glTF か): " + pathText;
		return false;
	}
	const std::string basePath = path.has_parent_path() ? path.parent_path().string() + "/" : std::string();
	if (cgltf_load_buffers(&options, data, pathText.c_str()) != cgltf_result_success)
	{
		cgltf_free(data);
		error = "レベルのバッファを読めない: " + pathText;
		return false;
	}
	std::vector<std::uint32_t> materials(data->materials_count + 1);
	for (cgltf_size m = 0; m < data->materials_count; ++m) { materials[m] = scene.addMaterial(detail::bakeMaterialOf(&data->materials[m], basePath)); }
	materials[data->materials_count] = scene.addMaterial(BakeMaterial{});
	for (cgltf_size n = 0; n < data->nodes_count; ++n)
	{
		const cgltf_node& node = data->nodes[n];
		if (node.mesh == nullptr || !level::isRenderedNode(node.extras.data)) { continue; }
		float world[16];
		cgltf_node_transform_world(&node, world);
		for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
		{
			const cgltf_primitive& prim = node.mesh->primitives[p];
			const cgltf_size mi = prim.material != nullptr ? static_cast<cgltf_size>(prim.material - data->materials) : data->materials_count;
			detail::addPrimitive(scene, prim, world, scale, materials[mi]);
		}
	}
	cgltf_free(data);
	return true;
}

} // namespace mitiru::render::gi
