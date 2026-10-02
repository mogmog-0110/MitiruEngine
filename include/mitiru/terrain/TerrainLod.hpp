#pragma once

/// @file TerrainLod.hpp
/// @brief 地形の CDLOD (Strugar 2010)
/// @details 四分木を目からの距離で選び、描く区画の並びを作る。
/// 区画には patchCells × patchCells マスの格子を使い、大きさだけを変える。
/// 段 lod の格子間隔は 2^lod マス。
/// range(lod) の morphStart の位置から外側へ、頂点を 1 段粗い格子に寄せる。
/// 親の段で足りる子は子の大きさで出し、格子を 1 つおきに畳んで親の間隔にする。
/// 隣り合う区画の段の差は 1 までになり、境目の頂点がそろう。
/// 選択結果は描画だけに使う。
/// 当たり判定と heightAt は常に最も細かい格子を使う。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <mitiru/terrain/Heightfield.hpp>

namespace mitiru::terrain
{

struct LodSettings
{
	std::uint32_t patchCells   = 32;    ///< 区画の格子のマスの数 (1 辺)。2 の冪
	std::uint32_t lodCount     = 6;     ///< 四分木の段の数。一番粗い段の区画が地形をタイルする
	float         lod0Distance = 0.0f;  ///< 段 0 の範囲 (m)。0 以下なら段 0 の区画の 1 辺の 1.5 倍。段ごとに倍
	float         morphStart   = 0.65f; ///< 範囲のこの割合から外で 1 段粗い格子へ寄せ始める
};

/// @brief 描く区画 1 つ。GPU へそのまま渡し、VS で格子を配置して目からの距離に応じて頂点を寄せる
struct LodPatch
{
	float         x0 = 0.0f;
	float         z0 = 0.0f;
	float         size = 0.0f;        ///< x 方向の 1 辺 (m)
	float         sizeZ = 0.0f;       ///< z 方向の 1 辺 (m)。マスが正方形なら size と同じ
	float         minY = 0.0f;
	float         maxY = 0.0f;
	float         morphStart = 0.0f;  ///< 目からの距離がこれを超えると寄せ始める
	float         morphEnd = 0.0f;    ///< ここで寄せ切る
	std::uint32_t lod = 0;            ///< 格子の細かさの段 (0 = 高さマップの 1 マス)
	std::uint32_t preSnap = 0;        ///< 1 なら格子を 1 つおきに畳んでから寄せる (子の大きさで親の段を描く区画)
};

/// @brief 区画の選別とカリングに使う軸平行の箱
struct LodBox
{
	float minX, minY, minZ, maxX, maxY, maxZ;
};

class LodQuadtree
{
public:
	LodQuadtree() = default;

	/// @brief 各段の節の高さの最小値と最大値を、読み込み時に 1 度だけ作る
	LodQuadtree(const Heightfield& h, const LodSettings& s) : m_settings(s), m_field(&h)
	{
		m_settings.patchCells = std::clamp<std::uint32_t>(s.patchCells, 2u, 256u);
		m_settings.lodCount = std::clamp<std::uint32_t>(s.lodCount, 1u, 16u);
		if (!h.valid()) { return; }
		m_levels.resize(m_settings.lodCount);
		for (std::uint32_t lod = 0; lod < m_settings.lodCount; ++lod) { buildLevel(lod); }
	}

	[[nodiscard]] const LodSettings& settings() const noexcept { return m_settings; }

	/// @brief 段 lod の節と区画の x 方向の 1 辺 (m)
	[[nodiscard]] float nodeSize(std::uint32_t lod) const noexcept
	{
		return static_cast<float>(cellsOf(lod)) * m_field->cellX();
	}

	/// @brief 段 lod の範囲 (m)
	[[nodiscard]] float range(std::uint32_t lod) const noexcept
	{
		const float base = (m_settings.lod0Distance > 0.0f) ? m_settings.lod0Distance : 1.5f * nodeSize(0);
		return base * static_cast<float>(1u << lod);
	}

	/// @brief eye から見た区画を out へ書く。visible(LodBox) が false の節は描かない
	template <class Visible>
	void select(const Vec3& eye, Visible&& visible, std::vector<LodPatch>& out) const
	{
		out.clear();
		if (m_levels.empty()) { return; }
		const std::uint32_t top = m_settings.lodCount - 1;
		const Level& root = m_levels[top];
		for (std::uint32_t nz = 0; nz < root.countZ; ++nz)
		{
			for (std::uint32_t nx = 0; nx < root.countX; ++nx)
			{
				if (!selectNode(top, nx, nz, eye, visible, out) && visible(box(top, nx, nz)))
				{
					out.push_back(patch(top, nx, nz, top));
				}
			}
		}
	}

