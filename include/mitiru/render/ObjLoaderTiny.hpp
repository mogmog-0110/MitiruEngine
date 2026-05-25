#pragma once

/// @file ObjLoaderTiny.hpp
/// @brief tinyobjloader ベースの OBJ+MTL ローダー
/// @details OBJ+MTLファイルを読み込み、MTLの色を頂点カラーに焼き込む。
///          map_Kdテクスチャがある場合はstb_imageで読み込んでUVサンプリング。

#include <tiny_obj_loader.h>

#include <stb_image.h>

#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>

#include <sgc/math/Vec2.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mitiru::render
{

/// @brief tinyobjloaderでOBJ+MTLを読み込み、Meshに変換する
/// @param filePath OBJファイルのパス
/// @return メッシュ（読み込み失敗時はnullopt）
///
/// @code
/// auto mesh = mitiru::render::loadObjWithMaterials("assets/models/strawberry.obj");
/// @endcode
[[nodiscard]] inline std::optional<Mesh> loadObjWithMaterials(const std::string& filePath)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	// OBJファイルのディレクトリをMTL検索パスとして使用
	std::string dir;
	auto lastSlash = filePath.find_last_of("/\\");
	if (lastSlash != std::string::npos)
		dir = filePath.substr(0, lastSlash + 1);

	bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
		filePath.c_str(), dir.c_str());

	if (!ok || shapes.empty())
		return std::nullopt;

	// テクスチャ読み込み（map_Kdがあれば）
	struct TexData { unsigned char* data; int w, h; };
	std::unordered_map<int, TexData> textures;

	for (int mi = 0; mi < static_cast<int>(materials.size()); ++mi)
	{
		const auto& mat = materials[static_cast<size_t>(mi)];
		if (!mat.diffuse_texname.empty())
		{
			std::string texPath = dir + mat.diffuse_texname;
			int w = 0, h = 0, ch = 0;
			auto* data = stbi_load(texPath.c_str(), &w, &h, &ch, 4);
			if (data && w > 0 && h > 0)
			{
				textures[mi] = {data, w, h};
			}
		}
	}

	// メッシュ構築
	std::vector<Vertex3D> vertices;
	std::vector<uint32_t> indices;

	for (const auto& shape : shapes)
	{
		size_t indexOffset = 0;
		for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f)
		{
			const auto fv = static_cast<size_t>(shape.mesh.num_face_vertices[f]);
			const int matId = (f < shape.mesh.material_ids.size())
				? shape.mesh.material_ids[f] : -1;

			// マテリアルカラー
			sgc::Colorf matColor{1, 1, 1, 1};
			if (matId >= 0 && matId < static_cast<int>(materials.size()))
			{
				const auto& m = materials[static_cast<size_t>(matId)];
				matColor = {m.diffuse[0], m.diffuse[1], m.diffuse[2], 1.0f};
			}

			// 三角形ファン分割（fv >= 3）
			for (size_t v = 1; v + 1 < fv; ++v)
			{
				const size_t triIndices[3] = {0, v, v + 1};

				for (size_t ti = 0; ti < 3; ++ti)
				{
					const auto& idx = shape.mesh.indices[indexOffset + triIndices[ti]];

					Vertex3D vert;

					// Position
					if (idx.vertex_index >= 0)
					{
						vert.position = {
							attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 0],
							attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 1],
							attrib.vertices[3 * static_cast<size_t>(idx.vertex_index) + 2]
						};
					}

					// Normal
					if (idx.normal_index >= 0)
					{
						vert.normal = {
							attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 0],
							attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 1],
							attrib.normals[3 * static_cast<size_t>(idx.normal_index) + 2]
						};
					}

					// TexCoord
					if (idx.texcoord_index >= 0)
					{
						vert.texCoord = {
							attrib.texcoords[2 * static_cast<size_t>(idx.texcoord_index) + 0],
							attrib.texcoords[2 * static_cast<size_t>(idx.texcoord_index) + 1]
						};
					}

					// Color: テクスチャがあればUVサンプリング、なければMTL色
					auto texIt = textures.find(matId);
					if (texIt != textures.end() && idx.texcoord_index >= 0)
					{
						const auto& tex = texIt->second;
						float u = vert.texCoord.x - std::floor(vert.texCoord.x);
						float vv = 1.0f - (vert.texCoord.y - std::floor(vert.texCoord.y)); // V 反転
						int px = std::clamp(static_cast<int>(u * tex.w), 0, tex.w - 1);
						int py = std::clamp(static_cast<int>(vv * tex.h), 0, tex.h - 1);
						auto pi = static_cast<size_t>((py * tex.w + px) * 4);
						vert.color = {
							tex.data[pi] / 255.0f,
							tex.data[pi + 1] / 255.0f,
							tex.data[pi + 2] / 255.0f,
							tex.data[pi + 3] / 255.0f
						};
					}
					else
					{
						vert.color = matColor;
					}

					indices.push_back(static_cast<uint32_t>(vertices.size()));
					vertices.push_back(vert);
				}
			}

			indexOffset += fv;
		}
	}

	// テクスチャ解放
	for (auto& [id, tex] : textures)
	{
		stbi_image_free(tex.data);
	}

	if (vertices.empty())
		return std::nullopt;

	Mesh mesh;
	mesh.setVertices(std::move(vertices));
	mesh.setIndices(std::move(indices));
	return mesh;
}

} // namespace mitiru::render
