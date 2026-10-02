#pragma once

/// @file Scatter.hpp
/// @brief 密度マップと規則から、地形の上に岩や木を毎回同じ置き方で撒く
/// @details 揺らした格子の各マスに候補を 1 つ置く。1 マスは 1 / sqrt(perM2) m。
/// マス番号と seed の整数ハッシュで、位置のずれ、配置の有無、向き、大きさを決める。
/// 乱数の状態を持たないため、範囲や密度マップを変えても他のマスの配置は変わらない。
/// host の描画と DLL の当たり判定は、同じ関数から同じ並びを得る。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <mitiru/terrain/Heightfield.hpp>

namespace mitiru::terrain
{

/// @brief 地形の範囲に貼る 8 bit 1 チャンネル画像。0..255 を 0..1 として扱い、行 0 は z の小さい側
struct DensityMap
{
	std::uint32_t             width = 0;
	std::uint32_t             height = 0;
	std::vector<std::uint8_t> values;

	[[nodiscard]] bool valid() const noexcept
	{
		return width > 0 && height > 0 && values.size() == static_cast<std::size_t>(width) * height;
	}

	/// @brief 地形上の (u, v) ∈ [0, 1]² を双線形補間する。空なら 1
	[[nodiscard]] float sample(float u, float v) const noexcept
	{
		if (!valid()) { return 1.0f; }
		const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width - 1);
		const float fz = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(height - 1);
		const auto x0 = static_cast<std::uint32_t>(fx);
		const auto z0 = static_cast<std::uint32_t>(fz);
		const std::uint32_t x1 = std::min(x0 + 1, width - 1);
		const std::uint32_t z1 = std::min(z0 + 1, height - 1);
		const float tx = fx - static_cast<float>(x0);
		const float tz = fz - static_cast<float>(z0);
		const auto at = [&](std::uint32_t x, std::uint32_t z) { return values[static_cast<std::size_t>(z) * width + x] / 255.0f; };
		const float top = at(x0, z0) + (at(x1, z0) - at(x0, z0)) * tx;
		const float bot = at(x0, z1) + (at(x1, z1) - at(x0, z1)) * tx;
		return top + (bot - top) * tz;
	}
};

struct ScatterRule
{
	float         perM2 = 0.02f;        ///< 密度 1 の所の 1 m² あたりの数
	std::uint32_t seed = 1;
	float         scaleMin = 0.8f;
	float         scaleMax = 1.2f;
	float         maxSlopeDeg = 35.0f;  ///< これより急な所には置かない
	float         minHeight = -1.0e9f;  ///< 置ける高さ (世界の y)
	float         maxHeight = 1.0e9f;
	float         sink = 0.0f;          ///< 地面へ沈める深さ (m、大きさを掛ける前)
	float         alignToNormal = 0.0f; ///< 0 = まっすぐ立てる、1 = 地面の法線に沿わせる
	float         regionMinX = -1.0e9f; ///< 撒く範囲 (世界の xz)。地形の範囲と重なる所だけ
	float         regionMinZ = -1.0e9f;
	float         regionMaxX = 1.0e9f;
	float         regionMaxZ = 1.0e9f;
};

/// @brief 配置した物 1 つ。world は行優先の 4 × 4 で、MeshInstance と同じ並び
struct ScatterInstance
{
	float         world[16]{};
	float         position[3]{};
	float         yawRad = 0.0f;
	float         scale = 1.0f;
	std::uint32_t cellIndex = 0;  ///< 格子の通し番号 (ハッシュの入力)。同じ置き方の目印
};

/// @brief lowbias32 による 32 bit 整数ハッシュ。HLSL の草の配置にも同じ式を使う
[[nodiscard]] constexpr std::uint32_t hash32(std::uint32_t x) noexcept
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

[[nodiscard]] constexpr float hash01(std::uint32_t x) noexcept
{
	return static_cast<float>(hash32(x) >> 8) * (1.0f / 16777216.0f);
}

