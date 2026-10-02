#pragma once

/// @file Heightfield.hpp
/// @brief 高さマップの地形 (作ったら変えない)。高さと法線は、当たり判定に使う三角形と同じ面から求める
/// @details 標本 (ix, iz) は世界座標 (origin.x + ix * cellX, origin.z + iz * cellZ) にある。
///          画像の行 0 は z の小さい側 (Blender の +Y、北) に対応する。
///          1 マスは対角 b-c で 2 枚に分ける (a=(ix,iz) b=(ix+1,iz) c=(ix,iz+1) d=(ix+1,iz+1))。
///          heightAt はこの 2 枚の平面を評価し、CollisionWorld の三角形と同じ値を返す。
///          乱数と時刻を使わず演算順も固定するため、同じバイナリの DLL と host では bit 単位で一致する

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <sgc/math/Vec3.hpp>

namespace mitiru::terrain
{

using Vec3 = sgc::Vec3f;

/// @brief 高さマップを置く世界座標と大きさ。origin は x と z が最小の角で、y は標本 0 の高さ
struct HeightfieldPlacement
{
	float originX = 0.0f;
	float originY = 0.0f;
	float originZ = 0.0f;
	float sizeX   = 256.0f;  ///< x 方向の全長 (m)。標本の数 - 1 マスで割る
	float sizeZ   = 256.0f;
	float height  = 32.0f;   ///< 標本 65535 の高さ (originY から)
};

class Heightfield
{
public:
	Heightfield() = default;

	/// @brief 行優先の標本 (width × depth) から作る。2 × 2 未満または標本数が合わない場合は空のまま返す
	[[nodiscard]] static Heightfield fromSamples(std::uint32_t width, std::uint32_t depth, std::vector<std::uint16_t> samples,
	                                             const HeightfieldPlacement& placement)
	{
		Heightfield h;
		if (width < 2 || depth < 2 || samples.size() != static_cast<std::size_t>(width) * depth) { return h; }
		h.m_width = width;
		h.m_depth = depth;
		h.m_samples = std::move(samples);
		h.m_place = placement;
		h.m_cellX = placement.sizeX / static_cast<float>(width - 1);
		h.m_cellZ = placement.sizeZ / static_cast<float>(depth - 1);
		h.m_scale = placement.height / 65535.0f;
		return h;
	}

	[[nodiscard]] bool valid() const noexcept { return m_width >= 2 && m_depth >= 2; }
	[[nodiscard]] std::uint32_t width() const noexcept { return m_width; }
	[[nodiscard]] std::uint32_t depth() const noexcept { return m_depth; }
	[[nodiscard]] float cellX() const noexcept { return m_cellX; }
	[[nodiscard]] float cellZ() const noexcept { return m_cellZ; }
	[[nodiscard]] const HeightfieldPlacement& placement() const noexcept { return m_place; }
	[[nodiscard]] const std::vector<std::uint16_t>& samples() const noexcept { return m_samples; }

	[[nodiscard]] float minX() const noexcept { return m_place.originX; }
	[[nodiscard]] float minZ() const noexcept { return m_place.originZ; }
	[[nodiscard]] float maxX() const noexcept { return m_place.originX + m_place.sizeX; }
	[[nodiscard]] float maxZ() const noexcept { return m_place.originZ + m_place.sizeZ; }

	[[nodiscard]] bool contains(float x, float z) const noexcept
	{
		return valid() && x >= minX() && x <= maxX() && z >= minZ() && z <= maxZ();
	}

	/// @brief 標本の値。範囲外では端の標本を返す
	[[nodiscard]] std::uint16_t sample(std::int64_t ix, std::int64_t iz) const noexcept
	{
		if (!valid()) { return 0; }
		ix = std::clamp<std::int64_t>(ix, 0, static_cast<std::int64_t>(m_width) - 1);
		iz = std::clamp<std::int64_t>(iz, 0, static_cast<std::int64_t>(m_depth) - 1);
		return m_samples[static_cast<std::size_t>(iz) * m_width + static_cast<std::size_t>(ix)];
	}

	/// @brief 標本 (ix, iz) の世界座標での高さ
	[[nodiscard]] float sampleHeight(std::int64_t ix, std::int64_t iz) const noexcept
	{
		return m_place.originY + static_cast<float>(sample(ix, iz)) * m_scale;
	}

	/// @brief 標本 (ix, iz) の世界座標
	[[nodiscard]] Vec3 vertex(std::int64_t ix, std::int64_t iz) const noexcept
	{
		return {m_place.originX + static_cast<float>(ix) * m_cellX, sampleHeight(ix, iz),
		        m_place.originZ + static_cast<float>(iz) * m_cellZ};
	}

