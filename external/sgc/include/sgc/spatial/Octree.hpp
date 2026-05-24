#pragma once

/// @file Octree.hpp
/// @brief 八分木（Octree）による3D空間分割
///
/// 3Dゲームのブロードフェーズ衝突検出や近傍探索に使用する。
/// Quadtreeの3D拡張版。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "sgc/math/Geometry.hpp"
#include "sgc/math/Frustum.hpp"
#include "sgc/math/Ray3.hpp"

namespace sgc
{

/// @brief 八分木
/// @tparam T 浮動小数点型
/// @tparam MaxDepth 最大深度
/// @tparam MaxPerNode ノードあたりの最大要素数
///
/// @code
/// sgc::Octreef ot(sgc::AABB3f{{-100,-100,-100}, {100,100,100}});
/// auto h = ot.insert(sgc::AABB3f{{10,10,10}, {20,20,20}});
/// auto results = ot.queryRange(sgc::AABB3f{{0,0,0}, {50,50,50}});
/// @endcode
template <FloatingPoint T, int MaxDepth = 8, int MaxPerNode = 8>
class Octree
{
public:
	/// @brief 要素ハンドル型
	using Handle = std::size_t;

	/// @brief 全体の境界を指定して構築する
	explicit Octree(const AABB3<T>& bounds)
	{
		m_nodes.push_back(Node{bounds});
	}

	/// @brief 要素を挿入する
	Handle insert(const AABB3<T>& bounds)
	{
		const Handle handle = m_elements.size();
		m_elements.push_back({bounds, true});
		insertIntoNode(0, handle);
		return handle;
	}

	/// @brief 要素を削除する
	void remove(Handle handle)
	{
		if (handle < m_elements.size())
		{
			m_elements[handle].active = false;
		}
	}

	/// @brief 要素の位置を更新する
	void update(Handle handle, const AABB3<T>& newBounds)
	{
		if (handle < m_elements.size())
		{
			m_elements[handle].bounds = newBounds;
			m_elements[handle].active = true;
		}
	}

	/// @brief 範囲内の要素を検索する
	[[nodiscard]] std::vector<Handle> queryRange(const AABB3<T>& range) const
	{
		std::vector<Handle> results;
		queryRangeImpl(0, range, results);
		return results;
	}

	/// @brief 点を含む要素を検索する
	[[nodiscard]] std::vector<Handle> queryPoint(const Vec3<T>& point) const
	{
		std::vector<Handle> results;
		const AABB3<T> pointBox{point, point};
		queryRangeImpl(0, pointBox, results);
		return results;
	}

	/// @brief 球内の要素を検索する
	[[nodiscard]] std::vector<Handle> querySphere(const Vec3<T>& center, T radius) const
	{
		const AABB3<T> range{
			{center.x - radius, center.y - radius, center.z - radius},
			{center.x + radius, center.y + radius, center.z + radius}
		};
		auto candidates = queryRange(range);

		std::vector<Handle> results;
		const T r2 = radius * radius;
		for (const auto h : candidates)
		{
			const auto& bounds = m_elements[h].bounds;
			auto clampAxis = [](T val, T minV, T maxV) -> T
			{
				if (val < minV) return minV;
				if (val > maxV) return maxV;
				return val;
			};
			const Vec3<T> closest{
				clampAxis(center.x, bounds.min.x, bounds.max.x),
				clampAxis(center.y, bounds.min.y, bounds.max.y),
				clampAxis(center.z, bounds.min.z, bounds.max.z)
			};
			if ((center - closest).lengthSquared() <= r2)
			{
				results.push_back(h);
			}
		}
		return results;
	}

	/// @brief フラスタム内の要素を検索する
	[[nodiscard]] std::vector<Handle> queryFrustum(const Frustum<T>& frustum) const
	{
		std::vector<Handle> results;
		queryFrustumImpl(0, frustum, results);
		return results;
	}

	/// @brief レイキャストヒット情報
	struct RaycastHit
	{
		Handle handle;   ///< ヒットした要素のハンドル
		T distance;      ///< レイの始点からの距離
	};

