#pragma once

/// @file FbxLoader.hpp
/// @brief FBX ファイルローダー (ufbx ライブラリ使用、条件コンパイル)
/// @details ufbx (MIT License) が利用可能な場合にFBXファイルからメッシュ・マテリアル・
///          ボーン階層を読み込む。ufbx 非利用時はスタブを提供する。

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/Vertex3D.hpp>

#ifdef MITIRU_HAS_UFBX
#include <ufbx.h>
#endif

namespace mitiru::asset
{

/// @brief FBXシーンデータ
struct FbxScene
{
	/// @brief FBXメッシュデータ
	struct FbxMesh
	{
		std::string name;
		std::vector<render::Vertex3D> vertices;
		std::vector<uint32_t> indices;
		int materialIndex = -1;
	};

	/// @brief FBXマテリアルデータ
	struct FbxMaterial
	{
		std::string name;
		std::array<float, 3> diffuseColor = {1.0f, 1.0f, 1.0f};
		std::array<float, 3> specularColor = {1.0f, 1.0f, 1.0f};
		float shininess = 32.0f;
		std::string diffuseTexturePath;
	};

	/// @brief FBXボーンデータ
	struct FbxBone
	{
		std::string name;
		int parentIndex = -1;
		std::array<float, 16> bindPoseMatrix = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};
	};

	std::vector<FbxMesh> meshes;
	std::vector<FbxMaterial> materials;
	std::vector<FbxBone> bones;
};

/// @brief FBXファイルローダー
/// @details ufbxライブラリが利用可能な場合にFBXファイルを読み込み、
///          エンジン内部の型に変換する。利用不可時はスタブとして動作し
///          load() は常に nullopt を返す。
///
/// @code
/// if constexpr (mitiru::asset::FbxLoader::isAvailable()) {
///     auto scene = mitiru::asset::FbxLoader::load("model.fbx");
///     if (scene) {
///         for (const auto& mesh : scene->meshes) {
///             auto engineMesh = FbxLoader::toEngineMesh(mesh);
///         }
///     }
/// }
/// @endcode
class FbxLoader
{
public:
	/// @brief FBXローダーが利用可能か
	/// @return ufbx が有効な場合 true
	[[nodiscard]] static constexpr bool isAvailable()
	{
#ifdef MITIRU_HAS_UFBX
		return true;
#else
		return false;
#endif
	}

	/// @brief FBXファイルを読み込む
	/// @param filepath FBXファイルパス
	/// @return FBXシーン、失敗時は nullopt
	[[nodiscard]] static std::optional<FbxScene> load(
		[[maybe_unused]] const std::string& filepath)
	{
#ifdef MITIRU_HAS_UFBX
		return loadWithUfbx(filepath);
#else
		return std::nullopt;
#endif
	}

	/// @brief FbxMesh をエンジン Mesh に変換する
	/// @param fbxMesh FBXメッシュ
	/// @return エンジン用 Mesh オブジェクト
	[[nodiscard]] static render::Mesh toEngineMesh(const FbxScene::FbxMesh& fbxMesh)
	{
		render::Mesh mesh;
		mesh.setVertices(fbxMesh.vertices);
		mesh.setIndices(fbxMesh.indices);
		return mesh;
	}

private:
#ifdef MITIRU_HAS_UFBX

	/// @brief ufbx を使用した FBX 読み込み実装
	[[nodiscard]] static std::optional<FbxScene> loadWithUfbx(
		const std::string& filepath)
	{
		ufbx_load_opts opts = {};
		opts.target_axes = ufbx_axes_right_handed_y_up;
		opts.target_unit_meters = 1.0f;

		ufbx_error error;
		ufbx_scene* uscene = ufbx_load_file(filepath.c_str(), &opts, &error);
		if (!uscene)
		{
			return std::nullopt;
		}

		FbxScene scene;

		// マテリアル変換
		for (size_t mi = 0; mi < uscene->materials.count; ++mi)
		{
			const auto* umat = uscene->materials.data[mi];
			scene.materials.push_back(convertMaterial(umat));
		}

		// メッシュ変換
		for (size_t mi = 0; mi < uscene->meshes.count; ++mi)
		{
			const auto* umesh = uscene->meshes.data[mi];
			auto converted = convertMesh(umesh);
			for (auto& m : converted)
			{
				scene.meshes.push_back(std::move(m));
			}
		}

		// ボーン階層変換
		for (size_t ni = 0; ni < uscene->nodes.count; ++ni)
		{
			const auto* node = uscene->nodes.data[ni];
			if (node->bone)
			{
				scene.bones.push_back(convertBone(node, uscene));
			}
		}

		ufbx_free_scene(uscene);
		return scene;
	}

	/// @brief ufbx マテリアルを FbxMaterial に変換
	[[nodiscard]] static FbxScene::FbxMaterial convertMaterial(
		const ufbx_material* umat)
	{
		FbxScene::FbxMaterial mat;
		mat.name = std::string(umat->name.data, umat->name.length);

		mat.diffuseColor = {
			static_cast<float>(umat->pbr.base_color.value_vec3.x),
			static_cast<float>(umat->pbr.base_color.value_vec3.y),
			static_cast<float>(umat->pbr.base_color.value_vec3.z)
		};

		mat.shininess = static_cast<float>(
			(1.0 - umat->pbr.roughness.value_real) * 128.0);

		if (umat->pbr.base_color.texture)
		{
			const auto& texName = umat->pbr.base_color.texture->filename;
			mat.diffuseTexturePath = std::string(
				texName.data, texName.length);
		}

		return mat;
	}

