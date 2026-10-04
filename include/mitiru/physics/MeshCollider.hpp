#pragma once

/// @file MeshCollider.hpp
/// @brief 描画用のメッシュ (render::Mesh / .obj / .gltf / .glb) からメッシュコライダーの三角形を作る
///
/// 物理側 (RigidBodyComponent3D / PhysicsSystem3D) は三角形しか知らず、ファイル形式や描画の頂点形式に
/// 依存しない。読み込みと変換はここに閉じ、SceneInstantiator には MeshColliderLibrary::load を渡す。

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sgc/math/Quaternion.hpp"
#include "mitiru/debug/WarnOnce.hpp"
#include "mitiru/physics/RigidBodyComponent3D.hpp"
#include "mitiru/render/GltfLoader.hpp"
#include "mitiru/render/Mesh.hpp"
#include "mitiru/render/ObjLoaderTiny.hpp"

namespace mitiru::physics3d
{

/// @brief 添字を持たないメッシュは頂点 3 つで 1 枚として読む
[[nodiscard]] inline std::shared_ptr<const TriangleMesh3D> triangleMeshFrom(const render::Mesh& mesh)
{
	auto out = std::make_shared<TriangleMesh3D>();
	out->vertices.reserve(mesh.vertices().size());
	for (const auto& v : mesh.vertices()) out->vertices.push_back(v.position);

	if (!mesh.indices().empty())
	{
		out->indices = mesh.indices();
	}
	else
	{
		const auto count = static_cast<std::uint32_t>(out->vertices.size() / 3 * 3);
		out->indices.reserve(count);
		for (std::uint32_t i = 0; i < count; ++i) out->indices.push_back(i);
	}
	return out;
}

namespace detail
{

/// @brief ノードの局所 TRS を親へ辿って掛け、頂点をシーンの座標へ移す
[[nodiscard]] inline sgc::Vec3f toSceneSpace(const render::GltfSceneData& scene, int node, sgc::Vec3f p)
{
	for (int n = node; n >= 0 && n < static_cast<int>(scene.nodes.size()); n = scene.nodes[static_cast<std::size_t>(n)].parent)
	{
		const auto& nd = scene.nodes[static_cast<std::size_t>(n)];
		const sgc::Quaternionf q{nd.rotation.x, nd.rotation.y, nd.rotation.z, nd.rotation.w};
		p = nd.translation + q.rotate(sgc::Vec3f{p.x * nd.scale.x, p.y * nd.scale.y, p.z * nd.scale.z});
	}
	return p;
}

inline void appendPrimitives(const render::GltfSceneData& scene, const render::GltfMeshData& mesh, int node,
	TriangleMesh3D& out)
{
	for (const auto& prim : mesh.primitives)
	{
		const auto base = static_cast<std::uint32_t>(out.vertices.size());
		for (const auto& v : prim.vertices) out.vertices.push_back(toSceneSpace(scene, node, v.position));
		for (const std::uint32_t i : prim.indices) out.indices.push_back(base + i);
	}
}

/// @brief 全ノードが参照するメッシュを、ノードの姿勢を掛けて 1 つの三角形の列にまとめる
[[nodiscard]] inline std::shared_ptr<const TriangleMesh3D> triangleMeshFrom(const render::GltfSceneData& scene)
{
	auto out = std::make_shared<TriangleMesh3D>();
	bool placedByNode = false;
	for (std::size_t n = 0; n < scene.nodes.size(); ++n)
	{
		const int mesh = scene.nodes[n].mesh;
		if (mesh < 0 || mesh >= static_cast<int>(scene.meshes.size())) continue;
		appendPrimitives(scene, scene.meshes[static_cast<std::size_t>(mesh)], static_cast<int>(n), *out);
		placedByNode = true;
	}
	if (!placedByNode)
	{
		for (const auto& mesh : scene.meshes) appendPrimitives(scene, mesh, -1, *out);
	}
	return out;
}

} // namespace detail

/// @brief メッシュファイルを三角形にして、パスごとに 1 度だけ読む (同じパスは同じ三角形を共有する)
class MeshColliderLibrary
{
public:
	/// @param baseDir 相対パスの基準 (シーン文書の置き場所など)
	explicit MeshColliderLibrary(std::filesystem::path baseDir = {}) : m_baseDir(std::move(baseDir)) {}

	/// @brief 読めなければ nullptr (warnOnce)。.obj / .gltf / .glb を受ける
	[[nodiscard]] std::shared_ptr<const TriangleMesh3D> load(const std::string& meshPath)
	{
		const auto it = m_cache.find(meshPath);
		if (it != m_cache.end()) return it->second;

		auto mesh = readFile(resolve(meshPath));
		if (mesh == nullptr || mesh->indices.size() < 3)
		{
			debug::warnOnce("physics.meshcollider.load",
				"当たり判定のメッシュ " + meshPath + " を読めません。ファイルがあるか、形式が .obj / .gltf / .glb か、"
				"三角形が入っているかを確かめてください。");
			mesh = nullptr;
		}
		m_cache.emplace(meshPath, mesh);
		return mesh;
	}

private:
	[[nodiscard]] std::filesystem::path resolve(const std::string& meshPath) const
	{
		const std::filesystem::path p(meshPath);
		return (p.is_absolute() || m_baseDir.empty()) ? p : m_baseDir / p;
	}

	[[nodiscard]] static std::shared_ptr<const TriangleMesh3D> readFile(const std::filesystem::path& path)
	{
		const std::string ext = path.extension().string();
		if (ext == ".obj")
		{
			const auto mesh = render::loadObjWithMaterials(path.string());
			return mesh.has_value() ? triangleMeshFrom(*mesh) : nullptr;
		}
		if (ext == ".gltf" || ext == ".glb")
		{
			const auto scene = render::loadGltfSceneFromFile(path.string());
			return scene.has_value() ? detail::triangleMeshFrom(*scene) : nullptr;
		}
		return nullptr;
	}

	std::filesystem::path m_baseDir;
	std::map<std::string, std::shared_ptr<const TriangleMesh3D>> m_cache;  ///< 読めなかったパスも nullptr で覚える
};

} // namespace mitiru::physics3d
