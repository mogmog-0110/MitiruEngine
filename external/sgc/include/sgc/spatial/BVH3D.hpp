#pragma once

/// @file BVH3D.hpp
/// @brief 3D Bounding Volume Hierarchy（バウンディングボリューム階層）
///
/// AABBベースのBVHによる3D空間クエリを提供する。
/// BVH2Dの3D拡張版。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "sgc/math/Geometry.hpp"

namespace sgc
{

/// @brief 3D Bounding Volume Hierarchy
/// @tparam T 浮動小数点型
///
/// AABB3の階層構造で効率的な3D空間クエリ（範囲・点・レイキャスト・全ペア交差）を提供する。
///
/// @code
/// sgc::BVH3Df bvh;
/// auto h0 = bvh.insert(sgc::AABB3f{{0,0,0}, {10,10,10}});
/// auto h1 = bvh.insert(sgc::AABB3f{{5,5,5}, {15,15,15}});
/// auto hits = bvh.queryRange(sgc::AABB3f{{0,0,0}, {20,20,20}});
/// @endcode
template <FloatingPoint T>
class BVH3D
{
public:
	/// @brief 要素ハンドル型
	using Handle = std::size_t;

	/// @brief レイキャストヒット情報
	struct RayHit
	{
		Handle handle;  ///< ヒットした要素のハンドル
		T distance;     ///< レイの始点からの距離
	};

	/// @brief AABB3の配列からBVHをトップダウンで構築する
	/// @param aabbs 入力AABBの配列
	void build(std::span<const AABB3<T>> aabbs)
	{
		clear();

		if (aabbs.empty())
		{
			return;
		}

		m_elements.reserve(aabbs.size());
		std::vector<std::size_t> indices;
		indices.reserve(aabbs.size());
		for (std::size_t i = 0; i < aabbs.size(); ++i)
		{
			m_elements.push_back({aabbs[i], true});
			indices.push_back(i);
		}

		m_nodes.reserve(aabbs.size() * 2);
		m_rootIndex = buildRecursive(indices, 0, indices.size());
	}

	/// @brief 単一AABBを挿入する
	/// @param bounds 挿入するバウンディングボックス
	/// @return 要素のハンドル
	Handle insert(const AABB3<T>& bounds)
	{
		const Handle handle = m_elements.size();
		m_elements.push_back({bounds, true});

		const int leafIdx = allocateNode();
		m_nodes[leafIdx].bounds = bounds;
		m_nodes[leafIdx].isLeaf = true;
		m_nodes[leafIdx].leafHandle = handle;

		if (m_rootIndex == -1)
		{
			m_rootIndex = leafIdx;
			return handle;
		}

		insertLeaf(leafIdx);
		return handle;
	}

	/// @brief ハンドルを削除する
	/// @param handle 削除する要素のハンドル
	void remove(Handle handle)
	{
		if (handle >= m_elements.size() || !m_elements[handle].active)
		{
			return;
		}

		m_elements[handle].active = false;

		const int leafIdx = findLeaf(handle);
		if (leafIdx == -1)
		{
			return;
		}

		removeLeaf(leafIdx);
	}

	/// @brief ハンドルのAABBを更新する
	/// @param handle 更新する要素のハンドル
	/// @param newBounds 新しいバウンディングボックス
	void update(Handle handle, const AABB3<T>& newBounds)
	{
		if (handle >= m_elements.size() || !m_elements[handle].active)
		{
			return;
		}

		const int leafIdx = findLeaf(handle);
		if (leafIdx == -1)
		{
			return;
		}

		removeLeaf(leafIdx);

		m_elements[handle].bounds = newBounds;

		// 新しいリーフノードを割り当てる（removeLeafで旧ノードは解放済み）
		const int newLeafIdx = allocateNode();
		m_nodes[newLeafIdx].bounds = newBounds;
		m_nodes[newLeafIdx].isLeaf = true;
		m_nodes[newLeafIdx].leafHandle = handle;

		if (m_rootIndex == -1)
		{
			m_rootIndex = newLeafIdx;
		}
		else
		{
			insertLeaf(newLeafIdx);
		}
	}

