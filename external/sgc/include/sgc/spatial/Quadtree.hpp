#pragma once

/// @file Quadtree.hpp
/// @brief 四分木（Quadtree）による2D空間分割
///
/// 2Dゲームのブロードフェーズ衝突検出や近傍探索に使用する。
/// ハンドルベースのAPIで、要素の挿入・削除・更新・クエリを提供する。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "sgc/math/Geometry.hpp"

namespace sgc
{

/// @brief 四分木
/// @tparam T 浮動小数点型
/// @tparam MaxDepth 最大深度
/// @tparam MaxPerNode ノードあたりの最大要素数
///
/// @code
/// sgc::Quadtreef qt(sgc::AABB2f{{0,0}, {1000,1000}});
/// auto h = qt.insert(sgc::AABB2f{{10,10}, {20,20}});
/// auto results = qt.queryRange(sgc::AABB2f{{0,0}, {50,50}});
/// qt.remove(h);
/// @endcode
template <FloatingPoint T, int MaxDepth = 8, int MaxPerNode = 8>
class Quadtree
{
public:
	/// @brief 要素ハンドル型
	using Handle = std::size_t;

	/// @brief 全体の境界を指定して構築する
	/// @param bounds クアッドツリーの全体境界
	explicit Quadtree(const AABB2<T>& bounds)
	{
		m_nodes.push_back(Node{bounds, {}, {-1, -1, -1, -1}, -1, 0});
	}

	/// @brief 要素を挿入する
	/// @param bounds 要素のバウンディングボックス
	/// @return 要素のハンドル
	Handle insert(const AABB2<T>& bounds)
	{
		const Handle handle = m_elements.size();
		m_elements.push_back({bounds, true});
		insertIntoNode(0, handle);
		return handle;
	}

	/// @brief 要素を削除する
	/// @param handle 削除する要素のハンドル
	void remove(Handle handle)
	{
		if (handle < m_elements.size())
		{
			m_elements[handle].active = false;
		}
	}

	/// @brief 要素の位置を更新する
	/// @param handle 更新する要素のハンドル
	/// @param newBounds 新しいバウンディングボックス
	void update(Handle handle, const AABB2<T>& newBounds)
	{
		if (handle < m_elements.size())
		{
			m_elements[handle].bounds = newBounds;
			m_elements[handle].active = true;
			// 簡易実装: 全再構築ではなく要素だけ更新
			// 本格的な実装ではremove+insertだが、ここでは境界だけ更新
		}
	}

	/// @brief 範囲内の要素を検索する
	/// @param range 検索範囲
	/// @return 範囲内の要素ハンドルリスト
	[[nodiscard]] std::vector<Handle> queryRange(const AABB2<T>& range) const
	{
		std::vector<Handle> results;
		queryRangeImpl(0, range, results);
		return results;
	}

	/// @brief 点を含む要素を検索する
	/// @param point 検索点
	/// @return 点を含む要素ハンドルリスト
	[[nodiscard]] std::vector<Handle> queryPoint(const Vec2<T>& point) const
	{
		std::vector<Handle> results;
		const AABB2<T> pointBox{point, point};
		queryRangeImpl(0, pointBox, results);
		return results;
	}

