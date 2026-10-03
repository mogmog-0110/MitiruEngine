#pragma once

/// @file BakeScene.hpp
/// @brief 光を焼くための場面。三角形と材質 (線形の反射率・自発光)、太陽、局所光、空。レイは三角形の BVH で引く
/// @details 光の強さはエンジンの描画と同じ約束にする。光の色 x pi を放射照度とみなすので、強さ 1 の白い光が
///          正面から当たった白い面の放射輝度は 1 になる。局所光の減衰は PBR の局所光と同じ式
///          (範囲の窓 x スポットの円錐 x 1 / (距離^2 + 1))。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <mitiru/action/TriangleBvh.hpp>
#include <mitiru/render/gi/ProbeMath.hpp>

namespace mitiru::render::gi
{

struct BakeMaterial
{
	Vec3 albedo{0.8f, 0.8f, 0.8f};  ///< 線形の反射率
	Vec3 emissive{};                ///< 線形の放射輝度
	bool doubleSided = false;       ///< 真なら裏から当たっても表として扱う (面の中に埋まったとは数えない)
};

struct BakeLight
{
	Vec3 position{};
	Vec3 direction{0.0f, -1.0f, 0.0f};  ///< スポットの向き
	float range = 10.0f;
	Vec3 color{1.0f, 1.0f, 1.0f};       ///< 線形の色 x 強さ
	float cosOuter = -2.0f;             ///< -2 以下なら点光源
	float cosInner = -1.0f;
};

struct BakeSun
{
	bool enabled = false;
	Vec3 direction{0.0f, -1.0f, 0.0f};  ///< 光の進む向き (Light::direction と同じ)
	Vec3 color{1.0f, 1.0f, 1.0f};       ///< 線形の色 x 強さ
};

/// @brief 空の放射輝度。上半球は地平線から天頂へ、下半球は地面の色
struct BakeSky
{
	Vec3 zenith{};
	Vec3 horizon{};
	Vec3 ground{};

	[[nodiscard]] Vec3 radiance(Vec3 dir) const noexcept
	{
		if (dir.y < 0.0f) { return ground; }
		const float t = std::sqrt(dir.y);
		return horizon + (zenith - horizon) * t;
	}
};

struct BakeHit
{
	float t = 0.0f;
	std::uint32_t triangle = 0;
	Vec3 normal{};       ///< 三角形の表の向き (頂点の並びが反時計回りに見える側)
	bool backface = false;
};

class BakeScene
{
public:
	BakeSun sun;
	BakeSky sky;
	std::vector<BakeLight> lights;

	[[nodiscard]] std::uint32_t addMaterial(const BakeMaterial& m)
	{
		m_materials.push_back(m);
		return static_cast<std::uint32_t>(m_materials.size() - 1);
	}

	/// @brief 反時計回りに見える側が表
	void addTriangle(Vec3 a, Vec3 b, Vec3 c, std::uint32_t material)
	{
		const Vec3 n = cross(b - a, c - a);
		if (!(dot(n, n) > 1e-24f)) { return; }
		m_tris.push_back({a, b, c, normalize(n), material});
		m_built = false;
	}

