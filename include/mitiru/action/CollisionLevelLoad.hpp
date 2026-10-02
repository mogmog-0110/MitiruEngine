#pragma once

/// @file CollisionLevelLoad.hpp
/// @brief 描画と同じメッシュファイル (.obj / .gltf / .glb)、Blender のレベル、`--collision` の JSON から CollisionLevel を組む。
/// @details 読み込みはヒープとファイルを使うので、DLL の読み込み時 (とホットリロード) に 1 度だけ呼ぶ。
///          遊んでいる間の問い合わせは CollisionWorld.hpp だけで済み、ここは要らない。
///          host の Jolt は JSON の三角形を片面で扱うが、こちらは両面で当たる (裏から入ったキャラも押し返すため)。

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <mitiru/action/CollisionLevel.hpp>
#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/level/LevelData.hpp>
#include <mitiru/physics/CollisionJson.hpp>
#include <mitiru/physics/MeshCollider.hpp>

namespace mitiru::action
{

inline void addTriangleMesh(CollisionLevelBuilder& builder, const physics3d::TriangleMesh3D& mesh, std::uint32_t layer,
                            const Pose& placement = {})
{
	builder.addTriangles(std::span<const Vec3>(mesh.vertices), std::span<const std::uint32_t>(mesh.indices), layer,
	                     placement);
}

/// @brief Blender から書き出したレベルの当たり判定の面 (LevelData::collision) を動かない地形として足す
inline void addLevelCollision(CollisionLevelBuilder& builder, const level::LevelData& level, std::uint32_t layer = 0)
{
	const level::TriangleSoup& soup = level.collision();
	builder.addTriangles(std::span<const Vec3>(soup.vertices), std::span<const std::uint32_t>(soup.indices), layer);
}

/// @brief レベルの当たり判定の面だけで地形を作る。箱や動く部品も足すなら builder と addLevelCollision を使う
[[nodiscard]] inline CollisionLevel buildLevelCollision(const level::LevelData& level, std::uint32_t layer = 0)
{
	CollisionLevelBuilder builder;
	addLevelCollision(builder, level, layer);
	return builder.build();
}

/// @brief `--collision` の要素を JSON の並び順に足す (箱は 12 枚、三角形は書かれた順)
inline void addCollisionShapes(CollisionLevelBuilder& builder, const std::vector<physics3d::CollisionJsonShape>& shapes)
{
	for (const auto& s : shapes)
	{
		if (s.kind == physics3d::CollisionJsonShape::Kind::Box)
		{
			builder.addBox(vmin(s.min, s.max), vmax(s.min, s.max), s.layer);
			continue;
		}
		builder.addTriangles(std::span<const Vec3>(s.vertices), std::span<const std::uint32_t>(s.indices), s.layer);
	}
}

/// @brief `--collision` と同じ書式の JSON を読んで足す。読めなければ false (warnOnce 1 行)
inline bool addCollisionJsonFile(CollisionLevelBuilder& builder, const std::string& path)
{
	const physics3d::CollisionJsonFile file = physics3d::readCollisionJsonFile(path);
	if (file.error != physics3d::CollisionJsonError::None)
	{
		debug::warnOnceFix("action.level.json", "当たり判定の JSON を読めない: " + path,
			"ファイルが無いか、構文エラーか、root が配列でも {\"boxes\":[...]} でもない",
			"[{\"min\":[x,y,z],\"max\":[x,y,z],\"layer\":0}] の形で書く");
		return false;
	}
	addCollisionShapes(builder, file.shapes);
	return true;
}

/// @brief 描画に使うメッシュファイルをそのまま当たり判定にする。読めなければ false (MeshColliderLibrary が warnOnce)
/// @param library 同じパスを何度足しても読むのは 1 度で済むよう、呼び出し側が持つ
inline bool addMeshFile(CollisionLevelBuilder& builder, physics3d::MeshColliderLibrary& library, const std::string& path,
                        std::uint32_t layer, const Pose& placement = {})
{
	const auto mesh = library.load(path);
	if (mesh == nullptr) { return false; }
	addTriangleMesh(builder, *mesh, layer, placement);
	return true;
}

} // namespace mitiru::action