	/// @brief 円内の要素を検索する
	/// @param center 円の中心
	/// @param radius 半径
	/// @return 円と交差する要素ハンドルリスト
	[[nodiscard]] std::vector<Handle> queryRadius(const Vec2<T>& center, T radius) const
	{
		// AABBで大まかにフィルタ後、距離チェック
		const AABB2<T> range{
			{center.x - radius, center.y - radius},
			{center.x + radius, center.y + radius}
		};
		auto candidates = queryRange(range);

		std::vector<Handle> results;
		const T r2 = radius * radius;
		for (const auto h : candidates)
		{
			const auto& bounds = m_elements[h].bounds;
			// AABBの最近接点と円中心の距離をチェック
			T cx = center.x;
			if (cx < bounds.min.x) cx = bounds.min.x;
			else if (cx > bounds.max.x) cx = bounds.max.x;
			T cy = center.y;
			if (cy < bounds.min.y) cy = bounds.min.y;
			else if (cy > bounds.max.y) cy = bounds.max.y;

			const T dx = center.x - cx;
			const T dy = center.y - cy;
			if (dx * dx + dy * dy <= r2)
			{
				results.push_back(h);
			}
		}
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
	[[nodiscard]] std::vector<RaycastHit> raycast(const Ray2<T>& ray) const
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
	/// 要素の中心点を基準に距離を計算する。
	///
	/// @param point 検索点
	/// @param k 取得する要素数
	/// @return ハンドルリスト（距離ソート済み）
	[[nodiscard]] std::vector<Handle> findKNearest(const Vec2<T>& point, std::size_t k) const
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
		m_nodes.push_back(Node{bounds, {}, {-1, -1, -1, -1}, -1, 0});
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

	/// @brief 要素のバウンディングボックスを取得する
	/// @param handle 要素ハンドル
	[[nodiscard]] const AABB2<T>& getBounds(Handle handle) const
	{
		return m_elements[handle].bounds;
	}

private:
	struct Element
	{
		AABB2<T> bounds;
		bool active;
	};

	struct Node
	{
		AABB2<T> bounds;
		std::vector<Handle> elements;
		std::array<int, 4> children;  // 子ノードのインデックス（-1: なし）
		int parent;
		int depth;

		Node() : children{-1, -1, -1, -1}, parent(-1), depth(0) {}
		Node(const AABB2<T>& b, const std::vector<Handle>& e,
			const std::array<int, 4>& c, int p, int d)
			: bounds(b), elements(e), children(c), parent(p), depth(d) {}

		[[nodiscard]] bool isLeaf() const noexcept
		{
			return children[0] == -1;
		}
	};

	std::vector<Node> m_nodes;
	std::vector<Element> m_elements;

	/// @brief ノードに要素を挿入する
	/// @note m_nodesのreallocationを考慮し、参照ではなくインデックスで操作する
	void insertIntoNode(int nodeIdx, Handle handle)
	{
		if (m_nodes[nodeIdx].isLeaf())
		{
			m_nodes[nodeIdx].elements.push_back(handle);

			// 分割が必要か判定（subdivideでm_nodesが再割り当てされる可能性あり）
			if (static_cast<int>(m_nodes[nodeIdx].elements.size()) > MaxPerNode
				&& m_nodes[nodeIdx].depth < MaxDepth)
			{
				subdivide(nodeIdx);
			}
			return;
		}

		// 子ノードに振り分け
		const auto elemBounds = m_elements[handle].bounds;  // コピー（参照無効化防止）
		for (int i = 0; i < 4; ++i)
		{
			const int childIdx = m_nodes[nodeIdx].children[i];
			if (childIdx != -1 && m_nodes[childIdx].bounds.intersects(elemBounds))
			{
				insertIntoNode(childIdx, handle);
				return;
			}
		}

		// どの子にも入らない場合は親ノードに保持
		m_nodes[nodeIdx].elements.push_back(handle);
	}

	/// @brief ノードを4分割する
	void subdivide(int nodeIdx)
	{
		// 値をコピーしてからpush_back（reallocation対策）
		const auto bounds = m_nodes[nodeIdx].bounds;
		const int depth = m_nodes[nodeIdx].depth;
		const auto center = bounds.center();

		const AABB2<T> quads[4] = {
			{bounds.min, center},
			{{center.x, bounds.min.y}, {bounds.max.x, center.y}},
			{{bounds.min.x, center.y}, {center.x, bounds.max.y}},
			{center, bounds.max}
		};

		// 子ノードのインデックスを予約
		const int firstChild = static_cast<int>(m_nodes.size());

		// 一括でreserveして再割り当てを防ぐ
		m_nodes.reserve(m_nodes.size() + 4);

		for (int i = 0; i < 4; ++i)
		{
			m_nodes[nodeIdx].children[i] = firstChild + i;
			m_nodes.push_back(Node{quads[i], {}, {-1, -1, -1, -1}, nodeIdx, depth + 1});
		}

		// 既存要素を再配置
		auto existing = std::move(m_nodes[nodeIdx].elements);
		m_nodes[nodeIdx].elements.clear();

		for (const auto h : existing)
		{
			if (!m_elements[h].active) continue;
			bool placed = false;
			for (int i = 0; i < 4; ++i)
			{
				const int childIdx = m_nodes[nodeIdx].children[i];
				if (m_nodes[childIdx].bounds.intersects(m_elements[h].bounds))
				{
					m_nodes[childIdx].elements.push_back(h);
					placed = true;
					break;
				}
			}
			if (!placed)
			{
				m_nodes[nodeIdx].elements.push_back(h);
			}
		}
	}

	/// @brief レイとAABB2の交差判定（スラブ法）
	/// @return 交差する場合true、tMinに交差距離を格納
	[[nodiscard]] static bool rayIntersectsAABB2(const Ray2<T>& ray, const AABB2<T>& aabb, T& tMin)
	{
		T tNear = -std::numeric_limits<T>::infinity();
		T tFar = std::numeric_limits<T>::infinity();

		// X軸
		if (std::abs(ray.direction.x) < std::numeric_limits<T>::epsilon())
		{
			if (ray.origin.x < aabb.min.x || ray.origin.x > aabb.max.x) return false;
		}
		else
		{
			T t1 = (aabb.min.x - ray.origin.x) / ray.direction.x;
			T t2 = (aabb.max.x - ray.origin.x) / ray.direction.x;
			if (t1 > t2) std::swap(t1, t2);
			tNear = std::max(tNear, t1);
			tFar = std::min(tFar, t2);
			if (tNear > tFar || tFar < T{0}) return false;
		}

		// Y軸
		if (std::abs(ray.direction.y) < std::numeric_limits<T>::epsilon())
		{
			if (ray.origin.y < aabb.min.y || ray.origin.y > aabb.max.y) return false;
		}
		else
		{
			T t1 = (aabb.min.y - ray.origin.y) / ray.direction.y;
			T t2 = (aabb.max.y - ray.origin.y) / ray.direction.y;
			if (t1 > t2) std::swap(t1, t2);
			tNear = std::max(tNear, t1);
			tFar = std::min(tFar, t2);
			if (tNear > tFar || tFar < T{0}) return false;
		}

		tMin = std::max(tNear, T{0});
		return true;
	}

	/// @brief レイキャストの再帰実装
	void raycastImpl(int nodeIdx, const Ray2<T>& ray, std::vector<RaycastHit>& results) const
	{
		T tNode{};
		if (!rayIntersectsAABB2(ray, m_nodes[nodeIdx].bounds, tNode)) return;

		// このノードの要素をチェック
		for (const auto h : m_nodes[nodeIdx].elements)
		{
			if (!m_elements[h].active) continue;
			T t{};
			if (rayIntersectsAABB2(ray, m_elements[h].bounds, t))
			{
				results.push_back({h, t});
			}
		}

		// 子ノードを再帰チェック
		if (!m_nodes[nodeIdx].isLeaf())
		{
			for (int i = 0; i < 4; ++i)
			{
				if (m_nodes[nodeIdx].children[i] != -1)
				{
					raycastImpl(m_nodes[nodeIdx].children[i], ray, results);
				}
			}
		}
	}

	/// @brief 範囲クエリの再帰実装
	void queryRangeImpl(int nodeIdx, const AABB2<T>& range, std::vector<Handle>& results) const
	{
		const auto& node = m_nodes[nodeIdx];
		if (!node.bounds.intersects(range)) return;

		// このノードの要素をチェック
		for (const auto h : node.elements)
		{
			if (m_elements[h].active && m_elements[h].bounds.intersects(range))
			{
				results.push_back(h);
			}
		}

		// 子ノードを再帰チェック
		if (!node.isLeaf())
		{
			for (int i = 0; i < 4; ++i)
			{
				if (node.children[i] != -1)
				{
					queryRangeImpl(node.children[i], range, results);
				}
			}
		}
	}
};

// ── エイリアス ──────────────────────────────────────────────────

using Quadtreef = Quadtree<float>;    ///< float版 Quadtree
using Quadtreed = Quadtree<double>;   ///< double版 Quadtree

} // namespace sgc
