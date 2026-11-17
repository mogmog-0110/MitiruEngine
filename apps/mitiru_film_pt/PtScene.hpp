#pragma once

// glb と場面 (FilmScene.hpp の JSON) を、パストレーサーが読む三角形・材質・光の表にする。
// 三角形は世界の座標へ焼いて 1 本の BLAS にする (試作)。インスタンス化とスキンは ADR 0154 の「足りないもの」。

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/render/GltfLoader.hpp>

namespace film_pt
{

struct PtMaterial
{
	float         baseColor[4];
	float         emissive[3];
	std::uint32_t texOffset;
	std::uint32_t texW;
	std::uint32_t texH;
	std::uint32_t alphaMode;   // 0 opaque, 1 mask, 2 blend
	float         alphaCutoff;
};

struct PtLight
{
	float         pos[3];
	float         radius;
	float         color[3];
	std::uint32_t kind;   // 0 sun, 1 point
	float         dir[3];
	float         pad;
};

struct PtCamera
{
	float eye[3], right[3], up[3], fwd[3];
	float tanHalf = 0.5f, aspect = 1.0f, focusDist = 0.0f, apertureR = 0.0f;
};

struct PtScene
{
	std::vector<float>         verts;     // 頂点ごとに 8 float: (pos.xyz, u), (nrm.xyz, v)
	std::vector<std::uint32_t> indices;
	std::vector<std::uint32_t> triMat;
	std::vector<std::uint32_t> texels;    // RGBA8 (sRGB バイトのまま)
	std::vector<PtMaterial>    materials;
	std::vector<PtLight>       lights;
	PtCamera                   camera;
	int                        width = 1280, height = 720;
	float                      ambient[3] = {0, 0, 0};
	float                      exposure = 1.0f;
};

namespace detail
{

struct Mat3x4
{
	float m[3][4];
};

inline Mat3x4 mul(const Mat3x4& a, const Mat3x4& b)
{
	Mat3x4 r{};
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			for (int k = 0; k < 3; ++k) { r.m[i][j] += a.m[i][k] * b.m[k][j]; }
			if (j == 3) { r.m[i][3] += a.m[i][3]; }
		}
	}
	return r;
}

inline Mat3x4 fromJson(const nlohmann::json& a)
{
	Mat3x4 r{};
	for (int i = 0; i < 12; ++i) { r.m[i / 4][i % 4] = a[static_cast<std::size_t>(i)].get<float>(); }
	return r;
}

inline Mat3x4 nodeLocal(const mitiru::render::GltfNode& n)
{
	const float x = n.rotation.x, y = n.rotation.y, z = n.rotation.z, w = n.rotation.w;
	const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
	                       {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
	                       {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
	const float s[3] = {n.scale.x, n.scale.y, n.scale.z}, t[3] = {n.translation.x, n.translation.y, n.translation.z};
	Mat3x4 m{};
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j) { m.m[i][j] = r[i][j] * s[j]; }
		m.m[i][3] = t[i];
	}
	return m;
}

inline Mat3x4 nodeWorld(const std::vector<mitiru::render::GltfNode>& nodes, int index)
{
	Mat3x4 m = nodeLocal(nodes[static_cast<std::size_t>(index)]);
	for (int p = nodes[static_cast<std::size_t>(index)].parent; p >= 0; p = nodes[static_cast<std::size_t>(p)].parent)
	{
		m = mul(nodeLocal(nodes[static_cast<std::size_t>(p)]), m);
	}
	return m;
}

/// 法線は余因子行列で送る (拡大が一様でなくても向きが合う)
inline void cofactorTimes(const Mat3x4& a, const float n[3], float out[3])
{
	const auto& m = a.m;
	const float c[3][3] = {{m[1][1] * m[2][2] - m[1][2] * m[2][1], m[1][2] * m[2][0] - m[1][0] * m[2][2], m[1][0] * m[2][1] - m[1][1] * m[2][0]},
	                       {m[0][2] * m[2][1] - m[0][1] * m[2][2], m[0][0] * m[2][2] - m[0][2] * m[2][0], m[0][1] * m[2][0] - m[0][0] * m[2][1]},
	                       {m[0][1] * m[1][2] - m[0][2] * m[1][1], m[0][2] * m[1][0] - m[0][0] * m[1][2], m[0][0] * m[1][1] - m[0][1] * m[1][0]}};
	float len = 0;
	for (int i = 0; i < 3; ++i)
	{
		out[i] = c[i][0] * n[0] + c[i][1] * n[1] + c[i][2] * n[2];
		len += out[i] * out[i];
	}
	len = std::sqrt(len);
	for (int i = 0; i < 3; ++i) { out[i] = len > 0 ? out[i] / len : (i == 1 ? 1.0f : 0.0f); }
}

