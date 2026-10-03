#pragma once

/// @file GiBaker.hpp
/// @brief 放射照度のプローブの格子を CPU のレイで焼く
/// @details プローブごとに球面フィボナッチの向き (プローブの番号から決まる回転を掛ける) へレイを飛ばし、
///          当たった面の放射輝度 (直接光 x 反射率 / pi + 自発光 + 前の回の格子から引いた間接光 x 反射率 / pi) と
///          外れた向きの空を SH L2 へ射影する。これを bounces 回繰り返すと光が bounces 回跳ね返る。
///          乱数の状態を持たず、プローブごとの結果は他のプローブに依存しないので、スレッドの数に関わらず
///          同じ入力から同じバイト列が出る。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include <mitiru/render/gi/BakeScene.hpp>
#include <mitiru/render/gi/ProbeVolume.hpp>

namespace mitiru::render::gi
{

struct GiBakeSettings
{
	std::uint32_t raysPerProbe = 256;
	std::uint32_t bounces = 3;          ///< 光の跳ね返りの回数 (1 = 空と、直接光を受けた面だけ)
	std::uint32_t threads = 0;          ///< 0 = CPU の数
	float backfaceInvalid = 0.25f;      ///< 裏面に当たったレイがこの割合を超えたプローブは面の中とみなす
	float visibilitySharpness = 20.0f;  ///< 距離の地図の 1 画素がレイを集める余弦の指数
};

namespace detail
{

/// @brief [0, count) を threads 本に分けて fn(i) を呼ぶ。各 i の結果は i の場所にだけ書く前提
template <class Fn>
void parallelFor(std::uint32_t count, std::uint32_t threads, Fn&& fn)
{
	if (threads == 0) { threads = std::max(1u, std::thread::hardware_concurrency()); }
	threads = std::min(threads, std::max(count, 1u));
	if (threads <= 1)
	{
		for (std::uint32_t i = 0; i < count; ++i) { fn(i); }
		return;
	}
	std::vector<std::thread> pool;
	pool.reserve(threads);
	for (std::uint32_t t = 0; t < threads; ++t)
	{
		pool.emplace_back([&, t] {
			for (std::uint32_t i = t; i < count; i += threads) { fn(i); }
		});
	}
	for (auto& th : pool) { th.join(); }
}

/// @brief プローブ i のレイの向きの回転 (番号のハッシュから作る一様な回転、Shoemake の方法)
struct Rotation
{
	Vec3 x, y, z;
	[[nodiscard]] Vec3 apply(Vec3 v) const noexcept { return x * v.x + y * v.y + z * v.z; }
};

[[nodiscard]] inline Rotation probeRotation(std::uint32_t i) noexcept
{
	const float u1 = hash01(i * 3u + 0x9e3779b9u);
	const float u2 = hash01(i * 3u + 1u + 0x85ebca6bu) * 2.0f * kPi;
	const float u3 = hash01(i * 3u + 2u + 0xc2b2ae35u) * 2.0f * kPi;
	const float a = std::sqrt(1.0f - u1), b = std::sqrt(u1);
	const float qx = a * std::sin(u2), qy = a * std::cos(u2), qz = b * std::sin(u3), qw = b * std::cos(u3);
	return {{1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qw * qz), 2 * (qx * qz - qw * qy)},
	        {2 * (qx * qy - qw * qz), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qw * qx)},
	        {2 * (qx * qz + qw * qy), 2 * (qy * qz - qw * qx), 1 - 2 * (qx * qx + qy * qy)}};
}

/// @brief 1 本のレイの当たり。面の放射輝度のうち、回をまたいで変わらない分 (直接光と自発光) を持つ
struct RayRecord
{
	Vec3 dir{};
	float t = 0.0f;
	std::uint32_t triangle = 0;
	std::uint8_t kind = 0;   ///< 0 = 外れ、1 = 表、2 = 裏
	Vec3 normal{};           ///< 当たった面の表の向き
	Vec3 fixedRadiance{};
};

[[nodiscard]] inline RayRecord traceProbeRay(const BakeScene& scene, Vec3 origin, Vec3 dir)
{
	RayRecord rec;
	rec.dir = dir;
	BakeHit hit;
	if (!scene.trace(origin, dir, 1e6f, hit)) { return rec; }
	rec.t = hit.t;
	rec.triangle = hit.triangle;
	rec.kind = hit.backface ? 2 : 1;
	rec.normal = hit.normal;
	if (!hit.backface)
	{
		const BakeMaterial& m = scene.materialOf(hit.triangle);
		const Vec3 e = scene.directIrradiance(origin + dir * hit.t, hit.normal);
		rec.fixedRadiance = mul(m.albedo, e) * (1.0f / kPi) + m.emissive;
	}
	return rec;
}

/// @brief 縁の 1 画素に、八面体の折り返しの向こう側の内側の画素を写す (双線形で縁をまたいでも続くように)
inline void fillVisibilityBorder(ProbeVolume& vol, std::uint32_t probe)
{
	std::uint32_t ox = 0, oy = 0;
	vol.tileOrigin(probe, ox, oy);
	const std::uint32_t w = vol.atlasWidth();
	constexpr std::uint32_t I = kVisibilityInterior;
	const auto at = [&](std::uint32_t x, std::uint32_t y) { return (static_cast<std::size_t>(oy + y) * w + ox + x) * 2; };
	const auto copy = [&](std::uint32_t dx, std::uint32_t dy, std::uint32_t sx, std::uint32_t sy) {
		vol.visibility[at(dx, dy)] = vol.visibility[at(sx, sy)];
		vol.visibility[at(dx, dy) + 1] = vol.visibility[at(sx, sy) + 1];
	};
	for (std::uint32_t i = 1; i <= I; ++i)
	{
		copy(i, 0, I + 1 - i, 1);
		copy(i, I + 1, I + 1 - i, I);
		copy(0, i, 1, I + 1 - i);
		copy(I + 1, i, I, I + 1 - i);
	}
	copy(0, 0, I, I);
	copy(I + 1, 0, 1, I);
	copy(0, I + 1, I, 1);
	copy(I + 1, I + 1, 1, 1);
}

