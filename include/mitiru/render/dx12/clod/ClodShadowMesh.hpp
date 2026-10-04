#pragma once

/// @file ClodShadowMesh.hpp
/// @brief clod のモデルを太陽の影の caster にする CPU のメッシュ
/// @details 影のパスは前方の描画の caster と同じ PSO・カスケード・余白で描くので、clod のモデルも普通の Mesh にして積む。
///          使うのは一番細かい段 (refined が無い cluster) だけ。影は画面の LOD と関係なく、元の形のまま落とす。

#include <mitiru/render/Mesh.hpp>
#include <mitiru/render/dx12/clod/ClodScene.hpp>

#include <sgc/math/Mat4.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mitiru::render::clod
{

/// @brief model の一番細かい段の三角形を、ファイルの座標のまま 1 つの Mesh にする。model が無ければ空の Mesh
[[nodiscard]] inline Mesh buildClodShadowMesh(const ClodScene& scene, int model)
{
	Mesh mesh;
	if (model < 0 || static_cast<std::size_t>(model) >= scene.models().size()) { return mesh; }
	const ClodModel& m = scene.models()[static_cast<std::size_t>(model)];
	const auto& pos = scene.positions();
	const auto& nrm = scene.normals();
	std::vector<Vertex3D> vertices;
	std::vector<std::uint32_t> indices;
	std::unordered_map<std::uint32_t, std::uint32_t> local;   // 連結配列の頂点 → このメッシュの頂点
	for (std::uint32_t ci = m.clusterBase; ci < m.clusterBase + m.clusterCount; ++ci)
	{
		const ClodCluster& c = scene.clusters()[ci];
		if (c.refined >= 0) { continue; }
		for (std::uint32_t t = 0; t < c.triCount * 3; ++t)
		{
			const std::uint32_t g = scene.clusterVerts()[c.vertOffset + scene.clusterTris()[c.triOffset + t]];
			const auto [it, added] = local.try_emplace(g, static_cast<std::uint32_t>(vertices.size()));
			if (added)
			{
				Vertex3D v;
				v.position = {pos[g * 3], pos[g * 3 + 1], pos[g * 3 + 2]};
				v.normal = {nrm[g * 3], nrm[g * 3 + 1], nrm[g * 3 + 2]};
				vertices.push_back(v);
			}
			indices.push_back(it->second);
		}
	}
	mesh.setVertices(std::move(vertices));
	mesh.setIndices(std::move(indices));
	return mesh;
}

/// @brief drawModel(path, pos, rotYDeg, scale) のインスタンスの world 行列。clod_engine.hlsl の instXform と同じ
///        (Y 軸回り、一様の拡大、平行移動の順)
[[nodiscard]] inline sgc::Mat4f clodInstanceWorld(const sgc::Vec3f& position, float rotYDeg, float scale) noexcept
{
	return sgc::Mat4f::translation(position) * sgc::Mat4f::rotationY(rotYDeg * 3.14159265f / 180.0f) *
	       sgc::Mat4f::scaling({scale, scale, scale});
}

} // namespace mitiru::render::clod
