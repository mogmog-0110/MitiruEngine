#pragma once

/// @file SpatialPartition.hpp
/// @brief 空間分割データ構造
/// @details 3D用Octreeと2D用固定グリッドを提供する。
///          ブロードフェーズ衝突検出やカリングの高速化に利用する。

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <mitiru/render/FrustumCulling.hpp>

namespace mitiru::render
{

// ────────────────────────────────────────────
// OctreeNode — 3D空間分割
// ────────────────────────────────────────────

/// @brief Octreeに格納するエントリ
/// @tparam T オブジェクト識別子の型
template <typename T>
struct OctreeEntry
{
	T object{};
	AABB bounds{};
};

/// @brief Octreeノード
/// @tparam T オブジェクト識別子の型
/// @details 再帰的に8分割し、AABBの空間問い合わせを高速化する。
///
/// @code
/// mitiru::render::OctreeNode<int> tree({-100,-100,-100, 100,100,100});
/// tree.insert(42, {10,10,10, 20,20,20});
/// auto results = tree.query({0,0,0, 50,50,50});
/// @endcode
template <typename T>
class OctreeNode
{
public:
	/// @brief コンストラクタ
	/// @param bounds このノードが管轄する空間範囲
	/// @param maxDepth 最大分割深度
	/// @param maxObjects 分割前の最大オブジェクト数
	explicit OctreeNode(const AABB& bounds,
	                    int maxDepth = 6,
	                    int maxObjects = 8) noexcept
		: m_bounds(bounds)
		, m_maxDepth(maxDepth)
		, m_maxObjects(maxObjects)
	{
	}

	/// @brief オブジェクトを挿入する
	/// @param object オブジェクト識別子
	/// @param objBounds オブジェクトのAABB
	void insert(const T& object, const AABB& objBounds)
	{
		insertImpl(object, objBounds, 0);
	}

	/// @brief 指定領域と交差するオブジェクトを問い合わせる
	/// @param region 問い合わせ領域
	/// @return 交差するオブジェクトのリスト
	[[nodiscard]] std::vector<T> query(const AABB& region) const
	{
		std::vector<T> results;
		queryImpl(region, results);
		return results;
	}

	/// @brief 全オブジェクトを消去する
	void clear() noexcept
	{
		m_entries.clear();
		for (auto& child : m_children)
		{
			child.reset();
		}
	}

	/// @brief 格納されているオブジェクト数（このノードのみ）
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_entries.size();
	}

private:
	AABB m_bounds;
	int m_maxDepth;
	int m_maxObjects;
	std::vector<OctreeEntry<T>> m_entries;
	std::array<std::unique_ptr<OctreeNode<T>>, 8> m_children{};

	[[nodiscard]] static bool aabbOverlap(const AABB& a, const AABB& b) noexcept
	{
		return a.minX <= b.maxX && a.maxX >= b.minX &&
		       a.minY <= b.maxY && a.maxY >= b.minY &&
		       a.minZ <= b.maxZ && a.maxZ >= b.minZ;
	}

	[[nodiscard]] static bool aabbContains(const AABB& outer, const AABB& inner) noexcept
	{
		return inner.minX >= outer.minX && inner.maxX <= outer.maxX &&
		       inner.minY >= outer.minY && inner.maxY <= outer.maxY &&
		       inner.minZ >= outer.minZ && inner.maxZ <= outer.maxZ;
	}

	void subdivide()
	{
		const float mx = (m_bounds.minX + m_bounds.maxX) * 0.5f;
		const float my = (m_bounds.minY + m_bounds.maxY) * 0.5f;
		const float mz = (m_bounds.minZ + m_bounds.maxZ) * 0.5f;

		const AABB childBounds[8] = {
			{m_bounds.minX, m_bounds.minY, m_bounds.minZ, mx, my, mz},
			{mx, m_bounds.minY, m_bounds.minZ, m_bounds.maxX, my, mz},
			{m_bounds.minX, my, m_bounds.minZ, mx, m_bounds.maxY, mz},
			{mx, my, m_bounds.minZ, m_bounds.maxX, m_bounds.maxY, mz},
			{m_bounds.minX, m_bounds.minY, mz, mx, my, m_bounds.maxZ},
			{mx, m_bounds.minY, mz, m_bounds.maxX, my, m_bounds.maxZ},
			{m_bounds.minX, my, mz, mx, m_bounds.maxY, m_bounds.maxZ},
			{mx, my, mz, m_bounds.maxX, m_bounds.maxY, m_bounds.maxZ},
		};

		for (int i = 0; i < 8; ++i)
		{
			m_children[i] = std::make_unique<OctreeNode<T>>(
				childBounds[i], m_maxDepth, m_maxObjects);
		}
	}

