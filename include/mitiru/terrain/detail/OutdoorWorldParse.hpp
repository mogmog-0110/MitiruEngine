#pragma once

/// @file OutdoorWorldParse.hpp
/// @brief world.json の各節を OutdoorWorld へ写す。OutdoorWorldLoad.hpp の下請け

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <mitiru/asset/AssetPack.hpp>
#include <mitiru/level/LevelLoader.hpp>
#include <mitiru/terrain/HeightfieldLoad.hpp>
#include <mitiru/terrain/OutdoorLevel.hpp>
#include <mitiru/terrain/OutdoorWorld.hpp>

namespace mitiru::terrain::detail
{

using Json = nlohmann::json;

struct WorldPaths
{
	std::string baseDir;

	[[nodiscard]] std::string resolve(const std::string& rel) const
	{
		if (rel.empty()) { return rel; }
		const bool absolute = rel[0] == '/' || rel[0] == '\\' || (rel.size() > 1 && rel[1] == ':');
		if (absolute || baseDir.empty()) { return rel; }
		const char last = baseDir.back();
		return (last == '/' || last == '\\') ? baseDir + rel : baseDir + "/" + rel;
	}

	[[nodiscard]] std::string dirWithSlash() const
	{
		if (baseDir.empty()) { return baseDir; }
		const char last = baseDir.back();
		return (last == '/' || last == '\\') ? baseDir : baseDir + "/";
	}
};

/// @brief 高さマップを読む前に決まっているもの。レベルの印で置き場所を上書きしてから読む
struct TerrainSource
{
	std::string          heightmap;
	std::uint32_t        rawWidth = 0;
	HeightfieldPlacement placement{};
};

[[nodiscard]] inline float num(const Json& j, const char* key, float fallback)
{
	const auto it = j.find(key);
	return (it != j.end() && it->is_number()) ? it->get<float>() : fallback;
}

[[nodiscard]] inline std::string str(const Json& j, const char* key)
{
	const auto it = j.find(key);
	return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

/// @brief 数の配列を n 個まで読み、読めた個数を返す。足りない分の out は変えない
inline int vec(const Json& j, const char* key, float* out, int n)
{
	const auto it = j.find(key);
	if (it == j.end() || !it->is_array()) { return 0; }
	int k = 0;
	for (const auto& v : *it)
	{
		if (k >= n || !v.is_number()) { break; }
		out[k++] = v.get<float>();
	}
	return k;
}

[[nodiscard]] inline bool parseTerrainSource(const Json& root, const WorldPaths& paths, TerrainSource& src, std::string& error)
{
	const auto it = root.find("terrain");
	if (it == root.end() || !it->is_object())
	{
		error = "\"terrain\" の節が無い";
		return false;
	}
	const Json& t = *it;
	src.heightmap = paths.resolve(str(t, "heightmap"));
	if (src.heightmap.empty())
	{
		error = "\"terrain\".\"heightmap\" が無い";
		return false;
	}
	src.rawWidth = static_cast<std::uint32_t>(std::max(num(t, "rawWidth", 0.0f), 0.0f));
	HeightfieldPlacement& p = src.placement;
	float size[2] = {p.sizeX, p.sizeZ};
	vec(t, "size", size, 2);
	p.sizeX = std::max(size[0], 0.01f);
	p.sizeZ = std::max(size[1], 0.01f);
	p.height = num(t, "height", p.height);
	float origin[3] = {-p.sizeX * 0.5f, 0.0f, -p.sizeZ * 0.5f};
	float center[3] = {0.0f, 0.0f, 0.0f};
	if (vec(t, "center", center, 3) == 3) { origin[0] = center[0] - p.sizeX * 0.5f; origin[1] = center[1]; origin[2] = center[2] - p.sizeZ * 0.5f; }
	vec(t, "origin", origin, 3);
	p.originX = origin[0];
	p.originY = origin[1];
	p.originZ = origin[2];
	return true;
}

inline void parseLod(const Json& root, OutdoorWorld& w)
{
	const auto t = root.find("terrain");
	const auto it = t->find("lod");
	if (it == t->end() || !it->is_object()) { return; }
	// 負の数を符号なしにすると巨大な値になるため、先に範囲へ収める
	w.lod.patchCells = static_cast<std::uint32_t>(std::clamp(num(*it, "patchCells", 32.0f), 2.0f, 256.0f));
	w.lod.lodCount = static_cast<std::uint32_t>(std::clamp(num(*it, "lodCount", 6.0f), 1.0f, 16.0f));
	w.lod.lod0Distance = num(*it, "lod0Distance", w.lod.lod0Distance);
	w.lod.morphStart = num(*it, "morphStart", w.lod.morphStart);
}

/// @brief 8 bit 画像を読む。channels が 1 ならグレースケール、4 なら RGBA
[[nodiscard]] inline std::optional<std::vector<std::uint8_t>> loadImage8(const std::string& path, int channels,
                                                                         std::uint32_t& width, std::uint32_t& height)
{
	const auto bytes = vfs::readAsset(path);
	if (!bytes) { return std::nullopt; }
	int w = 0;
	int h = 0;
	int c = 0;
	stbi_uc* px = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()), &w, &h, &c, channels);
	if (px == nullptr) { return std::nullopt; }
	std::vector<std::uint8_t> out(px, px + static_cast<std::size_t>(w) * h * channels);
	stbi_image_free(px);
	width = static_cast<std::uint32_t>(w);
	height = static_cast<std::uint32_t>(h);
	return out;
}