inline std::uint32_t addMaterial(PtScene& s, const mitiru::render::GltfMaterialData* g, const nlohmann::json& prop)
{
	PtMaterial m{};
	m.baseColor[0] = m.baseColor[1] = m.baseColor[2] = m.baseColor[3] = 1.0f;
	m.alphaCutoff = 0.5f;
	if (g != nullptr)
	{
		m.baseColor[0] = g->baseColor.r; m.baseColor[1] = g->baseColor.g; m.baseColor[2] = g->baseColor.b; m.baseColor[3] = g->baseColor.a;
		for (int i = 0; i < 3; ++i) { m.emissive[i] = g->emissiveFactor[i]; }
		m.alphaMode = g->alphaMode == mitiru::render::GltfAlphaMode::Mask ? 1u : (g->alphaMode == mitiru::render::GltfAlphaMode::Blend ? 2u : 0u);
		m.alphaCutoff = g->alphaCutoff;
		if (g->baseColorTexture.valid())
		{
			m.texOffset = static_cast<std::uint32_t>(s.texels.size());
			m.texW = static_cast<std::uint32_t>(g->baseColorTexture.width);
			m.texH = static_cast<std::uint32_t>(g->baseColorTexture.height);
			const auto& px = g->baseColorTexture.rgba;
			for (std::size_t i = 0; i + 3 < px.size(); i += 4)
			{
				s.texels.push_back(px[i] | (px[i + 1] << 8) | (px[i + 2] << 16) | (static_cast<std::uint32_t>(px[i + 3]) << 24));
			}
		}
	}
	const auto& c = prop["color"];
	for (int i = 0; i < 3; ++i) { m.baseColor[i] *= c[static_cast<std::size_t>(i)].get<float>(); }
	m.baseColor[3] *= prop["alpha"].get<float>();
	s.materials.push_back(m);
	return static_cast<std::uint32_t>(s.materials.size() - 1);
}

inline bool addProp(PtScene& s, const nlohmann::json& prop, const std::string& path)
{
	using namespace mitiru::render;
	const auto scene = loadGltfSceneFromFile(path);
	if (!scene) { std::fprintf(stderr, "警告: %s を読めません\n", path.c_str()); return false; }
	const Mat3x4 propWorld = fromJson(prop["matrix"]);
	std::vector<std::uint32_t> matId(scene->materials.size() + 1, 0xFFFFFFFFu);
	for (std::size_t ni = 0; ni < scene->nodes.size(); ++ni)
	{
		const int mi = scene->nodes[ni].mesh;
		if (mi < 0 || static_cast<std::size_t>(mi) >= scene->meshes.size()) { continue; }
		const Mat3x4 world = mul(propWorld, nodeWorld(scene->nodes, static_cast<int>(ni)));
		for (const auto& prim : scene->meshes[static_cast<std::size_t>(mi)].primitives)
		{
			const std::size_t slot = prim.materialIndex < 0 ? scene->materials.size() : static_cast<std::size_t>(prim.materialIndex);
			if (matId[slot] == 0xFFFFFFFFu) { matId[slot] = addMaterial(s, prim.materialIndex < 0 ? nullptr : &scene->materials[slot], prop); }
			const std::uint32_t base = static_cast<std::uint32_t>(s.verts.size() / 8);
			for (const auto& v : prim.vertices)
			{
				const float p[3] = {v.position.x, v.position.y, v.position.z}, n[3] = {v.normal.x, v.normal.y, v.normal.z};
				float wn[3];
				cofactorTimes(world, n, wn);
				for (int i = 0; i < 3; ++i) { s.verts.push_back(world.m[i][0] * p[0] + world.m[i][1] * p[1] + world.m[i][2] * p[2] + world.m[i][3]); }
				s.verts.push_back(v.texCoord.x);
				for (int i = 0; i < 3; ++i) { s.verts.push_back(wn[i]); }
				s.verts.push_back(v.texCoord.y);
			}
			for (std::size_t i = 0; i + 2 < prim.indices.size(); i += 3)
			{
				for (int k = 0; k < 3; ++k) { s.indices.push_back(base + prim.indices[i + static_cast<std::size_t>(k)]); }
				s.triMat.push_back(matId[slot]);
			}
		}
	}
	return true;
}

}  // namespace detail