	/// @brief レイキャストを実行する
	///
	/// レイと交差する全要素を距離順（近い順）で返す。
	///
	/// @param ray レイ（始点と方向）
	/// @return ヒットリスト（距離ソート済み）
	[[nodiscard]] std::vector<RaycastHit> raycast(const Ray3<T>& ray) const
	{
		std::vector<RaycastHit> results;
		raycastImpl(0, ray, results);
		std::sort(results.begin(), results.end(),
			[](const RaycastHit& a, const RaycastHit& b) { return a.distance < b.distance; });
		return results;
	}

	/// @brief k近傍探索を実行する
	///
	/// 指定点に最も近いk個の要素を距離順で返す。
	///
	/// @param point 検索点
	/// @param k 取得する要素数
	/// @return ハンドルリスト（距離ソート済み）
	[[nodiscard]] std::vector<Handle> findKNearest(const Vec3<T>& point, std::size_t k) const
	{
		struct DistHandle
		{
			T distance;
			Handle handle;
		};

		std::vector<DistHandle> all;
		all.reserve(m_elements.size());
		for (std::size_t i = 0; i < m_elements.size(); ++i)
		{
			if (!m_elements[i].active) continue;
			const auto center = m_elements[i].bounds.center();
			const T dist = (center - point).length();
			all.push_back({dist, i});
		}

		const auto count = std::min(k, all.size());
		std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(count), all.end(),
			[](const DistHandle& a, const DistHandle& b) { return a.distance < b.distance; });

		std::vector<Handle> result;
		result.reserve(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			result.push_back(all[i].handle);
		}
		return result;
	}

	/// @brief 全要素を削除する
	void clear()
	{
		const auto bounds = m_nodes[0].bounds;
		m_nodes.clear();
		m_elements.clear();
		m_nodes.push_back(Node{bounds});
	}

	/// @brief アクティブな要素数を返す
	[[nodiscard]] std::size_t size() const noexcept
	{
		std::size_t count = 0;
		for (const auto& e : m_elements)
		{
			if (e.active) ++count;
		}
		return count;
	}

private:
	struct Element
	{
		AABB3<T> bounds;
		bool active;
	};

	struct Node
	{
		AABB3<T> bounds;
		std::vector<Handle> elements;
		std::array<int, 8> children;
		int depth{0};

		explicit Node(const AABB3<T>& b) : bounds(b)
		{
			children.fill(-1);
		}

		[[nodiscard]] bool isLeaf() const noexcept { return children[0] == -1; }
	};

	std::vector<Node> m_nodes;
	std::vector<Element> m_elements;

	void insertIntoNode(int nodeIdx, Handle handle)
	{
		if (m_nodes[nodeIdx].isLeaf())
		{
			m_nodes[nodeIdx].elements.push_back(handle);
			if (static_cast<int>(m_nodes[nodeIdx].elements.size()) > MaxPerNode
				&& m_nodes[nodeIdx].depth < MaxDepth)
			{
				subdivide(nodeIdx);
			}
			return;
		}

		const auto elemBounds = m_elements[handle].bounds;
		for (int i = 0; i < 8; ++i)
		{
			const int childIdx = m_nodes[nodeIdx].children[i];
			if (childIdx != -1 && m_nodes[childIdx].bounds.intersects(elemBounds))
			{
				insertIntoNode(childIdx, handle);
				return;
			}
		}
		m_nodes[nodeIdx].elements.push_back(handle);
	}

	void subdivide(int nodeIdx)
	{
		const auto bounds = m_nodes[nodeIdx].bounds;
		const int depth = m_nodes[nodeIdx].depth;
		const auto c = bounds.center();

		const AABB3<T> octants[8] = {
			{bounds.min, c},
			{{c.x, bounds.min.y, bounds.min.z}, {bounds.max.x, c.y, c.z}},
			{{bounds.min.x, c.y, bounds.min.z}, {c.x, bounds.max.y, c.z}},
			{{c.x, c.y, bounds.min.z}, {bounds.max.x, bounds.max.y, c.z}},
			{{bounds.min.x, bounds.min.y, c.z}, {c.x, c.y, bounds.max.z}},
			{{c.x, bounds.min.y, c.z}, {bounds.max.x, c.y, bounds.max.z}},
			{{bounds.min.x, c.y, c.z}, {c.x, bounds.max.y, bounds.max.z}},
			{c, bounds.max}
		};

		const int firstChild = static_cast<int>(m_nodes.size());
		m_nodes.reserve(m_nodes.size() + 8);

		for (int i = 0; i < 8; ++i)
		{
			m_nodes[nodeIdx].children[i] = firstChild + i;
			Node child(octants[i]);
			child.depth = depth + 1;
			m_nodes.push_back(child);
		}

		auto existing = std::move(m_nodes[nodeIdx].elements);
		m_nodes[nodeIdx].elements.clear();

		for (const auto h : existing)
		{
			if (!m_elements[h].active) continue;
			bool placed = false;
			for (int i = 0; i < 8; ++i)
			{
				const int childIdx = m_nodes[nodeIdx].children[i];
				if (m_nodes[childIdx].bounds.intersects(m_elements[h].bounds))
				{
					m_nodes[childIdx].elements.push_back(h);
					placed = true;
					break;
				}
			}
			if (!placed) m_nodes[nodeIdx].elements.push_back(h);
		}
	}