	/// @brief ufbx メッシュを FbxMesh 配列に変換 (マテリアルパートごと)
	[[nodiscard]] static std::vector<FbxScene::FbxMesh> convertMesh(
		const ufbx_mesh* umesh)
	{
		std::vector<FbxScene::FbxMesh> result;

		// マテリアルパートごとに分割
		const size_t partCount = (umesh->material_parts.count > 0)
			? umesh->material_parts.count : 1;

		for (size_t pi = 0; pi < partCount; ++pi)
		{
			FbxScene::FbxMesh mesh;
			mesh.name = std::string(umesh->name.data, umesh->name.length);

			if (umesh->material_parts.count > 0)
			{
				const auto& part = umesh->material_parts.data[pi];
				mesh.materialIndex = static_cast<int>(part.index);

				for (size_t fi = 0; fi < part.num_faces; ++fi)
				{
					const auto faceIdx = part.face_indices.data[fi];
					const auto& face = umesh->faces.data[faceIdx];
					triangulateAndAddFace(umesh, face, mesh);
				}
			}
			else
			{
				for (size_t fi = 0; fi < umesh->faces.count; ++fi)
				{
					const auto& face = umesh->faces.data[fi];
					triangulateAndAddFace(umesh, face, mesh);
				}
			}

			if (!mesh.indices.empty())
			{
				result.push_back(std::move(mesh));
			}
		}

		return result;
	}

	/// @brief 面を三角形分割して FbxMesh に追加
	static void triangulateAndAddFace(
		const ufbx_mesh* umesh,
		const ufbx_face& face,
		FbxScene::FbxMesh& outMesh)
	{
		for (uint32_t ti = 0; ti + 2 < face.num_indices; ++ti)
		{
			const uint32_t baseVert =
				static_cast<uint32_t>(outMesh.vertices.size());

			const size_t idx0 = face.index_begin;
			const size_t idx1 = face.index_begin + ti + 1;
			const size_t idx2 = face.index_begin + ti + 2;

			outMesh.vertices.push_back(extractVertex(umesh, idx0));
			outMesh.vertices.push_back(extractVertex(umesh, idx1));
			outMesh.vertices.push_back(extractVertex(umesh, idx2));

			outMesh.indices.push_back(baseVert + 0);
			outMesh.indices.push_back(baseVert + 1);
			outMesh.indices.push_back(baseVert + 2);
		}
	}

	/// @brief ufbx インデックスから Vertex3D を構築
	[[nodiscard]] static render::Vertex3D extractVertex(
		const ufbx_mesh* umesh, size_t index)
	{
		render::Vertex3D v;

		const auto vi = umesh->vertex_indices.data[index];
		const auto& pos = umesh->vertex_position.values.data[
			umesh->vertex_position.indices.data[index]];
		v.position = {
			static_cast<float>(pos.x),
			static_cast<float>(pos.y),
			static_cast<float>(pos.z)
		};

		if (umesh->vertex_normal.exists)
		{
			const auto& n = umesh->vertex_normal.values.data[
				umesh->vertex_normal.indices.data[index]];
			v.normal = {
				static_cast<float>(n.x),
				static_cast<float>(n.y),
				static_cast<float>(n.z)
			};
		}

		if (umesh->vertex_uv.exists)
		{
			const auto& uv = umesh->vertex_uv.values.data[
				umesh->vertex_uv.indices.data[index]];
			v.texCoord = {
				static_cast<float>(uv.x),
				static_cast<float>(uv.y)
			};
		}

		return v;
	}

	/// @brief ufbx ノードを FbxBone に変換
	[[nodiscard]] static FbxScene::FbxBone convertBone(
		const ufbx_node* node, const ufbx_scene* scene)
	{
		FbxScene::FbxBone bone;
		bone.name = std::string(node->name.data, node->name.length);

		// 親ボーンのインデックスを検索
		bone.parentIndex = -1;
		if (node->parent && node->parent->bone)
		{
			for (size_t i = 0; i < scene->nodes.count; ++i)
			{
				if (scene->nodes.data[i] == node->parent)
				{
					bone.parentIndex = static_cast<int>(i);
					break;
				}
			}
		}

		// バインドポーズ行列 (行優先)
		const auto& m = node->node_to_world;
		bone.bindPoseMatrix = {
			static_cast<float>(m.m00), static_cast<float>(m.m10),
			static_cast<float>(m.m20), 0.0f,
			static_cast<float>(m.m01), static_cast<float>(m.m11),
			static_cast<float>(m.m21), 0.0f,
			static_cast<float>(m.m02), static_cast<float>(m.m12),
			static_cast<float>(m.m22), 0.0f,
			static_cast<float>(m.m03), static_cast<float>(m.m13),
			static_cast<float>(m.m23), 1.0f
		};

		return bone;
	}

#endif // MITIRU_HAS_UFBX
};

} // namespace mitiru::asset
