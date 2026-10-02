#pragma once

/// @file NavLevelBake.hpp
/// @brief レベルのメッシュファイル (.obj / .gltf / .glb) を読んでナビメッシュ blob を焼く (mitiru_navbake の中身)
///
/// 読み込みは物理のメッシュコライダーと同じ MeshColliderLibrary を使う。当たり判定と経路が同じ三角形から
/// 作られるので、「歩ける所」と「ぶつかる所」がずれない。
/// Blender から書き出したレベル (level/LevelLoader.hpp) にナビメッシュの元の面 (`mitiru_type` = navmesh か
/// `mitiru_nav`) があれば、その面だけで焼く。

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "mitiru/level/LevelLoader.hpp"
#include "mitiru/nav/NavCacheBake.hpp"
#include "mitiru/nav/NavMeshBake.hpp"
#include "mitiru/physics/MeshCollider.hpp"

namespace mitiru::nav
{

namespace detail
{

struct NavTriangles
{
	std::vector<sgc::Vec3f> vertices;
	std::vector<std::uint32_t> indices;
};

/// @brief glb / glTF のレベルにナビメッシュの元の面があれば返す
[[nodiscard]] inline std::optional<NavTriangles> levelNavSource(const std::filesystem::path& levelPath)
{
	const auto ext = levelPath.extension().string();
	if (ext != ".glb" && ext != ".gltf" && ext != ".GLB" && ext != ".GLTF") return std::nullopt;
	const auto u8 = levelPath.generic_u8string();
	const auto loaded = level::loadLevelFile(std::string(u8.begin(), u8.end()));
	if (!loaded.level || loaded.level->navSource().triangleCount() == 0) return std::nullopt;
	const auto& soup = loaded.level->navSource();
	NavTriangles out;
	for (std::size_t i = 0; i + 2 < soup.positions.size(); i += 3)
	{
		out.vertices.push_back({soup.positions[i], soup.positions[i + 1], soup.positions[i + 2]});
	}
	out.indices = soup.indices;
	return out;
}

/// @brief レベルのファイルから、焼く三角形を scale 倍して読む。読めなければ nullopt と理由
[[nodiscard]] inline std::optional<NavTriangles> loadNavTriangles(const std::filesystem::path& levelPath, float scale,
	std::string& error)
{
	NavTriangles tris;
	if (auto nav = levelNavSource(levelPath))
	{
		tris = std::move(*nav);
	}
	else
	{
		physics3d::MeshColliderLibrary library;
		const auto mesh = library.load(levelPath.string());
		if (mesh == nullptr)
		{
			error = "レベルのメッシュを読めない (.obj / .gltf / .glb か、三角形があるか): " + levelPath.string();
			return std::nullopt;
		}
		tris.vertices = mesh->vertices;
		tris.indices = mesh->indices;
	}
	for (auto& v : tris.vertices) v = sgc::Vec3f{v.x * scale, v.y * scale, v.z * scale};
	return tris;
}

} // namespace detail

/// @param scale drawModel に渡すのと同じ倍率。描画と同じ世界の寸法で焼く
[[nodiscard]] inline NavBakeResult bakeNavMeshFromFile(const std::filesystem::path& levelPath,
	const NavBakeSettings& settings = {}, float scale = 1.0f)
{
	NavBakeResult r;
	const auto tris = detail::loadNavTriangles(levelPath, scale, r.error);
	return tris ? bakeNavMesh(tris->vertices, tris->indices, settings) : r;
}

/// @brief bakeNavMeshFromFile の、障害物で作り直せる .navcache 版 (DynamicNavMesh.hpp が読む)
[[nodiscard]] inline NavCacheBakeResult bakeNavCacheFromFile(const std::filesystem::path& levelPath,
	const NavBakeSettings& settings = {}, float scale = 1.0f)
{
	NavCacheBakeResult r;
	const auto tris = detail::loadNavTriangles(levelPath, scale, r.error);
	return tris ? bakeNavCache(tris->vertices, tris->indices, settings) : r;
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
