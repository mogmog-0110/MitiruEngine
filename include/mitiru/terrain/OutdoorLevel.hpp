#pragma once

/// @file OutdoorLevel.hpp
/// @brief Blender のレベル (level::LevelData)に置いた terrain / water / scatter の印を、屋外の 1 枚へ追加する
/// @details 印は `mitiru_type` を付けた Empty で、アドオンのボタンから追加する (docs/LEVEL_FROM_BLENDER.md)。
/// - terrain: 位置は地形の中心 (x, z)と高さ 0 の y。`height` (数)で高さの幅を上書きする
/// - water: 立方体表示の Empty。位置の y が水面で、箱の x と z の広がりが水面の長方形。色などはプロパティに置く
/// - scatter: 立方体表示の Empty。箱の x と z の広がりに撒く。`model` (文字列)と密度などはプロパティに置く
/// Empty の回転は使わず、水面と範囲は軸に平行な長方形とする。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/level/LevelData.hpp>
#include <mitiru/terrain/OutdoorWorld.hpp>

namespace mitiru::terrain
{

namespace detail
{

inline void readColor(const level::LevelData& lv, const level::Entity& e, std::string_view key, float out[3])
{
	const level::Property* p = lv.property(e, key);
	if (p == nullptr || p->kind != level::PropertyKind::Vector || p->vectorSize < 3) { return; }
	for (int i = 0; i < 3; ++i) { out[i] = p->value[i]; }
}

[[nodiscard]] inline WaterBody waterFromEntity(const level::LevelData& lv, const level::Entity& e)
{
	WaterBody w;
	w.minX = e.position[0] - e.halfExtents[0];
	w.maxX = e.position[0] + e.halfExtents[0];
	w.minZ = e.position[2] - e.halfExtents[2];
	w.maxZ = e.position[2] + e.halfExtents[2];
	w.height = e.position[1];
	readColor(lv, e, "shallow_color", w.shallowColor);
	readColor(lv, e, "deep_color", w.deepColor);
	readColor(lv, e, "absorption", w.absorption);
	w.foamDepth = lv.number(e, "foam_depth", w.foamDepth);
	w.waveAmplitude = lv.number(e, "wave_amplitude", w.waveAmplitude);
	w.waveScale = lv.number(e, "wave_scale", w.waveScale);
	w.waveSpeed = lv.number(e, "wave_speed", w.waveSpeed);
	w.reflectivity = lv.number(e, "reflectivity", w.reflectivity);
	w.toonBands = static_cast<std::int32_t>(lv.number(e, "toon_bands", 0.0f));
	return w;
}

[[nodiscard]] inline ScatterSet scatterFromEntity(const level::LevelData& lv, const level::Entity& e, const std::string& baseDir)
{
	ScatterSet s;
	const std::string_view model = lv.text(e, "model");
	s.model = model.empty() ? std::string() : baseDir + std::string(model);
	ScatterRule& r = s.rule;
	r.regionMinX = e.position[0] - e.halfExtents[0];
	r.regionMaxX = e.position[0] + e.halfExtents[0];
	r.regionMinZ = e.position[2] - e.halfExtents[2];
	r.regionMaxZ = e.position[2] + e.halfExtents[2];
	r.perM2 = lv.number(e, "per_m2", r.perM2);
	r.seed = static_cast<std::uint32_t>(lv.number(e, "seed", static_cast<float>(e.nameHash & 0xFFFFu)));
	r.scaleMin = lv.number(e, "scale_min", r.scaleMin);
	r.scaleMax = lv.number(e, "scale_max", r.scaleMax);
	r.maxSlopeDeg = lv.number(e, "max_slope", r.maxSlopeDeg);
	r.sink = lv.number(e, "sink", r.sink);
	r.alignToNormal = lv.number(e, "align", r.alignToNormal);
	s.collideRadius = lv.number(e, "collide_radius", 0.0f);
	s.collideHeight = lv.number(e, "collide_height", s.collideHeight);
	return s;
}

} // namespace detail

/// @brief terrain の印で地形の置き場所を上書きする。高さマップを読む前に呼ぶ。印が 2 つ以上なら最初のものを使う
inline void applyLevelTerrain(const level::LevelData& lv, HeightfieldPlacement& p, std::vector<std::string>& warnings)
{
	int count = 0;
	lv.forEachOfType("terrain", [&](const level::Entity& e, std::size_t) {
		if (count++ > 0)
		{
			warnings.push_back(std::string("terrain の印は 1 つにする (") + e.name + " は無視した)");
			return;
		}
		p.originX = e.position[0] - p.sizeX * 0.5f;
		p.originZ = e.position[2] - p.sizeZ * 0.5f;
		p.originY = e.position[1];
		p.height = lv.number(e, "height", p.height);
	});
}

/// @brief water と scatter の印を追加する。地形を読んだ後に呼ぶ。baseDir は scatter の `model` の相対パスの起点とする
inline void applyLevelMarks(const level::LevelData& lv, const std::string& baseDir, OutdoorWorld& world)
{
	lv.forEachOfType("water", [&](const level::Entity& e, std::size_t) { world.water.push_back(detail::waterFromEntity(lv, e)); });
	lv.forEachOfType("scatter", [&](const level::Entity& e, std::size_t) {
		ScatterSet s = detail::scatterFromEntity(lv, e, baseDir);
		if (s.model.empty())
		{
			world.warnings.push_back(std::string("scatter の印 ") + e.name + " に model (文字列) が無いので飛ばした");
			return;
		}
		scatter(world.terrain, nullptr, s.rule, s.instances);
		world.scatter.push_back(std::move(s));
	});
}

} // namespace mitiru::terrain
