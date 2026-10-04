#pragma once

/// @file LightingBakeJob.hpp
/// @brief 光を焼く指定 (*.lighting.json) を読み、焼いて LightingBake を作る (mitiru_lightbake の中身)
/// @details 色はエンジンの他の所と同じ sRGB で書く ("#rrggbb" か 0..1 の 3 要素)。書式は docs/LIGHTING_GI.md。
///          パスは json のある場所からの相対。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/render/ColorSpace.hpp>
#include <mitiru/render/gi/BakeScene.hpp>
#include <mitiru/render/gi/BakeSceneGltf.hpp>
#include <mitiru/render/gi/BakeSceneTerrain.hpp>
#include <mitiru/render/gi/GiBaker.hpp>
#include <mitiru/render/gi/LightingBakeFile.hpp>
#include <mitiru/render/gi/ReflectionBake.hpp>

namespace mitiru::render::gi
{

struct LightingBakeJob
{
	BakeScene scene;
	bool hasGrid = false;
	ProbeGridDesc grid;
	std::vector<ReflectionProbeDesc> reflections;
	GiBakeSettings gi;
	ReflectionBakeSettings reflection;
	std::filesystem::path output;
};

namespace detail
{

[[nodiscard]] inline bool readVec3(const nlohmann::json& j, const char* key, Vec3& out)
{
	const auto it = j.find(key);
	if (it == j.end() || !it->is_array() || it->size() != 3) { return false; }
	for (const auto& e : *it)
	{
		if (!e.is_number()) { return false; }
	}
	out = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
	return true;
}

[[nodiscard]] inline int hexDigit(char ch) noexcept
{
	if (ch >= '0' && ch <= '9') { return ch - '0'; }
	if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
	if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
	return -1;
}

/// @brief "#rrggbb" か 0 以上の数の 3 要素を c へ。形が違えば false
[[nodiscard]] inline bool parseSrgb(const nlohmann::json& v, float c[3])
{
	if (v.is_string())
	{
		const std::string s = v.get<std::string>();
		if (s.size() != 7 || s[0] != '#') { return false; }
		for (int k = 0; k < 3; ++k)
		{
			const int hi = hexDigit(s[1 + 2 * k]), lo = hexDigit(s[2 + 2 * k]);
			if (hi < 0 || lo < 0) { return false; }
			c[k] = static_cast<float>(hi * 16 + lo) / 255.0f;
		}
		return true;
	}
	if (!v.is_array() || v.size() != 3) { return false; }
	for (int k = 0; k < 3; ++k)
	{
		const auto& e = v[static_cast<std::size_t>(k)];
		if (!e.is_number() || !(e.get<float>() >= 0.0f)) { return false; }
		c[k] = e.get<float>();
	}
	return true;
}

/// @brief sRGB の色 x intensity を線形にする。無ければ fallback。形が違えば error に理由を書いて fallback
[[nodiscard]] inline Vec3 readColor(const nlohmann::json& j, const char* key, Vec3 fallback, float intensity, std::string& error)
{
	const auto it = j.find(key);
	if (it == j.end()) { return fallback; }
	float c[3] = {};
	if (!parseSrgb(*it, c))
	{
		error = std::string("色は \"#rrggbb\" か 0 以上の数の 3 要素で書く: ") + key;
		return fallback;
	}
	const auto lin = linearRgb(c[0], c[1], c[2]);
	return Vec3{lin[0], lin[1], lin[2]} * intensity;
}

[[nodiscard]] inline float readFloat(const nlohmann::json& j, const char* key, float fallback)
{
	const auto it = j.find(key);
	return (it != j.end() && it->is_number()) ? it->get<float>() : fallback;
}

inline void readLights(const nlohmann::json& root, BakeScene& scene, std::string& error)
{
	if (const auto sun = root.find("sun"); sun != root.end() && sun->is_object())
	{
		scene.sun.enabled = readVec3(*sun, "direction", scene.sun.direction);
		scene.sun.direction = normalize(scene.sun.direction);
		scene.sun.color = readColor(*sun, "color", {1.0f, 1.0f, 1.0f}, readFloat(*sun, "intensity", 1.0f), error);
	}
	if (const auto sky = root.find("sky"); sky != root.end() && sky->is_object())
	{
		const float k = readFloat(*sky, "intensity", 1.0f);
		scene.sky.zenith = readColor(*sky, "zenith", {}, k, error);
		scene.sky.horizon = readColor(*sky, "horizon", scene.sky.zenith, k, error);
		scene.sky.ground = readColor(*sky, "ground", {}, k, error);
	}
	const auto lights = root.find("lights");
	if (lights == root.end() || !lights->is_array()) { return; }
	for (const auto& l : *lights)
	{
		BakeLight light;
		(void)readVec3(l, "position", light.position);
		light.range = readFloat(l, "range", 10.0f);
		light.color = readColor(l, "color", {1.0f, 1.0f, 1.0f}, readFloat(l, "intensity", 1.0f), error);
		if (l.value("type", std::string("point")) == "spot" && readVec3(l, "direction", light.direction))
		{
			const float outer = readFloat(l, "outerDeg", 45.0f) * kPi / 180.0f;
			const float inner = readFloat(l, "innerDeg", readFloat(l, "outerDeg", 45.0f) * 0.9f) * kPi / 180.0f;
			light.cosOuter = std::cos(outer);
			light.cosInner = std::cos(inner);
		}
		scene.lights.push_back(light);
	}
}

inline void readBoxes(const nlohmann::json& root, BakeScene& scene, std::string& error)
{
	const auto boxes = root.find("boxes");
	if (boxes == root.end() || !boxes->is_array()) { return; }
	for (const auto& b : *boxes)
	{
		Vec3 lo{}, hi{};
		if (!readVec3(b, "min", lo) || !readVec3(b, "max", hi)) { continue; }
		BakeMaterial m;
		m.albedo = readColor(b, "color", {0.8f, 0.8f, 0.8f}, 1.0f, error);
		m.emissive = readColor(b, "emissive", {}, readFloat(b, "emissiveIntensity", 1.0f), error);
		scene.addBox(lo, hi, scene.addMaterial(m));
	}
}

/// @brief "terrain": "x.world.json" か {"world": "x.world.json", "step": n}。地形を遮る面と跳ね返す面として足す
[[nodiscard]] inline bool readTerrain(const nlohmann::json& root, const std::filesystem::path& dir, BakeScene& scene, std::string& error)
{
	const auto it = root.find("terrain");
	if (it == root.end()) { return true; }
	std::string world;
	float step = 1.0f;
	if (it->is_string()) { world = it->get<std::string>(); }
	else if (it->is_object())
	{
		world = it->value("world", std::string());
		step = readFloat(*it, "step", 1.0f);
	}
	if (world.empty() || !(step >= 1.0f && step <= 64.0f))
	{
		error = "terrain には world.json のパスか {\"world\": \"x.world.json\", \"step\": 1..64} を書く";
		return false;
	}
	return addTerrainWorld(scene, dir / std::filesystem::u8path(world), static_cast<std::uint32_t>(step), error);
}

/// @brief "probes": { min, max, spacing (数か 3 要素) } から格子を作る
[[nodiscard]] inline bool readGrid(const nlohmann::json& root, ProbeGridDesc& grid, std::string& error)
{
	const auto p = root.find("probes");
	if (p == root.end()) { return false; }
	Vec3 lo{}, hi{};
	Vec3 spacing{1.0f, 1.0f, 1.0f};
	if (!readVec3(*p, "min", lo) || !readVec3(*p, "max", hi))
	{
		error = "probes に min と max が要る";
		return false;
	}
	if (!readVec3(*p, "spacing", spacing))
	{
		const float s = readFloat(*p, "spacing", 1.0f);
		spacing = {s, s, s};
	}
	const float ext[3] = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
	const float sp[3] = {spacing.x, spacing.y, spacing.z};
	double total = 1.0;
	for (int a = 0; a < 3; ++a)
	{
		if (!(sp[a] > 0.0f) || !(ext[a] >= 0.0f))
		{
			error = "probes の spacing は正、max は min 以上にする";
			return false;
		}
		const double n = std::floor(static_cast<double>(ext[a]) / sp[a] + 1e-4) + 1.0;
		total *= n;
		if (n > 4096.0 || total > static_cast<double>(1u << 20))
		{
			error = "プローブが多すぎる (1 軸 4096 個、全部で 104 万個まで)";
			return false;
		}
		grid.dims[a] = static_cast<std::uint32_t>(n);
	}
	grid.origin = lo;
	grid.spacing = spacing;
	return true;
}

inline void readReflections(const nlohmann::json& root, std::vector<ReflectionProbeDesc>& out)
{
	const auto list = root.find("reflections");
	if (list == root.end() || !list->is_array()) { return; }
	for (const auto& r : *list)
	{
		ReflectionProbeDesc d;
		if (!readVec3(r, "min", d.boxMin) || !readVec3(r, "max", d.boxMax)) { continue; }
		if (!readVec3(r, "position", d.position)) { d.position = (d.boxMin + d.boxMax) * 0.5f; }
		d.blend = readFloat(r, "blend", 0.5f);
		out.push_back(d);
	}
}

} // namespace detail

/// @brief json を読んで、焼く準備 (場面の BVH まで) を済ませる。読めなければ nullopt と理由
[[nodiscard]] inline std::optional<LightingBakeJob> loadLightingBakeJobUnchecked(const std::filesystem::path& jsonPath,
                                                                                  std::string& error)
{
	std::ifstream in(jsonPath);
	const nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
	if (!in.good() && !in.eof()) { error = "開けない: " + jsonPath.string(); return std::nullopt; }
	if (!root.is_object()) { error = "json のオブジェクトではない: " + jsonPath.string(); return std::nullopt; }
	const std::filesystem::path dir = jsonPath.parent_path();
	LightingBakeJob job;
	if (const auto level = root.find("level"); level != root.end() && level->is_string())
	{
		if (!addGltfLevel(job.scene, dir / level->get<std::string>(), detail::readFloat(root, "scale", 1.0f), error)) { return std::nullopt; }
	}
	if (!detail::readTerrain(root, dir, job.scene, error)) { return std::nullopt; }
	detail::readBoxes(root, job.scene, error);
	detail::readLights(root, job.scene, error);
	if (!error.empty()) { return std::nullopt; }
	job.hasGrid = detail::readGrid(root, job.grid, error);
	if (!error.empty()) { return std::nullopt; }
	detail::readReflections(root, job.reflections);
	if (!job.hasGrid && job.reflections.empty()) { error = "probes も reflections も無い"; return std::nullopt; }
	if (job.scene.triangleCount() == 0) { error = "三角形が無い (level か terrain か boxes を書く)"; return std::nullopt; }
	const auto count = [&](const char* key, std::uint32_t fallback, std::uint32_t lo, std::uint32_t hi) {
		const float v = detail::readFloat(root, key, static_cast<float>(fallback));
		return static_cast<std::uint32_t>(std::clamp(v, static_cast<float>(lo), static_cast<float>(hi)));
	};
	job.gi.raysPerProbe = count("rays", job.gi.raysPerProbe, 16, 65536);
	job.gi.bounces = count("bounces", job.gi.bounces, 1, 16);
	job.reflection.size = count("reflectionSize", job.reflection.size, 4, 1024);
	std::string out = root.value("out", std::string());
	if (out.empty())
	{
		std::string stem = jsonPath.filename().string();
		const auto dot = stem.find(".lighting.json");
		out = (dot != std::string::npos ? stem.substr(0, dot) : jsonPath.stem().string()) + ".lighting.bin";
	}
	job.output = dir / out;
	job.scene.build();
	return job;
}

/// @brief json を読んで、焼く準備 (場面の BVH まで) を済ませる。読めなければ nullopt と理由 (例外は投げない)
[[nodiscard]] inline std::optional<LightingBakeJob> loadLightingBakeJob(const std::filesystem::path& jsonPath, std::string& error)
{
	try
	{
		return loadLightingBakeJobUnchecked(jsonPath, error);
	}
	catch (const std::exception& e)
	{
		error = std::string("lighting.json の値の型が違う: ") + e.what();
		return std::nullopt;
	}
}

/// @brief 格子を焼き、その格子の間接光を使って反射のプローブを焼く
[[nodiscard]] inline LightingBake runLightingBake(const LightingBakeJob& job)
{
	LightingBake bake;
	if (job.hasGrid)
	{
		bake.volume = bakeProbeVolume(job.scene, job.grid, job.gi);
		bake.hasVolume = true;
	}
	for (const ReflectionProbeDesc& d : job.reflections)
	{
		bake.reflections.push_back(bakeReflectionProbe(job.scene, bake.hasVolume ? &bake.volume : nullptr, d, job.reflection));
	}
	return bake;
}

} // namespace mitiru::render::gi
