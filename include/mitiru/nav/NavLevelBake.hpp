#pragma once

/// @file NavLevelBake.hpp
/// @brief レベルのメッシュファイル (.obj / .gltf / .glb) を読んでナビメッシュ blob を焼く (mitiru_navbake の中身)
///
/// 読み込みは物理のメッシュコライダーと同じ MeshColliderLibrary を使う。当たり判定と経路が同じ三角形から
/// 作られるので、「歩ける所」と「ぶつかる所」がずれない。

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mitiru/nav/NavMeshBake.hpp"
#include "mitiru/physics/MeshCollider.hpp"

namespace mitiru::nav
{

/// @param scale drawModel に渡すのと同じ倍率。描画と同じ世界の寸法で焼く
[[nodiscard]] inline NavBakeResult bakeNavMeshFromFile(const std::filesystem::path& levelPath,
	const NavBakeSettings& settings = {}, float scale = 1.0f)
{
	physics3d::MeshColliderLibrary library;
	const auto mesh = library.load(levelPath.string());
	if (mesh == nullptr)
	{
		NavBakeResult r;
		r.error = "レベルのメッシュを読めない (.obj / .gltf / .glb か、三角形があるか): " + levelPath.string();
		return r;
	}
	std::vector<sgc::Vec3f> verts = mesh->vertices;
	for (auto& v : verts) v = sgc::Vec3f{v.x * scale, v.y * scale, v.z * scale};
	return bakeNavMesh(verts, mesh->indices, settings);
}

/// @return 書けなければ false
inline bool writeNavBlob(const std::filesystem::path& outPath, const std::vector<std::uint8_t>& blob)
{
	std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
	if (!f) return false;
	f.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
	return static_cast<bool>(f);
}

} // namespace mitiru::nav