[[nodiscard]] inline DensityMap loadDensity(const std::string& path, OutdoorWorld& w)
{
	DensityMap d;
	if (path.empty()) { return d; }
	auto px = loadImage8(path, 1, d.width, d.height);
	if (!px) { w.warnings.push_back("密度マップを読めない (全域 1 にした): " + path); return {}; }
	d.values = std::move(*px);
	return d;
}

inline void parseRule(const Json& j, TerrainLayer& l)
{
	const auto it = j.find("rule");
	if (it == j.end() || !it->is_object()) { return; }
	l.hasRule = true;
	float h[2] = {l.minHeight, l.maxHeight};
	vec(*it, "height", h, 2);
	float s[2] = {l.minSlope, l.maxSlope};
	vec(*it, "slope", s, 2);
	l.minHeight = h[0];
	l.maxHeight = h[1];
	l.minSlope = s[0];
	l.maxSlope = s[1];
	l.blend = std::max(num(*it, "blend", l.blend), 0.001f);
}

inline void parseLayers(const Json& root, const WorldPaths& paths, OutdoorWorld& w)
{
	parseLod(root, w);
	const auto it = root.find("layers");
	if (it != root.end() && it->is_array())
	{
		for (const auto& j : *it)
		{
			if (w.layers.size() >= kMaxTerrainLayers) { w.warnings.push_back("層は 8 枚まで (9 枚目から無視した)"); break; }
			TerrainLayer l;
			l.albedo = paths.resolve(str(j, "albedo"));
			l.normal = paths.resolve(str(j, "normal"));
			vec(j, "color", l.color, 3);
			l.tile = std::max(num(j, "tile", l.tile), 0.01f);
			l.roughness = num(j, "roughness", l.roughness);
			l.metallic = num(j, "metallic", l.metallic);
			l.normalStrength = num(j, "normalStrength", l.normalStrength);
			parseRule(j, l);
			w.layers.push_back(std::move(l));
		}
	}
	const auto t = root.find("terrain");
	const auto splat = t->find("splat");
	if (splat == t->end() || !splat->is_array()) { return; }
	std::size_t k = 0;
	for (const auto& s : *splat)
	{
		if (k >= 2 || !s.is_string()) { break; }
		const std::string p = paths.resolve(s.get<std::string>());
		auto px = loadImage8(p, 4, w.splat[k].width, w.splat[k].height);
		if (!px) { w.warnings.push_back("splat マップを読めない: " + p); w.splat[k] = {}; break; }
		w.splat[k].rgba = std::move(*px);
		++k;
	}
}

inline void parseGrass(const Json& root, const WorldPaths& paths, OutdoorWorld& w)
{
	const auto it = root.find("grass");
	if (it == root.end() || !it->is_object()) { return; }
	const Json& g = *it;
	w.hasGrass = true;
	GrassSettings& s = w.grass;
	s.bladesPerM2 = std::max(num(g, "bladesPerM2", s.bladesPerM2), 0.0f);
	s.patchSize = std::max(num(g, "patchSize", s.patchSize), 1.0f);
	s.lodStart = num(g, "lodStart", s.lodStart);
	s.lodStep = num(g, "lodStep", s.lodStep);
	s.maxLod = static_cast<std::uint32_t>(std::clamp(num(g, "maxLod", 4.0f), 0.0f, 16.0f));
	s.fadeStart = num(g, "fadeStart", s.fadeStart);
	s.fadeEnd = std::max(num(g, "fadeEnd", s.fadeEnd), s.fadeStart + 0.01f);
	GrassLook& look = w.grassLook;
	look.bladeHeight = num(g, "height", look.bladeHeight);
	look.bladeWidth = num(g, "width", look.bladeWidth);
	look.maxSlopeDeg = num(g, "maxSlope", look.maxSlopeDeg);
	vec(g, "baseColor", look.baseColor, 3);
	vec(g, "tipColor", look.tipColor, 3);
	if (const auto wind = g.find("wind"); wind != g.end() && wind->is_object())
	{
		vec(*wind, "dir", look.windDir, 2);
		look.windStrength = num(*wind, "strength", look.windStrength);
		look.windSpeed = num(*wind, "speed", look.windSpeed);
	}
	w.grassDensity = loadDensity(paths.resolve(str(g, "density")), w);
}