	[[nodiscard]] LodBox box(std::uint32_t lod, std::uint32_t nx, std::uint32_t nz) const noexcept
	{
		const auto& lv = m_levels[lod];
		const float s = nodeSize(lod);
		const float sz = static_cast<float>(cellsOf(lod)) * m_field->cellZ();
		const float x0 = m_field->minX() + static_cast<float>(nx) * s;
		const float z0 = m_field->minZ() + static_cast<float>(nz) * sz;
		const std::size_t i = static_cast<std::size_t>(nz) * lv.countX + nx;
		return {x0, lv.minY[i], z0, std::min(x0 + s, m_field->maxX()), lv.maxY[i], std::min(z0 + sz, m_field->maxZ())};
	}

private:
	struct Level
	{
		std::uint32_t      countX = 0;
		std::uint32_t      countZ = 0;
		std::vector<float> minY;
		std::vector<float> maxY;
	};

	[[nodiscard]] std::uint32_t cellsOf(std::uint32_t lod) const noexcept { return m_settings.patchCells << lod; }

	void buildLevel(std::uint32_t lod)
	{
		const std::uint32_t cells = cellsOf(lod);
		Level& lv = m_levels[lod];
		lv.countX = (m_field->width() - 2) / cells + 1;
		lv.countZ = (m_field->depth() - 2) / cells + 1;
		lv.minY.resize(static_cast<std::size_t>(lv.countX) * lv.countZ);
		lv.maxY.resize(lv.minY.size());
		for (std::uint32_t nz = 0; nz < lv.countZ; ++nz)
		{
			for (std::uint32_t nx = 0; nx < lv.countX; ++nx)
			{
				const auto [lo, hi] = m_field->heightRange(nx * cells, nz * cells, (nx + 1) * cells, (nz + 1) * cells);
				lv.minY[static_cast<std::size_t>(nz) * lv.countX + nx] = lo;
				lv.maxY[static_cast<std::size_t>(nz) * lv.countX + nx] = hi;
			}
		}
	}

	[[nodiscard]] static bool boxTouchesSphere(const LodBox& b, const Vec3& c, float r) noexcept
	{
		const float dx = std::max({b.minX - c.x, 0.0f, c.x - b.maxX});
		const float dy = std::max({b.minY - c.y, 0.0f, c.y - b.maxY});
		const float dz = std::max({b.minZ - c.z, 0.0f, c.z - b.maxZ});
		return dx * dx + dy * dy + dz * dz <= r * r;
	}

	/// @brief 節 (lod, nx, nz)の場所を、格子の段 meshLod の区画として出す
	[[nodiscard]] LodPatch patch(std::uint32_t lod, std::uint32_t nx, std::uint32_t nz, std::uint32_t meshLod) const noexcept
	{
		const LodBox b = box(lod, nx, nz);
		LodPatch p;
		p.x0 = b.minX;
		p.z0 = b.minZ;
		p.size = nodeSize(lod);
		p.sizeZ = static_cast<float>(cellsOf(lod)) * m_field->cellZ();
		p.minY = b.minY;
		p.maxY = b.maxY;
		p.lod = meshLod;
		p.morphEnd = range(meshLod);
		p.morphStart = p.morphEnd * m_settings.morphStart;
		return p;
	}

	/// @brief 範囲外では false を返し、親をその段で描く
	template <class Visible>
	bool selectNode(std::uint32_t lod, std::uint32_t nx, std::uint32_t nz, const Vec3& eye, Visible& visible,
	                std::vector<LodPatch>& out) const
	{
		const LodBox b = box(lod, nx, nz);
		if (!boxTouchesSphere(b, eye, range(lod))) { return false; }
		if (!visible(b)) { return true; }
		if (lod == 0 || !boxTouchesSphere(b, eye, range(lod - 1)))
		{
			out.push_back(patch(lod, nx, nz, lod));
			return true;
		}
		const Level& child = m_levels[lod - 1];
		for (std::uint32_t k = 0; k < 4; ++k)
		{
			const std::uint32_t cx = nx * 2 + (k & 1u);
			const std::uint32_t cz = nz * 2 + (k >> 1);
			if (cx >= child.countX || cz >= child.countZ) { continue; }
			if (!selectNode(lod - 1, cx, cz, eye, visible, out) && visible(box(lod - 1, cx, cz)))
			{
				out.push_back(patch(lod - 1, cx, cz, lod));
				out.back().preSnap = 1;
			}
		}
		return true;
	}

	LodSettings        m_settings{};
	const Heightfield* m_field = nullptr;
	std::vector<Level> m_levels;
};

} // namespace mitiru::terrain
