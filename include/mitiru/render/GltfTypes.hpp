#pragma once

/// @file GltfTypes.hpp
/// @brief glTF中間データ型
/// @details cgltf内部型とエンジン型の間の橋渡しデータ構造。

#include <cstdint>
#include <string>
#include <vector>

#include <sgc/types/Color.hpp>

#include <mitiru/render/Vertex3D.hpp>

namespace mitiru::render
{

/// @brief glTF プリミティブデータ
struct GltfMeshPrimitive
{
	std::vector<Vertex3D> vertices;    ///< 頂点データ
	std::vector<uint32_t> indices;     ///< インデックスデータ
	int materialIndex = -1;            ///< マテリアルインデックス (-1=デフォルト)
};

/// @brief glTF メッシュデータ
struct GltfMeshData
{
	std::string name;                              ///< メッシュ名
	std::vector<GltfMeshPrimitive> primitives;     ///< プリミティブ一覧
};

/// @brief glTF マテリアルデータ (PBR metallic-roughness)
struct GltfMaterialData
{
	std::string name;                              ///< マテリアル名
	sgc::Colorf baseColor{1, 1, 1, 1};            ///< ベースカラー
	float metallic = 0.0f;                         ///< メタリック [0,1]
	float roughness = 1.0f;                        ///< ラフネス [0,1]
	std::string baseColorTexturePath;              ///< ベースカラーテクスチャパス
	std::string normalTexturePath;                 ///< 法線マップパス
};

/// @brief glTF シーンデータ
struct GltfSceneData
{
	std::vector<GltfMeshData> meshes;              ///< メッシュ一覧
	std::vector<GltfMaterialData> materials;       ///< マテリアル一覧
};

} // namespace mitiru::render