	/// @brief 範囲クエリ（AABB3と交差するハンドルを返す）
	/// @param range 検索範囲
	/// @return 範囲内の要素ハンドルリスト
	[[nodiscard]] std::vector<Handle> queryRange(const AABB3<T>& range) const
	{
		std::vector<Handle> results;
		if (m_rootIndex != -1)
		{
			queryRangeImpl(m_rootIndex, range, results);
		}
		return results;
	}

	/// @brief 点クエリ
	/// @param point 検索点
	/// @return 点を含む要素ハンドルリスト
	[[nodiscard]] std::vector<Handle> queryPoint(const Vec3<T>& point) const
	{
		std::vector<Handle> results;
		if (m_rootIndex != -1)
		{
			queryPointImpl(m_rootIndex, point, results);
		}
		return results;
	}

	/// @brief レイキャスト（最近傍交差を返す）
	/// @param origin レイの始点
	/// @param direction レイの方向（正規化推奨）
	/// @param maxDist 最大距離
	/// @return 最近傍のヒット情報（交差がない場合はnullopt）
	[[nodiscard]] std::optional<RayHit> raycast(
		const Vec3<T>& origin,
		const Vec3<T>& direction,
		T maxDist) const
	{
		if (m_rootIndex == -1)
		{
			return std::nullopt;
		}

		RayHit bestHit{0, maxDist};
		bool found = false;
		raycastImpl(m_rootIndex, origin, direction, bestHit, found);

		if (found)
		{
			return bestHit;
		}
		return std::nullopt;
	}

	/// @brief 全ペアの交差判定（ブロードフェーズ）
	/// @return 交差しているハンドルのペアリスト
	[[nodiscard]] std::vector<std::pair<Handle, Handle>> queryAllPairs() const
	{
		std::vector<std::pair<Handle, Handle>> pairs;

		std::vector<std::size_t> activeHandles;
		for (std::size_t i = 0; i < m_elements.size(); ++i)
		{
			if (m_elements[i].active)
			{
				activeHandles.push_back(i);
			}
		}

		for (std::size_t i = 0; i < activeHandles.size(); ++i)
		{
			for (std::size_t j = i + 1; j < activeHandles.size(); ++j)
			{
				const auto hi = activeHandles[i];
				const auto hj = activeHandles[j];
				if (m_elements[hi].bounds.intersects(m_elements[hj].bounds))
				{
					pairs.emplace_back(hi, hj);
				}
			}
		}

		return pairs;
	}

	/// @brief 要素数を返す
	/// @return アクティブな要素の数
	[[nodiscard]] std::size_t size() const noexcept
	{
		std::size_t count = 0;
		for (const auto& e : m_elements)
		{
			if (e.active) ++count;
		}
		return count;
	}

	/// @brief 全クリア
	void clear()
	{
		m_nodes.clear();
		m_elements.clear();
		m_freeNodes.clear();
		m_rootIndex = -1;
	}

private:
	/// @brief BVHノード
	struct BVHNode
	{
		AABB3<T> bounds{};        ///< バウンディングボックス
		int left = -1;            ///< 左子ノードインデックス
		int right = -1;           ///< 右子ノードインデックス
		int parent = -1;          ///< 親ノードインデックス
		Handle leafHandle = 0;    ///< リーフの場合の要素ハンドル
		bool isLeaf = false;      ///< リーフノードかどうか
	};

	/// @brief 要素データ
	struct Element
	{
		AABB3<T> bounds;  ///< バウンディングボックス
		bool active;      ///< アクティブかどうか
	};

	std::vector<BVHNode> m_nodes;
	std::vector<Element> m_elements;
	std::vector<int> m_freeNodes;
	int m_rootIndex = -1;

	/// @brief ノードを割り当てる
	int allocateNode()
	{
		if (!m_freeNodes.empty())
		{
			const int idx = m_freeNodes.back();
			m_freeNodes.pop_back();
			m_nodes[idx] = BVHNode{};
			return idx;
		}
		const int idx = static_cast<int>(m_nodes.size());
		m_nodes.push_back(BVHNode{});
		return idx;
	}

