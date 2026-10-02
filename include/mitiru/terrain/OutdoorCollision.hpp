#pragma once

/// @file OutdoorCollision.hpp
/// @brief OutdoorWorld の地形と、collideRadius を付けた撒いた物を CollisionLevelBuilder に追加する
/// @details 撒いた物は、軸に平行な縦長の箱 (幅 2 × collideRadius × 大きさ、高さ collideHeight × 大きさ)で判定する。
/// 木の幹や岩をキャラが通り抜けない程度の判定とし、モデルの形には合わせない。
/// 地形の三角形を先に追加するため、その id は terrainTriangleId と一致する。

#include <cstdint>

#include <mitiru/action/CollisionLevel.hpp>
#include <mitiru/terrain/HeightfieldCollision.hpp>
#include <mitiru/terrain/OutdoorWorld.hpp>

namespace mitiru::terrain
{

inline void addOutdoorWorld(action::CollisionLevelBuilder& builder, const OutdoorWorld& world, std::uint32_t terrainLayer,
                            std::uint32_t propLayer)
{
	addHeightfield(builder, world.terrain, terrainLayer);
	for (const ScatterSet& set : world.scatter)
	{
		if (!(set.collideRadius > 0.0f)) { continue; }
		for (const ScatterInstance& inst : set.instances)
		{
			const float r = set.collideRadius * inst.scale;
			const float top = set.collideHeight * inst.scale;
			const action::Vec3 lo{inst.position[0] - r, inst.position[1], inst.position[2] - r};
			const action::Vec3 hi{inst.position[0] + r, inst.position[1] + top, inst.position[2] + r};
			builder.addBox(lo, hi, propLayer);
		}
	}
}

} // namespace mitiru::terrain
