#pragma once

/// @file CollisionLevel.hpp
/// @brief game DLL が自分で持つステージの当たり判定データ (作ったら書き換えない)。
/// @details 中身は 2 種類。動かない三角形をまとめた 1 本の BVH と、姿勢だけを毎フレーム GameMemory から
///          受け取って動かす部品 (エレベーターなど) のローカル座標の BVH。どちらも読み込みとホットリロードで
///          作り直し、遊んでいる間は変えない。動く部品の姿勢と、箱だけで済む動く足場は MovingBody
///          (CollisionWorld.hpp) として GameMemory に置く。
///
///          三角形の id は CollisionLevelBuilder に足した順の通し番号で、BVH の並びに依らず当たりの結果に返る。
///          動かない三角形は全体で 1 つの通し番号、動く部品は部品ごとに 0 から数える。

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <mitiru/action/TriangleBvh.hpp>

namespace mitiru::action
{

/// @brief 箱の 12 枚の三角形の頂点番号。頂点 i は (i&1 ? max.x : min.x, i&2 ? max.y : min.y, i&4 ? max.z : min.z)。
///        外から見て反時計回り (法線が外を向く)
inline constexpr std::uint8_t kBoxTriangleCorners[36] = {
	1, 3, 7, 1, 7, 5, 0, 4, 6, 0, 6, 2, 2, 6, 7, 2, 7, 3,
	0, 1, 5, 0, 5, 4, 4, 5, 7, 4, 7, 6, 0, 2, 3, 0, 3, 1,
};

[[nodiscard]] constexpr Vec3 boxCorner(const Vec3& lo, const Vec3& hi, int i) noexcept
{
	return {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
}

class CollisionLevelBuilder;

class CollisionLevel
{
public:
	CollisionLevel() = default;

	[[nodiscard]] const TriangleBvh& statics() const noexcept { return m_statics; }
	[[nodiscard]] std::size_t movableMeshCount() const noexcept { return m_movable.size(); }
	/// @brief 範囲外の番号は空の BVH を返す (当たらない)
	[[nodiscard]] const TriangleBvh& movableMesh(std::size_t index) const noexcept
	{
		return index < m_movable.size() ? m_movable[index] : m_empty;
	}
	/// @brief 動かない三角形として足した数 (潰れて捨てたものも含む。id の上限)
	[[nodiscard]] std::uint32_t staticTriangleIdCount() const noexcept { return m_staticIdCount; }

private:
	friend class CollisionLevelBuilder;

	TriangleBvh              m_statics;
	std::vector<TriangleBvh> m_movable;
	TriangleBvh              m_empty;
	std::uint32_t            m_staticIdCount = 0;
};

/// @brief 三角形を集めて CollisionLevel を 1 度だけ作る
class CollisionLevelBuilder
{
public:
	/// @brief 添字付きの三角形を動かない地形として足す。範囲外の添字を含む三角形は捨てる (id は消費する)
	/// @param placement 頂点に掛ける姿勢 (ステージ内の配置)
	void addTriangles(std::span<const Vec3> vertices, std::span<const std::uint32_t> indices, std::uint32_t layer,
	                  const Pose& placement = {})
	{
		appendTriangles(m_statics, m_staticIds, vertices, indices, layer, placement);
	}

	/// @brief 軸に平行な箱を動かない地形として足す (12 枚、id を 12 個使う)
	void addBox(const Vec3& lo, const Vec3& hi, std::uint32_t layer)
	{
		for (int i = 0; i < 36; i += 3)
		{
			const BvhTriangle t{boxCorner(lo, hi, kBoxTriangleCorners[i]), boxCorner(lo, hi, kBoxTriangleCorners[i + 1]),
			                    boxCorner(lo, hi, kBoxTriangleCorners[i + 2]), m_staticIds++, layer};
			m_statics.push_back(t);
		}
	}

	/// @brief 動く部品をローカル座標で足し、MovingBody::mesh に入れる番号を返す
	std::uint16_t addMovableMesh(std::span<const Vec3> vertices, std::span<const std::uint32_t> indices,
	                             std::uint32_t layer)
	{
		std::vector<BvhTriangle> tris;
		std::uint32_t            ids = 0;
		appendTriangles(tris, ids, vertices, indices, layer, Pose{});
		m_movable.push_back(std::move(tris));
		return static_cast<std::uint16_t>(m_movable.size() - 1);
	}

	/// @brief BVH を作って渡す。builder は空に戻る
	[[nodiscard]] CollisionLevel build()
	{
		CollisionLevel level;
		level.m_statics       = TriangleBvh(std::move(m_statics));
		level.m_staticIdCount = m_staticIds;
		level.m_movable.reserve(m_movable.size());
		for (auto& tris : m_movable) { level.m_movable.emplace_back(std::move(tris)); }
		m_statics.clear();
		m_movable.clear();
		m_staticIds = 0;
		return level;
	}

private:
	static void appendTriangles(std::vector<BvhTriangle>& out, std::uint32_t& nextId, std::span<const Vec3> vertices,
	                            std::span<const std::uint32_t> indices, std::uint32_t layer, const Pose& placement)
	{
		const std::size_t n = indices.size() / 3;
		out.reserve(out.size() + n);
		for (std::size_t i = 0; i < n; ++i)
		{
			const std::uint32_t id = nextId++;
			const std::uint32_t i0 = indices[i * 3], i1 = indices[i * 3 + 1], i2 = indices[i * 3 + 2];
			if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) { continue; }
			out.push_back({placement.apply(vertices[i0]), placement.apply(vertices[i1]), placement.apply(vertices[i2]),
			               id, layer});
		}
	}

	std::vector<BvhTriangle>              m_statics;
	std::uint32_t                         m_staticIds = 0;
	std::vector<std::vector<BvhTriangle>> m_movable;
};

} // namespace mitiru::action