	void insertImpl(const T& object, const AABB& objBounds, int depth)
	{
		if (!aabbOverlap(m_bounds, objBounds)) return;

		// 葉ノードで容量未満、または最大深度到達
		if (!m_children[0] &&
		    (static_cast<int>(m_entries.size()) < m_maxObjects || depth >= m_maxDepth))
		{
			m_entries.push_back({object, objBounds});
			return;
		}

		// 分割が必要
		if (!m_children[0])
		{
			subdivide();

			// 既存エントリを子ノードに再配分
			auto oldEntries = std::move(m_entries);
			m_entries.clear();
			for (const auto& entry : oldEntries)
			{
				bool placed = false;
				for (auto& child : m_children)
				{
					if (aabbContains(child->m_bounds, entry.bounds))
					{
						child->insertImpl(entry.object, entry.bounds, depth + 1);
						placed = true;
						break;
					}
				}
				if (!placed)
				{
					m_entries.push_back(entry); // 複数子にまたがる場合は親に保持
				}
			}
		}

		// 新規オブジェクトを適切な子に挿入
		for (auto& child : m_children)
		{
			if (aabbContains(child->m_bounds, objBounds))
			{
				child->insertImpl(object, objBounds, depth + 1);
				return;
			}
		}
		m_entries.push_back({object, objBounds}); // またがる場合
	}

	void queryImpl(const AABB& region, std::vector<T>& results) const
	{
		if (!aabbOverlap(m_bounds, region)) return;

		for (const auto& entry : m_entries)
		{
			if (aabbOverlap(entry.bounds, region))
			{
				results.push_back(entry.object);
			}
		}

		if (m_children[0])
		{
			for (const auto& child : m_children)
			{
				child->queryImpl(region, results);
			}
		}
	}
};

// ────────────────────────────────────────────
// GridPartition2D — 2D固定グリッド空間分割
// ────────────────────────────────────────────

/// @brief 2D固定グリッドによる空間分割
/// @tparam T オブジェクト識別子の型
/// @details 固定サイズのセルに分割し、挿入・範囲問い合わせを O(1) に近い速度で行う。
///
/// @code
/// mitiru::render::GridPartition2D<int> grid(0, 0, 1000, 1000, 50.0f);
/// grid.insert(42, 100.0f, 200.0f);
/// auto nearby = grid.queryRadius(100.0f, 200.0f, 60.0f);
/// @endcode
template <typename T>
class GridPartition2D
{
public:
	/// @brief コンストラクタ
	/// @param originX グリッド原点X
	/// @param originY グリッド原点Y
	/// @param width グリッド全体の幅
	/// @param height グリッド全体の高さ
	/// @param cellSize セルの一辺の長さ
	GridPartition2D(float originX, float originY,
	                float width, float height,
	                float cellSize)
		: m_originX(originX)
		, m_originY(originY)
		, m_cellSize(cellSize)
		, m_cols(std::max(1, static_cast<int>(std::ceil(width / cellSize))))
		, m_rows(std::max(1, static_cast<int>(std::ceil(height / cellSize))))
	{
		m_cells.resize(static_cast<std::size_t>(m_cols) * m_rows);
	}

	/// @brief オブジェクトを座標に基づいて挿入する
	/// @param object オブジェクト識別子
	/// @param x X座標
	/// @param y Y座標
	void insert(const T& object, float x, float y)
	{
		const int idx = cellIndex(x, y);
		if (idx >= 0 && idx < static_cast<int>(m_cells.size()))
		{
			m_cells[idx].push_back({object, x, y});
		}
	}

	/// @brief 指定座標から半径内のオブジェクトを問い合わせる
	/// @param cx 中心X
	/// @param cy 中心Y
	/// @param radius 検索半径
	/// @return 半径内のオブジェクトのリスト
	[[nodiscard]] std::vector<T> queryRadius(float cx, float cy, float radius) const
	{
		std::vector<T> results;
		const float r2 = radius * radius;

		const int minCol = std::max(0, toCol(cx - radius));
		const int maxCol = std::min(m_cols - 1, toCol(cx + radius));
		const int minRow = std::max(0, toRow(cy - radius));
		const int maxRow = std::min(m_rows - 1, toRow(cy + radius));

		for (int row = minRow; row <= maxRow; ++row)
		{
			for (int col = minCol; col <= maxCol; ++col)
			{
				const auto& cell = m_cells[static_cast<std::size_t>(row) * m_cols + col];
				for (const auto& entry : cell)
				{
					const float dx = entry.x - cx;
					const float dy = entry.y - cy;
					if (dx * dx + dy * dy <= r2)
					{
						results.push_back(entry.object);
					}
				}
			}
		}
		return results;
	}

	/// @brief 全オブジェクトを消去する
	void clear()
	{
		for (auto& cell : m_cells)
		{
			cell.clear();
		}
	}

	/// @brief グリッドの列数
	[[nodiscard]] int cols() const noexcept { return m_cols; }

	/// @brief グリッドの行数
	[[nodiscard]] int rows() const noexcept { return m_rows; }

private:
	struct Entry
	{
		T object{};
		float x = 0.0f;
		float y = 0.0f;
	};

	float m_originX;
	float m_originY;
	float m_cellSize;
	int m_cols;
	int m_rows;
	std::vector<std::vector<Entry>> m_cells;

	[[nodiscard]] int toCol(float x) const noexcept
	{
		return static_cast<int>((x - m_originX) / m_cellSize);
	}

	[[nodiscard]] int toRow(float y) const noexcept
	{
		return static_cast<int>((y - m_originY) / m_cellSize);
	}

	[[nodiscard]] int cellIndex(float x, float y) const noexcept
	{
		const int col = toCol(x);
		const int row = toRow(y);
		if (col < 0 || col >= m_cols || row < 0 || row >= m_rows) return -1;
		return row * m_cols + col;
	}
};

} // namespace mitiru::render
