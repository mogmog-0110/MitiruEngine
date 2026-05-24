#pragma once

/// @file PriorityQueue.hpp
/// @brief ハンドル付き優先度キュー（ヒープベース）
///
/// decreaseKeyをサポートするバイナリヒープ。
/// A*パスファインディング等のアルゴリズムに最適。
///
/// @code
/// sgc::containers::PriorityQueue<float> pq;
/// auto h1 = pq.push(5.0f);
/// auto h2 = pq.push(3.0f);
/// auto h3 = pq.push(7.0f);
///
/// float top = pq.top();   // 3.0f
/// pq.decreaseKey(h1, 1.0f);
/// top = pq.top();          // 1.0f
/// pq.pop();
/// @endcode

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sgc::containers
{

/// @brief ハンドル付き優先度キュー
///
/// バイナリヒープにハンドルベースの要素追跡を追加。
/// decreaseKey操作でヒープ内の要素の優先度を変更可能。
///
/// @tparam T 格納する値の型（比較可能であること）
/// @tparam Compare 比較関数オブジェクト（デフォルト: std::less、最小ヒープ）
template <typename T, typename Compare = std::less<T>>
class PriorityQueue
{
public:
	/// @brief ヒープ内要素を識別するハンドル
	using Handle = std::size_t;

	/// @brief 無効なハンドルを示す定数
	static constexpr Handle INVALID_HANDLE = static_cast<Handle>(-1);

	/// @brief デフォルトコンストラクタ
	PriorityQueue() = default;

	/// @brief 比較関数を指定するコンストラクタ
	/// @param comp 比較関数オブジェクト
	explicit PriorityQueue(Compare comp)
		: m_comp(std::move(comp))
	{
	}

	/// @brief 要素をキューに追加する
	/// @param value 追加する値
	/// @return 追加した要素のハンドル
	Handle push(const T& value)
	{
		const Handle handle = allocateHandle();
		const std::size_t heapIdx = m_heap.size();
		m_heap.push_back({value, handle});
		m_handleToHeap[handle] = heapIdx;
		siftUp(heapIdx);
		return handle;
	}

	/// @brief 要素をムーブでキューに追加する
	/// @param value 追加する値（ムーブ）
	/// @return 追加した要素のハンドル
	Handle push(T&& value)
	{
		const Handle handle = allocateHandle();
		const std::size_t heapIdx = m_heap.size();
		m_heap.push_back({std::move(value), handle});
		m_handleToHeap[handle] = heapIdx;
		siftUp(heapIdx);
		return handle;
	}

	/// @brief 最小（最高優先度）要素を削除する
	/// @throw std::out_of_range キューが空の場合
	void pop()
	{
		if (m_heap.empty())
		{
			throw std::out_of_range("PriorityQueue::pop - queue is empty");
		}
		const Handle removedHandle = m_heap[0].handle;
		m_handleToHeap[removedHandle] = INVALID_HANDLE;
		m_freeHandles.push_back(removedHandle);

		if (m_heap.size() > 1)
		{
			m_heap[0] = std::move(m_heap.back());
			m_heap.pop_back();
			m_handleToHeap[m_heap[0].handle] = 0;
			siftDown(0);
		}
		else
		{
			m_heap.pop_back();
		}
	}

	/// @brief 最小（最高優先度）要素への参照を返す
	/// @return 最小要素へのconst参照
	/// @throw std::out_of_range キューが空の場合
	[[nodiscard]] const T& top() const
	{
		if (m_heap.empty())
		{
			throw std::out_of_range("PriorityQueue::top - queue is empty");
		}
		return m_heap[0].value;
	}

	/// @brief ハンドルで指定した要素のキーを減少させる
	///
	/// 新しい値は現在の値以下でなければならない（最小ヒープの場合）。
	///
	/// @param handle 対象要素のハンドル
	/// @param newValue 新しい値（現在値以下）
	/// @return 操作に成功した場合true
	bool decreaseKey(Handle handle, const T& newValue)
	{
		if (!isValidHandle(handle))
		{
			return false;
		}
		const std::size_t heapIdx = m_handleToHeap[handle];
		if (!m_comp(newValue, m_heap[heapIdx].value))
		{
			return false;
		}
		m_heap[heapIdx].value = newValue;
		siftUp(heapIdx);
		return true;
	}

	/// @brief ハンドルで指定した要素のキーを更新する
	///
	/// 増減どちらにも対応する汎用版。
	///
	/// @param handle 対象要素のハンドル
	/// @param newValue 新しい値
	/// @return 操作に成功した場合true
	bool updateKey(Handle handle, const T& newValue)
	{
		if (!isValidHandle(handle))
		{
			return false;
		}
		const std::size_t heapIdx = m_handleToHeap[handle];
		m_heap[heapIdx].value = newValue;
		siftUp(heapIdx);
		siftDown(heapIdx < m_heap.size() ? m_handleToHeap[m_heap[heapIdx < m_heap.size() ? heapIdx : 0].handle] : heapIdx);
		return true;
	}

	/// @brief ハンドルから要素の値を取得する
	/// @param handle 対象要素のハンドル
	/// @return 値が存在すればoptionalで返す
	[[nodiscard]] std::optional<std::reference_wrapper<const T>> getValue(Handle handle) const
	{
		if (!isValidHandle(handle))
		{
			return std::nullopt;
		}
		return std::cref(m_heap[m_handleToHeap[handle]].value);
	}

	/// @brief ハンドルが有効か判定する
	/// @param handle 判定するハンドル
	/// @return 有効な場合true
	[[nodiscard]] bool isValidHandle(Handle handle) const noexcept
	{
		return handle < m_handleToHeap.size()
			&& m_handleToHeap[handle] != INVALID_HANDLE;
	}

	/// @brief キューの要素数を返す
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_heap.size();
	}

	/// @brief キューが空か判定する
	[[nodiscard]] bool empty() const noexcept
	{
		return m_heap.empty();
	}

	/// @brief 全要素を削除する
	void clear()
	{
		m_heap.clear();
		m_handleToHeap.clear();
		m_freeHandles.clear();
		m_nextHandle = 0;
	}

private:
	/// @brief ヒープノード
	struct HeapNode
	{
		T value;            ///< 格納値
		Handle handle;      ///< ハンドル
	};

	/// @brief 新しいハンドルを割り当てる
	/// @return 割り当てたハンドル
	Handle allocateHandle()
	{
		Handle handle;
		if (!m_freeHandles.empty())
		{
			handle = m_freeHandles.back();
			m_freeHandles.pop_back();
		}
		else
		{
			handle = m_nextHandle++;
			m_handleToHeap.push_back(INVALID_HANDLE);
		}
		return handle;
	}

	/// @brief ヒープを上方向に修復する
	/// @param idx 開始インデックス
	void siftUp(std::size_t idx)
	{
		while (idx > 0)
		{
			const std::size_t parent = (idx - 1) / 2;
			if (!m_comp(m_heap[idx].value, m_heap[parent].value))
			{
				break;
			}
			swapNodes(idx, parent);
			idx = parent;
		}
	}

	/// @brief ヒープを下方向に修復する
	/// @param idx 開始インデックス
	void siftDown(std::size_t idx)
	{
		const std::size_t heapSize = m_heap.size();
		while (true)
		{
			std::size_t smallest = idx;
			const std::size_t left = 2 * idx + 1;
			const std::size_t right = 2 * idx + 2;

			if (left < heapSize && m_comp(m_heap[left].value, m_heap[smallest].value))
			{
				smallest = left;
			}
			if (right < heapSize && m_comp(m_heap[right].value, m_heap[smallest].value))
			{
				smallest = right;
			}
			if (smallest == idx)
			{
				break;
			}
			swapNodes(idx, smallest);
			idx = smallest;
		}
	}

	/// @brief 2つのヒープノードを交換する
	/// @param a インデックスA
	/// @param b インデックスB
	void swapNodes(std::size_t a, std::size_t b)
	{
		m_handleToHeap[m_heap[a].handle] = b;
		m_handleToHeap[m_heap[b].handle] = a;
		std::swap(m_heap[a], m_heap[b]);
	}

	std::vector<HeapNode> m_heap;              ///< ヒープ配列
	std::vector<std::size_t> m_handleToHeap;   ///< ハンドル→ヒープインデックス
	std::vector<Handle> m_freeHandles;         ///< 再利用可能なハンドル
	Handle m_nextHandle = 0;                   ///< 次に割り当てるハンドル
	Compare m_comp{};                          ///< 比較関数
};

} // namespace sgc::containers