	/// @brief (x, z) の真下にある面の高さ。範囲外では端に寄せた点の高さ
	[[nodiscard]] float heightAt(float x, float z) const noexcept
	{
		const Cell c = cellAt(x, z);
		const float ha = sampleHeight(c.ix, c.iz);
		const float hb = sampleHeight(c.ix + 1, c.iz);
		const float hc = sampleHeight(c.ix, c.iz + 1);
		if (c.u + c.v <= 1.0f) { return ha + (hb - ha) * c.u + (hc - ha) * c.v; }
		const float hd = sampleHeight(c.ix + 1, c.iz + 1);
		return hd + (hc - hd) * (1.0f - c.u) + (hb - hd) * (1.0f - c.v);
	}

	/// @brief (x, z) の真下にある三角形の面法線。上向きで長さは 1。当たり判定の surfaceNormal と同じ向き
	[[nodiscard]] Vec3 normalAt(float x, float z) const noexcept
	{
		const Cell c = cellAt(x, z);
		const float ha = sampleHeight(c.ix, c.iz);
		const float hb = sampleHeight(c.ix + 1, c.iz);
		const float hc = sampleHeight(c.ix, c.iz + 1);
		float dhdx = 0.0f;
		float dhdz = 0.0f;
		if (c.u + c.v <= 1.0f)
		{
			dhdx = (hb - ha) / m_cellX;
			dhdz = (hc - ha) / m_cellZ;
		}
		else
		{
			const float hd = sampleHeight(c.ix + 1, c.iz + 1);
			dhdx = (hd - hc) / m_cellX;
			dhdz = (hd - hb) / m_cellZ;
		}
		return normalized({-dhdx, 1.0f, -dhdz});
	}

	/// @brief 描画と同じ中心差分で求めた滑らかな法線。坂の向きを読むときは normalAt より揺れが少ない
	[[nodiscard]] Vec3 smoothNormalAt(float x, float z) const noexcept
	{
		const float dx = (heightAt(x + m_cellX, z) - heightAt(x - m_cellX, z)) / (2.0f * m_cellX);
		const float dz = (heightAt(x, z + m_cellZ) - heightAt(x, z - m_cellZ)) / (2.0f * m_cellZ);
		return normalized({-dx, 1.0f, -dz});
	}

	/// @brief 標本番号の範囲 [ix0, ix1] × [iz0, iz1] にある高さの最小値と最大値。両端を含む
	[[nodiscard]] std::pair<float, float> heightRange(std::uint32_t ix0, std::uint32_t iz0, std::uint32_t ix1,
	                                                 std::uint32_t iz1) const noexcept
	{
		std::uint16_t lo = 0xFFFF;
		std::uint16_t hi = 0;
		for (std::uint32_t iz = iz0; iz <= iz1 && iz < m_depth; ++iz)
		{
			for (std::uint32_t ix = ix0; ix <= ix1 && ix < m_width; ++ix)
			{
				const std::uint16_t s = m_samples[static_cast<std::size_t>(iz) * m_width + ix];
				lo = std::min(lo, s);
				hi = std::max(hi, s);
			}
		}
		if (lo > hi) { return {m_place.originY, m_place.originY}; }
		return {m_place.originY + lo * m_scale, m_place.originY + hi * m_scale};
	}

private:
	struct Cell
	{
		std::int64_t ix = 0;
		std::int64_t iz = 0;
		float u = 0.0f;
		float v = 0.0f;
	};

	[[nodiscard]] Cell cellAt(float x, float z) const noexcept
	{
		Cell c;
		if (!valid()) { return c; }
		const float fx = std::clamp((x - m_place.originX) / m_cellX, 0.0f, static_cast<float>(m_width - 1));
		const float fz = std::clamp((z - m_place.originZ) / m_cellZ, 0.0f, static_cast<float>(m_depth - 1));
		// 最後の標本は、手前のマスの u = 1 として扱う
		c.ix = std::min<std::int64_t>(static_cast<std::int64_t>(fx), static_cast<std::int64_t>(m_width) - 2);
		c.iz = std::min<std::int64_t>(static_cast<std::int64_t>(fz), static_cast<std::int64_t>(m_depth) - 2);
		c.u = fx - static_cast<float>(c.ix);
		c.v = fz - static_cast<float>(c.iz);
		return c;
	}

	[[nodiscard]] static Vec3 normalized(const Vec3& v) noexcept
	{
		const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		return (l > 0.0f) ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0.0f, 1.0f, 0.0f};
	}

	std::uint32_t              m_width = 0;
	std::uint32_t              m_depth = 0;
	std::vector<std::uint16_t> m_samples;
	HeightfieldPlacement       m_place{};
	float                      m_cellX = 1.0f;
	float                      m_cellZ = 1.0f;
	float                      m_scale = 0.0f;
};

} // namespace mitiru::terrain