	/// @brief レイとAABB3の交差判定（スラブ法）
	[[nodiscard]] static bool rayIntersectsAABB3(const Ray3<T>& ray, const AABB3<T>& aabb, T& tMin)
	{
		T tNear = -std::numeric_limits<T>::infinity();
		T tFar = std::numeric_limits<T>::infinity();

		auto checkAxis = [&](T origin, T dir, T minVal, T maxVal) -> bool
		{
			if (std::abs(dir) < std::numeric_limits<T>::epsilon())
			{
				return origin >= minVal && origin <= maxVal;
			}
			T t1 = (minVal - origin) / dir;
			T t2 = (maxVal - origin) / dir;
			if (t1 > t2) std::swap(t1, t2);
			tNear = std::max(tNear, t1);
			tFar = std::min(tFar, t2);
			return tNear <= tFar && tFar >= T{0};
		};

		if (!checkAxis(ray.origin.x, ray.direction.x, aabb.min.x, aabb.max.x)) return false;
		if (!checkAxis(ray.origin.y, ray.direction.y, aabb.min.y, aabb.max.y)) return false;
		if (!checkAxis(ray.origin.z, ray.direction.z, aabb.min.z, aabb.max.z)) return false;

		tMin = std::max(tNear, T{0});
		return true;
	}

	/// @brief レイキャストの再帰実装
	void raycastImpl(int nodeIdx, const Ray3<T>& ray, std::vector<RaycastHit>& results) const
	{
		T tNode{};
		if (!rayIntersectsAABB3(ray, m_nodes[nodeIdx].bounds, tNode)) return;

		for (const auto h : m_nodes[nodeIdx].elements)
		{
			if (!m_elements[h].active) continue;
			T t{};
			if (rayIntersectsAABB3(ray, m_elements[h].bounds, t))
			{
				results.push_back({h, t});
			}
		}

		if (!m_nodes[nodeIdx].isLeaf())
		{
			for (int i = 0; i < 8; ++i)
			{
				if (m_nodes[nodeIdx].children[i] != -1)
				{
					raycastImpl(m_nodes[nodeIdx].children[i], ray, results);
				}
			}
		}
	}

	void queryRangeImpl(int nodeIdx, const AABB3<T>& range, std::vector<Handle>& results) const
	{
		const auto& node = m_nodes[nodeIdx];
		if (!node.bounds.intersects(range)) return;

		for (const auto h : node.elements)
		{
			if (m_elements[h].active && m_elements[h].bounds.intersects(range))
			{
				results.push_back(h);
			}
		}

		if (!node.isLeaf())
		{
			for (int i = 0; i < 8; ++i)
			{
				if (node.children[i] != -1)
				{
					queryRangeImpl(node.children[i], range, results);
				}
			}
		}
	}

	void queryFrustumImpl(int nodeIdx, const Frustum<T>& frustum, std::vector<Handle>& results) const
	{
		const auto& node = m_nodes[nodeIdx];
		if (!frustum.intersectsAABB(node.bounds)) return;

		for (const auto h : node.elements)
		{
			if (m_elements[h].active && frustum.intersectsAABB(m_elements[h].bounds))
			{
				results.push_back(h);
			}
		}

		if (!node.isLeaf())
		{
			for (int i = 0; i < 8; ++i)
			{
				if (node.children[i] != -1)
				{
					queryFrustumImpl(node.children[i], frustum, results);
				}
			}
		}
	}
};

// ── エイリアス ──────────────────────────────────────────────────

using Octreef = Octree<float>;    ///< float版 Octree
using Octreed = Octree<double>;   ///< double版 Octree

} // namespace sgc