/// @param modelRoots モデルのパス (assets からの相対) を探す起点
inline PtScene buildScene(const nlohmann::json& scene, const std::vector<std::string>& modelRoots, float wattScale)
{
	namespace fs = std::filesystem;
	PtScene s;
	s.width = scene["size"][0].get<int>();
	s.height = scene["size"][1].get<int>();
	for (const auto& p : scene["props"])
	{
		const std::string model = p["model"].get<std::string>();
		if (model.empty()) { std::fprintf(stderr, "警告: %s は組み込みの形で、この試作は描きません\n", p["name"].get<std::string>().c_str()); continue; }
		fs::path found;
		for (const auto& r : modelRoots)
		{
			std::error_code ec;
			if (fs::is_regular_file(fs::path(r) / model, ec)) { found = fs::path(r) / model; break; }
		}
		if (found.empty()) { std::fprintf(stderr, "警告: モデルが見つかりません: %s\n", model.c_str()); continue; }
		detail::addProp(s, p, found.generic_string());
	}
	const float kPi = 3.14159265f;
	for (const auto& l : scene["lights"])
	{
		PtLight pl{};
		const bool sun = l["kind"].get<std::string>() == "sun";
		pl.kind = sun ? 0u : 1u;
		pl.radius = 0.05f;
		// Cycles の W と同じ単位にそろえる (較正: 点光 E = P / (4 pi d^2)、太陽 E = 強さ)
		const float k = l["intensity"].get<float>() * (sun ? 1.0f : wattScale / (4.0f * kPi));
		for (int i = 0; i < 3; ++i)
		{
			pl.pos[i] = l["pos"][static_cast<std::size_t>(i)].get<float>();
			pl.dir[i] = l["dir"][static_cast<std::size_t>(i)].get<float>();
			pl.color[i] = l["color"][static_cast<std::size_t>(i)].get<float>() * k;
		}
		if (l["kind"].get<std::string>() == "spot") { std::fprintf(stderr, "警告: スポットは点光として扱います\n"); }
		s.lights.push_back(pl);
	}
	if (s.lights.empty()) { s.lights.push_back(PtLight{}); }
	const auto& c = scene["camera"];
	float eye[3], tgt[3], up[3];
	for (int i = 0; i < 3; ++i)
	{
		eye[i] = c["eye"][static_cast<std::size_t>(i)].get<float>();
		tgt[i] = c["target"][static_cast<std::size_t>(i)].get<float>();
		up[i] = c["up"][static_cast<std::size_t>(i)].get<float>();
	}
	const auto norm = [](float v[3]) { const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); for (int i = 0; i < 3; ++i) { v[i] /= l; } };
	const auto cross = [](const float a[3], const float b[3], float o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; };
	float fwd[3] = {tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2]};
	norm(fwd);
	float right[3], tu[3];
	cross(fwd, up, right);
	norm(right);
	cross(right, fwd, tu);
	for (int i = 0; i < 3; ++i) { s.camera.eye[i] = eye[i]; s.camera.fwd[i] = fwd[i]; s.camera.right[i] = right[i]; s.camera.up[i] = tu[i]; }
	s.camera.tanHalf = std::tan(c["fovDeg"].get<float>() * kPi / 360.0f);
	s.camera.aspect = static_cast<float>(s.width) / static_cast<float>(s.height);
	s.camera.focusDist = c["focusDist"].get<float>();
	s.camera.apertureR = c["apertureRadius"].get<float>();
	const auto& w = scene["world"];
	// 空と地面があれば平均する (Cycles のスクリプトと同じ。平らな環境光しか持たない)
	const auto nonZero = [&](const char* key) { for (int i = 0; i < 3; ++i) { if (w[key][static_cast<std::size_t>(i)].get<float>() != 0.0f) { return true; } } return false; };
	const bool hemi = nonZero("ambientSky") || nonZero("ambientGround");
	for (std::size_t i = 0; i < 3; ++i)
	{
		s.ambient[i] = hemi ? 0.5f * (w["ambientSky"][i].get<float>() + w["ambientGround"][i].get<float>()) : w["ambient"][i].get<float>();
	}
	s.exposure = w["exposure"].get<float>();
	return s;
}

}  // namespace film_pt