inline void parseScatter(const Json& root, const WorldPaths& paths, OutdoorWorld& w)
{
	const auto it = root.find("scatter");
	if (it == root.end() || !it->is_array()) { return; }
	for (const auto& j : *it)
	{
		ScatterSet s;
		s.model = paths.resolve(str(j, "model"));
		if (s.model.empty()) { w.warnings.push_back("scatter の要素に model が無いので飛ばした"); continue; }
		ScatterRule& r = s.rule;
		r.perM2 = num(j, "perM2", r.perM2);
		r.seed = static_cast<std::uint32_t>(num(j, "seed", static_cast<float>(r.seed)));
		float sc[2] = {r.scaleMin, r.scaleMax};
		vec(j, "scale", sc, 2);
		r.scaleMin = sc[0];
		r.scaleMax = sc[1];
		r.maxSlopeDeg = num(j, "maxSlope", r.maxSlopeDeg);
		float hr[2] = {r.minHeight, r.maxHeight};
		vec(j, "height", hr, 2);
		r.minHeight = hr[0];
		r.maxHeight = hr[1];
		r.sink = num(j, "sink", r.sink);
		r.alignToNormal = num(j, "align", r.alignToNormal);
		if (const auto reg = j.find("region"); reg != j.end() && reg->is_object())
		{
			float lo[2] = {r.regionMinX, r.regionMinZ};
			float hi[2] = {r.regionMaxX, r.regionMaxZ};
			vec(*reg, "min", lo, 2);
			vec(*reg, "max", hi, 2);
			r.regionMinX = lo[0]; r.regionMinZ = lo[1]; r.regionMaxX = hi[0]; r.regionMaxZ = hi[1];
		}
		s.collideRadius = num(j, "collideRadius", 0.0f);
		s.collideHeight = num(j, "collideHeight", s.collideHeight);
		const DensityMap density = loadDensity(paths.resolve(str(j, "density")), w);
		w.scatter.push_back(std::move(s));
		// 密度マップは配置後に不要になるため、地形を読んだこの時点で配置する
		scatter(w.terrain, density.valid() ? &density : nullptr, w.scatter.back().rule, w.scatter.back().instances);
	}
}

inline void parseWater(const Json& root, OutdoorWorld& w)
{
	const auto it = root.find("water");
	if (it == root.end() || !it->is_array()) { return; }
	for (const auto& j : *it)
	{
		WaterBody b;
		b.height = num(j, "height", b.height);
		float lo[2] = {b.minX, b.minZ};
		float hi[2] = {b.maxX, b.maxZ};
		vec(j, "min", lo, 2);
		vec(j, "max", hi, 2);
		b.minX = lo[0]; b.minZ = lo[1]; b.maxX = hi[0]; b.maxZ = hi[1];
		vec(j, "shallowColor", b.shallowColor, 3);
		vec(j, "deepColor", b.deepColor, 3);
		vec(j, "absorption", b.absorption, 3);
		vec(j, "foamColor", b.foamColor, 3);
		b.foamDepth = num(j, "foamDepth", b.foamDepth);
		b.refraction = num(j, "refraction", b.refraction);
		b.waveAmplitude = num(j, "waveAmplitude", b.waveAmplitude);
		b.waveScale = num(j, "waveScale", b.waveScale);
		b.waveSpeed = num(j, "waveSpeed", b.waveSpeed);
		b.reflectivity = num(j, "reflectivity", b.reflectivity);
		b.toonBands = static_cast<std::int32_t>(num(j, "toonBands", 0.0f));
		w.water.push_back(b);
	}
}

/// @brief "level" で指定したレベルの glb を読む。読めなければ warning を残して nullopt を返す
[[nodiscard]] inline std::optional<level::LevelData> loadLevelFor(const Json& root, const WorldPaths& paths, OutdoorWorld& w)
{
	const std::string path = paths.resolve(str(root, "level"));
	if (path.empty()) { return std::nullopt; }
	auto r = level::loadLevelFile(path);
	if (!r.level) { w.warnings.push_back("レベルを読めない: " + path + " (" + r.error + ")"); }
	return std::move(r.level);
}

} // namespace mitiru::terrain::detail