/// @brief 距離の地図 1 枚 (内側 + 縁) を埋める
inline void fillVisibilityTile(ProbeVolume& vol, std::uint32_t probe, const RayRecord* rays, std::uint32_t count, float maxDistance,
                               float sharpness)
{
	std::uint32_t ox = 0, oy = 0;
	vol.tileOrigin(probe, ox, oy);
	const std::uint32_t w = vol.atlasWidth();
	for (std::uint32_t ty = 0; ty < kVisibilityInterior; ++ty)
	{
		for (std::uint32_t tx = 0; tx < kVisibilityInterior; ++tx)
		{
			const float u = (static_cast<float>(tx) + 0.5f) / kVisibilityInterior * 2.0f - 1.0f;
			const float v = (static_cast<float>(ty) + 0.5f) / kVisibilityInterior * 2.0f - 1.0f;
			const Vec3 texDir = octDecode(u, v);
			float wsum = 0.0f, m1 = 0.0f, m2 = 0.0f;
			for (std::uint32_t r = 0; r < count; ++r)
			{
				const float c = dot(texDir, rays[r].dir);
				if (c <= 0.0f) { continue; }
				const float weight = std::pow(c, sharpness);
				const float d = rays[r].kind == 0 ? maxDistance : std::min(rays[r].t, maxDistance);
				wsum += weight;
				m1 += weight * d;
				m2 += weight * d * d;
			}
			const std::size_t k = (static_cast<std::size_t>(oy + 1 + ty) * w + ox + 1 + tx) * 2;
			vol.visibility[k] = wsum > 0.0f ? m1 / wsum : maxDistance;
			vol.visibility[k + 1] = wsum > 0.0f ? m2 / wsum : maxDistance * maxDistance;
		}
	}
	fillVisibilityBorder(vol, probe);
}

/// @brief プローブ 1 個の SH を、記録したレイと前の回の格子 (null なら間接光なし) から作る
[[nodiscard]] inline ShL2Rgb projectProbe(const BakeScene& scene, const ProbeVolume* previous, Vec3 origin, const RayRecord* rays,
                                          std::uint32_t count, float bias)
{
	ShL2Rgb sh;
	const float weight = 4.0f * kPi / static_cast<float>(count);
	for (std::uint32_t r = 0; r < count; ++r)
	{
		const RayRecord& rec = rays[r];
		Vec3 radiance{};
		if (rec.kind == 0) { radiance = scene.sky.radiance(rec.dir); }
		else if (rec.kind == 1)
		{
			radiance = rec.fixedRadiance;
			Vec3 indirect{};
			if (previous != nullptr &&
			    sampleIrradiance(*previous, origin + rec.dir * rec.t, rec.normal, rec.dir * -1.0f, bias, indirect))
			{
				radiance = radiance + mul(scene.materialOf(rec.triangle).albedo, indirect) * (1.0f / kPi);
			}
		}
		sh.addRadiance(rec.dir, radiance, weight);
	}
	return sh.convolvedToIrradiance();
}

} // namespace detail

/// @brief 格子 grid のプローブを焼く。scene は build 済みであること
[[nodiscard]] inline ProbeVolume bakeProbeVolume(const BakeScene& scene, const ProbeGridDesc& grid, const GiBakeSettings& s = {})
{
	ProbeVolume vol;
	vol.allocate(grid);
	const std::uint32_t probes = grid.count();
	const std::uint32_t rays = std::max(16u, s.raysPerProbe);
	const float maxDistance = 1.5f * length(grid.spacing);
	std::vector<detail::RayRecord> records(static_cast<std::size_t>(probes) * rays);
	detail::parallelFor(probes, s.threads, [&](std::uint32_t i) {
		const detail::Rotation rot = detail::probeRotation(i);
		const Vec3 origin = grid.position(i);
		detail::RayRecord* rec = records.data() + static_cast<std::size_t>(i) * rays;
		std::uint32_t back = 0;
		for (std::uint32_t r = 0; r < rays; ++r)
		{
			rec[r] = detail::traceProbeRay(scene, origin, rot.apply(sphericalFibonacci(r, rays)));
			back += rec[r].kind == 2 ? 1u : 0u;
		}
		vol.valid[i] = static_cast<float>(back) > s.backfaceInvalid * static_cast<float>(rays) ? 0 : 1;
		detail::fillVisibilityTile(vol, i, rec, rays, maxDistance, s.visibilitySharpness);
	});
	const float bias = defaultProbeBias(grid);
	ProbeVolume previous = vol;
	for (std::uint32_t bounce = 0; bounce < std::max(1u, s.bounces); ++bounce)
	{
		const ProbeVolume* prev = bounce == 0 ? nullptr : &previous;
		detail::parallelFor(probes, s.threads, [&](std::uint32_t i) {
			vol.irradiance[i] = detail::projectProbe(scene, prev, grid.position(i), records.data() + static_cast<std::size_t>(i) * rays,
			                                         rays, bias);
		});
		previous.irradiance = vol.irradiance;
	}
	return vol;
}

} // namespace mitiru::render::gi