namespace detail
{

[[nodiscard]] inline ScatterInstance placeOne(const Heightfield& h, const ScatterRule& r, float x, float z, std::uint32_t key)
{
	ScatterInstance inst;
	const Vec3 n = h.smoothNormalAt(x, z);
	inst.yawRad = hash01(key ^ 0x51ed270bu) * 6.28318530718f;
	inst.scale = r.scaleMin + (r.scaleMax - r.scaleMin) * hash01(key ^ 0x2545f491u);
	const float y = h.heightAt(x, z) - r.sink * inst.scale;
	inst.position[0] = x;
	inst.position[1] = y;
	inst.position[2] = z;
	// 上方向は法線と真上を混ぜる。前方向は yaw から上方向に直交するように作る
	const float a = std::clamp(r.alignToNormal, 0.0f, 1.0f);
	Vec3 up{n.x * a, 1.0f - a + n.y * a, n.z * a};
	const float ul = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
	up = {up.x / ul, up.y / ul, up.z / ul};
	const Vec3 f0{std::sin(inst.yawRad), 0.0f, std::cos(inst.yawRad)};
	Vec3 right{up.y * f0.z - up.z * f0.y, up.z * f0.x - up.x * f0.z, up.x * f0.y - up.y * f0.x};
	const float rl = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
	right = {right.x / rl, right.y / rl, right.z / rl};
	const Vec3 fwd{right.y * up.z - right.z * up.y, right.z * up.x - right.x * up.z, right.x * up.y - right.y * up.x};
	const float s = inst.scale;
	const float m[16] = {right.x * s, up.x * s, fwd.x * s, x,
	                     right.y * s, up.y * s, fwd.y * s, y,
	                     right.z * s, up.z * s, fwd.z * s, z,
	                     0.0f,        0.0f,     0.0f,      1.0f};
	std::copy(m, m + 16, inst.world);
	return inst;
}

} // namespace detail

/// @brief 規則と密度マップから配置し、out を作り直す。密度マップが無ければ全域を 1 とし、格子の行優先で並べる
inline void scatter(const Heightfield& h, const DensityMap* density, const ScatterRule& r, std::vector<ScatterInstance>& out)
{
	out.clear();
	if (!h.valid() || !(r.perM2 > 0.0f)) { return; }
	const float cell = 1.0f / std::sqrt(r.perM2);
	const float x0 = std::max(h.minX(), r.regionMinX);
	const float z0 = std::max(h.minZ(), r.regionMinZ);
	const float x1 = std::min(h.maxX(), r.regionMaxX);
	const float z1 = std::min(h.maxZ(), r.regionMaxZ);
	if (!(x1 > x0) || !(z1 > z0)) { return; }
	// 格子の原点は地形の角に固定し、範囲を変えても同じ位置に同じ物を配置する
	const auto gx0 = static_cast<std::int64_t>(std::floor((x0 - h.minX()) / cell));
	const auto gz0 = static_cast<std::int64_t>(std::floor((z0 - h.minZ()) / cell));
	const auto gx1 = static_cast<std::int64_t>(std::ceil((x1 - h.minX()) / cell));
	const auto gz1 = static_cast<std::int64_t>(std::ceil((z1 - h.minZ()) / cell));
	const float cosMax = std::cos(r.maxSlopeDeg * 0.017453292519943295f);
	const std::uint32_t rowStride = static_cast<std::uint32_t>(std::ceil(h.placement().sizeX / cell)) + 1u;
	for (std::int64_t gz = gz0; gz < gz1; ++gz)
	{
		for (std::int64_t gx = gx0; gx < gx1; ++gx)
		{
			const auto index = static_cast<std::uint32_t>(gz) * rowStride + static_cast<std::uint32_t>(gx);
			const std::uint32_t key = hash32(index ^ hash32(r.seed));
			const float x = h.minX() + (static_cast<float>(gx) + hash01(key)) * cell;
			const float z = h.minZ() + (static_cast<float>(gz) + hash01(key ^ 0x9e3779b9u)) * cell;
			if (x < x0 || x >= x1 || z < z0 || z >= z1) { continue; }
			const float u = (x - h.minX()) / h.placement().sizeX;
			const float v = (z - h.minZ()) / h.placement().sizeZ;
			const float keep = density != nullptr ? density->sample(u, v) : 1.0f;
			if (hash01(key ^ 0x68e31da4u) >= keep) { continue; }
			const float y = h.heightAt(x, z);
			if (y < r.minHeight || y > r.maxHeight || h.smoothNormalAt(x, z).y < cosMax) { continue; }
			out.push_back(detail::placeOne(h, r, x, z, key));
			out.back().cellIndex = index;
		}
	}
}

} // namespace mitiru::terrain