	/// @brief ノードを解放する
	void freeNode(int idx)
	{
		m_nodes[idx] = BVHNode{};
		m_freeNodes.push_back(idx);
	}

	/// @brief 2つのAABB3を合成する
	[[nodiscard]] static constexpr AABB3<T> mergeAABB(
		const AABB3<T>& a,
		const AABB3<T>& b) noexcept
	{
		return {
			{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)},
			{std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)}
		};
	}

	/// @brief AABB3の表面積を返す
	[[nodiscard]] static constexpr T surfaceArea(const AABB3<T>& aabb) noexcept
	{
		const T dx = aabb.max.x - aabb.min.x;
		const T dy = aabb.max.y - aabb.min.y;
		const T dz = aabb.max.z - aabb.min.z;
		return T{2} * (dx * dy + dy * dz + dz * dx);
	}

	/// @brief トップダウン再帰構築
	int buildRecursive(std::vector<std::size_t>& indices, std::size_t begin, std::size_t end)
	{
		if (begin + 1 == end)
		{
			const int nodeIdx = allocateNode();
			m_nodes[nodeIdx].bounds = m_elements[indices[begin]].bounds;
			m_nodes[nodeIdx].isLeaf = true;
			m_nodes[nodeIdx].leafHandle = indices[begin];
			return nodeIdx;
		}

		// 全要素の包含AABBを計算
		AABB3<T> totalBounds = m_elements[indices[begin]].bounds;
		for (std::size_t i = begin + 1; i < end; ++i)
		{
			totalBounds = mergeAABB(totalBounds, m_elements[indices[i]].bounds);
		}

		// 最長軸を選択
		const T extentX = totalBounds.max.x - totalBounds.min.x;
		const T extentY = totalBounds.max.y - totalBounds.min.y;
		const T extentZ = totalBounds.max.z - totalBounds.min.z;

		int splitAxis = 0;  // 0=X, 1=Y, 2=Z
		if (extentY > extentX && extentY > extentZ)
		{
			splitAxis = 1;
		}
		else if (extentZ > extentX && extentZ > extentY)
		{
			splitAxis = 2;
		}

		const std::size_t mid = begin + (end - begin) / 2;
		std::nth_element(
			indices.begin() + static_cast<std::ptrdiff_t>(begin),
			indices.begin() + static_cast<std::ptrdiff_t>(mid),
			indices.begin() + static_cast<std::ptrdiff_t>(end),
			[&](std::size_t a, std::size_t b)
			{
				const auto ca = m_elements[a].bounds.center();
				const auto cb = m_elements[b].bounds.center();
				if (splitAxis == 0) return ca.x < cb.x;
				if (splitAxis == 1) return ca.y < cb.y;
				return ca.z < cb.z;
			});

		const int nodeIdx = allocateNode();
		const int leftChild = buildRecursive(indices, begin, mid);
		const int rightChild = buildRecursive(indices, mid, end);

		m_nodes[nodeIdx].left = leftChild;
		m_nodes[nodeIdx].right = rightChild;
		m_nodes[nodeIdx].bounds = mergeAABB(
			m_nodes[leftChild].bounds, m_nodes[rightChild].bounds);
		m_nodes[nodeIdx].isLeaf = false;

		m_nodes[leftChild].parent = nodeIdx;
		m_nodes[rightChild].parent = nodeIdx;

		return nodeIdx;
	}

	/// @brief リーフノードをツリーに挿入する
	void insertLeaf(int leafIdx)
	{
		int bestSibling = m_rootIndex;
		T bestCost = surfaceArea(mergeAABB(m_nodes[m_rootIndex].bounds, m_nodes[leafIdx].bounds));

		struct StackEntry
		{
			int nodeIdx;
			T inheritedCost;
		};
		std::vector<StackEntry> stack;
		stack.push_back({m_rootIndex, T{0}});

		while (!stack.empty())
		{
			const auto [current, inherited] = stack.back();
			stack.pop_back();

			const AABB3<T> merged = mergeAABB(m_nodes[current].bounds, m_nodes[leafIdx].bounds);
			const T directCost = surfaceArea(merged);
			const T totalCost = directCost + inherited;

			if (totalCost < bestCost)
			{
				bestCost = totalCost;
				bestSibling = current;
			}

			const T inheritedForChild = inherited + directCost - surfaceArea(m_nodes[current].bounds);
			const T lowerBound = surfaceArea(m_nodes[leafIdx].bounds) + inheritedForChild;

			if (lowerBound < bestCost && !m_nodes[current].isLeaf)
			{
				stack.push_back({m_nodes[current].left, inheritedForChild});
				stack.push_back({m_nodes[current].right, inheritedForChild});
			}
		}

		const int oldParent = m_nodes[bestSibling].parent;
		const int newParent = allocateNode();
		m_nodes[newParent].parent = oldParent;
		m_nodes[newParent].bounds = mergeAABB(
			m_nodes[bestSibling].bounds, m_nodes[leafIdx].bounds);
		m_nodes[newParent].isLeaf = false;

		if (oldParent != -1)
		{
			if (m_nodes[oldParent].left == bestSibling)
			{
				m_nodes[oldParent].left = newParent;
			}
			else
			{
				m_nodes[oldParent].right = newParent;
			}
			m_nodes[newParent].left = bestSibling;
			m_nodes[newParent].right = leafIdx;
			m_nodes[bestSibling].parent = newParent;
			m_nodes[leafIdx].parent = newParent;
		}
		else
		{
			m_nodes[newParent].left = bestSibling;
			m_nodes[newParent].right = leafIdx;
			m_nodes[bestSibling].parent = newParent;
			m_nodes[leafIdx].parent = newParent;
			m_rootIndex = newParent;
		}

		refitAncestors(m_nodes[leafIdx].parent);
	}

	/// @brief リーフノードをツリーから削除する
	void removeLeaf(int leafIdx)
	{
		if (leafIdx == m_rootIndex)
		{
			m_rootIndex = -1;
			freeNode(leafIdx);
			return;
		}

		const int parentIdx = m_nodes[leafIdx].parent;
		const int grandParent = m_nodes[parentIdx].parent;
		const int sibling = (m_nodes[parentIdx].left == leafIdx)
			? m_nodes[parentIdx].right
			: m_nodes[parentIdx].left;

		if (grandParent != -1)
		{
			if (m_nodes[grandParent].left == parentIdx)
			{
				m_nodes[grandParent].left = sibling;
			}
			else
			{
				m_nodes[grandParent].right = sibling;
			}
			m_nodes[sibling].parent = grandParent;

			freeNode(parentIdx);
			freeNode(leafIdx);

			refitAncestors(grandParent);
		}
		else
		{
			m_rootIndex = sibling;
			m_nodes[sibling].parent = -1;

			freeNode(parentIdx);
			freeNode(leafIdx);
		}
	}

	/// @brief ハンドルに対応するリーフノードを検索する
	[[nodiscard]] int findLeaf(Handle handle) const
	{
		for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
		{
			if (m_nodes[i].isLeaf && m_nodes[i].leafHandle == handle)
			{
				return i;
			}
		}
		return -1;
	}

	/// @brief 祖先ノードのAABBを再計算する
	void refitAncestors(int nodeIdx)
	{
		int current = nodeIdx;
		while (current != -1)
		{
			const int left = m_nodes[current].left;
			const int right = m_nodes[current].right;
			if (left != -1 && right != -1)
			{
				m_nodes[current].bounds = mergeAABB(
					m_nodes[left].bounds, m_nodes[right].bounds);
			}
			current = m_nodes[current].parent;
		}
	}

	/// @brief 範囲クエリの再帰実装
	void queryRangeImpl(int nodeIdx, const AABB3<T>& range, std::vector<Handle>& results) const
	{
		if (nodeIdx == -1)
		{
			return;
		}

		if (!m_nodes[nodeIdx].bounds.intersects(range))
		{
			return;
		}

		if (m_nodes[nodeIdx].isLeaf)
		{
			const Handle h = m_nodes[nodeIdx].leafHandle;
			if (m_elements[h].active && m_elements[h].bounds.intersects(range))
			{
				results.push_back(h);
			}
			return;
		}

		queryRangeImpl(m_nodes[nodeIdx].left, range, results);
		queryRangeImpl(m_nodes[nodeIdx].right, range, results);
	}

	/// @brief 点クエリの再帰実装
	void queryPointImpl(int nodeIdx, const Vec3<T>& point, std::vector<Handle>& results) const
	{
		if (nodeIdx == -1)
		{
			return;
		}

		if (!m_nodes[nodeIdx].bounds.contains(point))
		{
			return;
		}

		if (m_nodes[nodeIdx].isLeaf)
		{
			const Handle h = m_nodes[nodeIdx].leafHandle;
			if (m_elements[h].active && m_elements[h].bounds.contains(point))
			{
				results.push_back(h);
			}
			return;
		}

		queryPointImpl(m_nodes[nodeIdx].left, point, results);
		queryPointImpl(m_nodes[nodeIdx].right, point, results);
	}

	/// @brief レイとAABB3の交差判定（スラブ法）
	[[nodiscard]] static bool rayIntersectsAABB(
		const Vec3<T>& origin,
		const Vec3<T>& direction,
		const AABB3<T>& aabb,
		T& tMin)
	{
		T tNear = -std::numeric_limits<T>::infinity();
		T tFar = std::numeric_limits<T>::infinity();

		auto checkAxis = [&](T orig, T dir, T minVal, T maxVal) -> bool
		{
			if (std::abs(dir) < std::numeric_limits<T>::epsilon())
			{
				return orig >= minVal && orig <= maxVal;
			}
			T t1 = (minVal - orig) / dir;
			T t2 = (maxVal - orig) / dir;
			if (t1 > t2) std::swap(t1, t2);
			tNear = std::max(tNear, t1);
			tFar = std::min(tFar, t2);
			return tNear <= tFar && tFar >= T{0};
		};

		if (!checkAxis(origin.x, direction.x, aabb.min.x, aabb.max.x)) return false;
		if (!checkAxis(origin.y, direction.y, aabb.min.y, aabb.max.y)) return false;
		if (!checkAxis(origin.z, direction.z, aabb.min.z, aabb.max.z)) return false;

		tMin = std::max(tNear, T{0});
		return true;
	}

	/// @brief レイキャストの再帰実装
	void raycastImpl(
		int nodeIdx,
		const Vec3<T>& origin,
		const Vec3<T>& direction,
		RayHit& bestHit,
		bool& found) const
	{
		if (nodeIdx == -1)
		{
			return;
		}

		T tNode{};
		if (!rayIntersectsAABB(origin, direction, m_nodes[nodeIdx].bounds, tNode))
		{
			return;
		}

		if (tNode > bestHit.distance)
		{
			return;
		}

		if (m_nodes[nodeIdx].isLeaf)
		{
			const Handle h = m_nodes[nodeIdx].leafHandle;
			if (m_elements[h].active)
			{
				T t{};
				if (rayIntersectsAABB(origin, direction, m_elements[h].bounds, t))
				{
					if (t < bestHit.distance)
					{
						bestHit.handle = h;
						bestHit.distance = t;
						found = true;
					}
				}
			}
			return;
		}

		const int left = m_nodes[nodeIdx].left;
		const int right = m_nodes[nodeIdx].right;
		T tLeft = std::numeric_limits<T>::max();
		T tRight = std::numeric_limits<T>::max();

		if (left != -1)
		{
			(void)rayIntersectsAABB(origin, direction, m_nodes[left].bounds, tLeft);
		}
		if (right != -1)
		{
			(void)rayIntersectsAABB(origin, direction, m_nodes[right].bounds, tRight);
		}

		if (tLeft < tRight)
		{
			raycastImpl(left, origin, direction, bestHit, found);
			raycastImpl(right, origin, direction, bestHit, found);
		}
		else
		{
			raycastImpl(right, origin, direction, bestHit, found);
			raycastImpl(left, origin, direction, bestHit, found);
		}
	}
};

// ── エイリアス ──────────────────────────────────────────────────

using BVH3Df = BVH3D<float>;    ///< float版 BVH3D
using BVH3Dd = BVH3D<double>;   ///< double版 BVH3D

} // namespace sgc
