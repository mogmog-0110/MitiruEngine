#pragma once

/// @file BakeSceneObj.hpp
/// @brief レベルの .obj を、光を焼く場面の三角形として足す (lighting.json の "level" が .obj のとき)
/// @details .obj の材質 (mtl) は読まず、全部の面に 1 つの材質を使う。面の向きは .obj の頂点の並びのまま
///          (反時計回りに見える側が表)。

#include <cstdint>
#include <filesystem>
#include <string>

#include <mitiru/render/ObjLoader.hpp>
#include <mitiru/render/gi/BakeScene.hpp>

namespace mitiru::render::gi
{

/// @brief path の .obj の面を scale 倍して scene へ足す。読めなければ false と理由
[[nodiscard]] inline bool addObjLevel(BakeScene& scene, const std::filesystem::path& path, float scale,
                                      const BakeMaterial& material, std::string& error)
{
	const auto mesh = loadObjFromFile(path.string());
	if (!mesh || mesh->indices().size() < 3)
	{
		error = "レベルの .obj を読めないか、面が無い: " + path.string();
		return false;
	}
	const std::uint32_t m = scene.addMaterial(material);
	const auto& v = mesh->vertices();
	const auto& ix = mesh->indices();
	const auto at = [&](std::uint32_t i) {
		const auto& p = v[i].position;
		return Vec3{p.x * scale, p.y * scale, p.z * scale};
	};
	for (std::size_t i = 0; i + 2 < ix.size(); i += 3) { scene.addTriangle(at(ix[i]), at(ix[i + 1]), at(ix[i + 2]), m); }
	return true;
}

} // namespace mitiru::render::gi
