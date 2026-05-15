#pragma once

/// @file GltfLoader.hpp
/// @brief glTF 2.0 ローダー (.gltf / .glb)
/// @details cgltfを使用してglTFファイルをパースし、エンジンのMeshオブジェクトに変換する。
///          ObjLoader.hppと同じパターン（optional返却）を踏襲する。

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/GltfTypes.hpp>
#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>

/// cgltf 実装ガード — ヘッダーオンリーエンジンのため inline 関数内で定義
#ifndef MITIRU_CGLTF_IMPL_GUARD
#define MITIRU_CGLTF_IMPL_GUARD
#define CGLTF_IMPLEMENTATION
#endif
#include <cgltf.h>

namespace mitiru::render
{

namespace detail
{

/// @brief cgltfアクセサから浮動小数点値を安全に読み取る
[[nodiscard]] inline float readFloat(const cgltf_accessor* accessor, cgltf_size index, cgltf_size component)
{
	float value = 0.0f;
	if (accessor && index < accessor->count)
	{
		cgltf_accessor_read_float(accessor, index, &value + 0, 1);
		/// 複数コンポーネントの場合、一時バッファを使う
		if (component > 0)
		{
			float buf[4] = {0};
			const auto numComponents = cgltf_num_components(accessor->type);
			if (component < static_cast<cgltf_size>(numComponents))
			{
				cgltf_accessor_read_float(accessor, index, buf, static_cast<cgltf_size>(numComponents));
				value = buf[component];
			}
		}
		else
		{
			float buf[4] = {0};
			cgltf_accessor_read_float(accessor, index, buf, 1);
			value = buf[0];
		}
	}
	return value;
}

/// @brief cgltfアクセサからVec3を読み取る
[[nodiscard]] inline sgc::Vec3f readVec3(const cgltf_accessor* accessor, cgltf_size index)
{
	float buf[3] = {0, 0, 0};
	if (accessor && index < accessor->count)
	{
		cgltf_accessor_read_float(accessor, index, buf, 3);
	}
	return {buf[0], buf[1], buf[2]};
}

/// @brief cgltfアクセサからVec2を読み取る
[[nodiscard]] inline sgc::Vec2f readVec2(const cgltf_accessor* accessor, cgltf_size index)
{
	float buf[2] = {0, 0};
	if (accessor && index < accessor->count)
	{
		cgltf_accessor_read_float(accessor, index, buf, 2);
	}
	return {buf[0], buf[1]};
}

/// @brief cgltfアクセサからインデックスを読み取る
[[nodiscard]] inline uint32_t readIndex(const cgltf_accessor* accessor, cgltf_size index)
{
	if (!accessor || index >= accessor->count) { return 0; }
	return static_cast<uint32_t>(cgltf_accessor_read_index(accessor, index));
}

/// @brief 三角形の頂点からフラット法線を計算する
[[nodiscard]] inline sgc::Vec3f computeFlatNormal(
	const sgc::Vec3f& v0, const sgc::Vec3f& v1, const sgc::Vec3f& v2)
{
	const sgc::Vec3f edge1 = v1 - v0;
	const sgc::Vec3f edge2 = v2 - v0;
	sgc::Vec3f n{
		edge1.y * edge2.z - edge1.z * edge2.y,
		edge1.z * edge2.x - edge1.x * edge2.z,
		edge1.x * edge2.y - edge1.y * edge2.x
	};
	const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
	if (len > 1e-7f)
	{
		n.x /= len; n.y /= len; n.z /= len;
	}
	return n;
}

/// @brief cgltf プリミティブを GltfMeshPrimitive に変換する
[[nodiscard]] inline GltfMeshPrimitive convertPrimitive(const cgltf_primitive& prim)
{
	GltfMeshPrimitive result;

	/// 三角形以外はスキップ（空で返す）
	if (prim.type != cgltf_primitive_type_triangles)
	{
		return result;
	}

	/// アクセサを検索する
	const cgltf_accessor* posAccessor = nullptr;
	const cgltf_accessor* normAccessor = nullptr;
	const cgltf_accessor* uvAccessor = nullptr;

	for (cgltf_size i = 0; i < prim.attributes_count; ++i)
	{
		const auto& attr = prim.attributes[i];
		switch (attr.type)
		{
		case cgltf_attribute_type_position: posAccessor = attr.data; break;
		case cgltf_attribute_type_normal:   normAccessor = attr.data; break;
		case cgltf_attribute_type_texcoord: uvAccessor = attr.data; break;
		default: break;
		}
	}

	if (!posAccessor || posAccessor->count == 0)
	{
		return result;
	}

	/// 頂点を読み取る
	const auto vertexCount = posAccessor->count;
	result.vertices.resize(vertexCount);

	for (cgltf_size i = 0; i < vertexCount; ++i)
	{
		auto& v = result.vertices[i];
		v.position = readVec3(posAccessor, i);
		v.normal = normAccessor ? readVec3(normAccessor, i) : sgc::Vec3f{0, 0, 0};
		v.texCoord = uvAccessor ? readVec2(uvAccessor, i) : sgc::Vec2f{0, 0};
		v.color = sgc::Colorf{1, 1, 1, 1};
	}

	/// インデックスを読み取る
	if (prim.indices)
	{
		const auto indexCount = prim.indices->count;
		result.indices.resize(indexCount);
		for (cgltf_size i = 0; i < indexCount; ++i)
		{
			result.indices[i] = readIndex(prim.indices, i);
		}
	}
	else
	{
		/// インデックスなし → 連番生成
		result.indices.resize(vertexCount);
		for (cgltf_size i = 0; i < vertexCount; ++i)
		{
			result.indices[i] = static_cast<uint32_t>(i);
		}
	}

	/// 法線がない場合、フラット法線を生成する
	if (!normAccessor && result.indices.size() >= 3)
	{
		for (std::size_t i = 0; i + 2 < result.indices.size(); i += 3)
		{
			const auto i0 = result.indices[i];
			const auto i1 = result.indices[i + 1];
			const auto i2 = result.indices[i + 2];
			if (i0 < result.vertices.size() &&
				i1 < result.vertices.size() &&
				i2 < result.vertices.size())
			{
				const auto n = computeFlatNormal(
					result.vertices[i0].position,
					result.vertices[i1].position,
					result.vertices[i2].position);
				result.vertices[i0].normal = n;
				result.vertices[i1].normal = n;
				result.vertices[i2].normal = n;
			}
		}
	}

	/// マテリアルインデックス
	if (prim.material)
	{
		/// cgltf_material ポインタからインデックスを逆算する
		/// （cgltf はポインタベースなので、data->materials からのオフセットで取得）
		result.materialIndex = -1; // 後で外側で設定
	}

	return result;
}

} // namespace detail

/// @brief glTFメモリデータからシーンを読み込む
/// @param data データバッファ
/// @param size データサイズ
/// @return パース成功時はGltfSceneData、失敗時はnullopt
[[nodiscard]] inline std::optional<GltfSceneData> loadGltfFromMemory(
	const void* data, std::size_t size)
{
	if (!data || size == 0) { return std::nullopt; }

	cgltf_options options{};
	cgltf_data* gltfData = nullptr;

	cgltf_result result = cgltf_parse(&options, data, size, &gltfData);
	if (result != cgltf_result_success || !gltfData)
	{
		if (gltfData) { cgltf_free(gltfData); }
		return std::nullopt;
	}

	/// バッファデータをロードする（メモリ内glbの場合はバッファが埋め込まれている）
	result = cgltf_load_buffers(&options, gltfData, nullptr);
	if (result != cgltf_result_success)
	{
		cgltf_free(gltfData);
		return std::nullopt;
	}

	GltfSceneData scene;

	/// マテリアルを抽出する
	for (cgltf_size i = 0; i < gltfData->materials_count; ++i)
	{
		const auto& mat = gltfData->materials[i];
		GltfMaterialData gmat;
		gmat.name = mat.name ? mat.name : "";

		if (mat.has_pbr_metallic_roughness)
		{
			const auto& pbr = mat.pbr_metallic_roughness;
			gmat.baseColor = {
				pbr.base_color_factor[0],
				pbr.base_color_factor[1],
				pbr.base_color_factor[2],
				pbr.base_color_factor[3]
			};
			gmat.metallic = pbr.metallic_factor;
			gmat.roughness = pbr.roughness_factor;

			if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
			{
				const auto* img = pbr.base_color_texture.texture->image;
				gmat.baseColorTexturePath = img->uri ? img->uri : "";
			}
		}

		if (mat.normal_texture.texture && mat.normal_texture.texture->image)
		{
			const auto* img = mat.normal_texture.texture->image;
			gmat.normalTexturePath = img->uri ? img->uri : "";
		}

		scene.materials.push_back(std::move(gmat));
	}

	/// メッシュを抽出する
	for (cgltf_size i = 0; i < gltfData->meshes_count; ++i)
	{
		const auto& mesh = gltfData->meshes[i];
		GltfMeshData gMesh;
		gMesh.name = mesh.name ? mesh.name : "";

		for (cgltf_size j = 0; j < mesh.primitives_count; ++j)
		{
			auto prim = detail::convertPrimitive(mesh.primitives[j]);
			if (prim.vertices.empty()) { continue; }

			/// マテリアルインデックスを設定する
			if (mesh.primitives[j].material)
			{
				prim.materialIndex = static_cast<int>(
					mesh.primitives[j].material - gltfData->materials);
			}

			gMesh.primitives.push_back(std::move(prim));
		}

		if (!gMesh.primitives.empty())
		{
			scene.meshes.push_back(std::move(gMesh));
		}
	}

	cgltf_free(gltfData);

	if (scene.meshes.empty()) { return std::nullopt; }
	return scene;
}

/// @brief glTFファイルからシーンを読み込む
/// @param filePath glTF/GLBファイルパス
/// @return パース成功時はGltfSceneData、失敗時はnullopt
[[nodiscard]] inline std::optional<GltfSceneData> loadGltfSceneFromFile(const std::string& filePath)
{
	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) { return std::nullopt; }

	const auto fileSize = file.tellg();
	if (fileSize <= 0) { return std::nullopt; }

	file.seekg(0);
	std::vector<char> buffer(static_cast<std::size_t>(fileSize));
	file.read(buffer.data(), fileSize);

	return loadGltfFromMemory(buffer.data(), buffer.size());
}

/// @brief glTFファイルから最初のメッシュを読み込む（便利関数）
/// @param filePath glTF/GLBファイルパス
/// @return パース成功時はMesh、失敗時はnullopt
[[nodiscard]] inline std::optional<Mesh> loadGltfMeshFromFile(const std::string& filePath)
{
	auto scene = loadGltfSceneFromFile(filePath);
	if (!scene || scene->meshes.empty() || scene->meshes[0].primitives.empty())
	{
		return std::nullopt;
	}

	/// 最初のメッシュの最初のプリミティブを返す
	const auto& prim = scene->meshes[0].primitives[0];
	Mesh mesh;
	mesh.setVertices(prim.vertices);
	mesh.setIndices(prim.indices);
	return mesh;
}

} // namespace mitiru::render