	/// @brief 外向きの面 12 枚の箱
	void addBox(Vec3 lo, Vec3 hi, std::uint32_t material)
	{
		const Vec3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
		                   {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
		static constexpr int kQuads[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
		for (const auto& q : kQuads)
		{
			addTriangle(p[q[0]], p[q[1]], p[q[2]], material);
			addTriangle(p[q[0]], p[q[2]], p[q[3]], material);
		}
	}

	/// @brief BVH を作る。三角形を足し終えてから 1 回呼ぶ
	void build()
	{
		std::vector<action::BvhTriangle> tris;
		tris.reserve(m_tris.size());
		for (std::uint32_t i = 0; i < m_tris.size(); ++i)
		{
			tris.push_back({toBvh(m_tris[i].a), toBvh(m_tris[i].b), toBvh(m_tris[i].c), i, 0});
		}
		m_bvh = action::TriangleBvh(std::move(tris));
		m_built = true;
	}

	[[nodiscard]] bool built() const noexcept { return m_built; }
	[[nodiscard]] std::size_t triangleCount() const noexcept { return m_tris.size(); }
	[[nodiscard]] const BakeMaterial& materialOf(std::uint32_t triangle) const noexcept
	{
		return m_materials[m_tris[triangle].material];
	}

	/// @brief 一番近い当たり。裏面も当てる (プローブが面の中に埋まったかを数えるため)
	[[nodiscard]] bool trace(Vec3 o, Vec3 d, float tMax, BakeHit& hit) const
	{
		bool found = false;
		m_bvh.traverseRay(toBvh(o), toBvh(d), tMax, action::Vec3{}, [&](const action::BvhTriangle& tri, float best) {
			float t = 0.0f;
			if (action::geom::rayTriangle(toBvh(o), toBvh(d), tri.a, tri.b, tri.c, best, false, t) && t < best)
			{
				hit.t = t;
				hit.triangle = tri.id;
				found = true;
				return t;
			}
			return best;
		});
		if (found)
		{
			const Tri& tri = m_tris[hit.triangle];
			hit.normal = tri.normal;
			hit.backface = dot(hit.normal, d) > 0.0f;
			if (hit.backface && m_materials[tri.material].doubleSided)
			{
				hit.normal = hit.normal * -1.0f;
				hit.backface = false;
			}
		}
		return found;
	}

	/// @brief o から d の向きに tMax までの間に何かあるか
	[[nodiscard]] bool occluded(Vec3 o, Vec3 d, float tMax) const
	{
		BakeHit h;
		return trace(o, d, tMax, h);
	}

	/// @brief 面 (位置 p、表の向き n) が太陽と局所光から直接受ける放射照度。影はレイで調べる
	[[nodiscard]] Vec3 directIrradiance(Vec3 p, Vec3 n) const
	{
		constexpr float kOffset = 1e-3f;
		const Vec3 origin = p + n * kOffset;
		Vec3 e{};
		if (sun.enabled)
		{
			const Vec3 l = normalize(sun.direction * -1.0f);
			const float ndl = dot(n, l);
			if (ndl > 0.0f && !occluded(origin, l, 1e6f)) { e = e + sun.color * (kPi * ndl); }
		}
		for (const BakeLight& light : lights) { e = e + localIrradiance(light, p, n, origin); }
		return e;
	}

private:
	struct Tri
	{
		Vec3 a, b, c, normal;
		std::uint32_t material;
	};

	[[nodiscard]] static action::Vec3 toBvh(Vec3 v) noexcept { return {v.x, v.y, v.z}; }

	[[nodiscard]] Vec3 localIrradiance(const BakeLight& light, Vec3 p, Vec3 n, Vec3 origin) const
	{
		const Vec3 toLight = light.position - p;
		const float dist2 = dot(toLight, toLight);
		const float r2 = light.range * light.range;
		if (!(dist2 < r2) || !(dist2 > 1e-12f)) { return {}; }
		const float dist = std::sqrt(dist2);
		const Vec3 l = toLight * (1.0f / dist);
		const float ndl = dot(n, l);
		if (ndl <= 0.0f) { return {}; }
		const float win = std::fmax(0.0f, 1.0f - (dist2 * dist2) / (r2 * r2));
		float spot = 1.0f;
		if (light.cosOuter > -1.5f)
		{
			const float scale = 1.0f / std::fmax(light.cosInner - light.cosOuter, 1e-4f);
			const float c = dot(l * -1.0f, normalize(light.direction));
			spot = std::clamp((c - light.cosOuter) * scale, 0.0f, 1.0f);
		}
		const float shape = win * win * spot * spot;
		if (shape <= 0.0f || occluded(origin, l, dist - 2e-3f)) { return {}; }
		return light.color * (kPi * ndl * shape / (dist2 + 1.0f));
	}

	std::vector<Tri> m_tris;
	std::vector<BakeMaterial> m_materials;
	action::TriangleBvh m_bvh;
	bool m_built = false;
};

} // namespace mitiru::render::gi
