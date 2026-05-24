#pragma once

/// @file SparseSet.hpp
/// @brief 疎密分離型コンテナ（SparseSet）
///
/// O(1)の追加・削除・検索と密な走査を両立するデータ構造。
/// ECSのコンポーネントストレージに最適。
///
/// @code
/// sgc::containers::SparseSet<float> set;
/// set.insert(42, 3.14f);
/// set.insert(10, 2.71f);
/// if (set.contains(42)) {
///     float& val = set.get(42);
/// }
/// for (const auto& val : set) {
///     // 密な配列を順に走査
/// }
/// set.remove(42);
/// @endcode

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sgc::containers
{

/// @brief 疎密分離型コンテナ
///
/// sparse配列でIDからdense配列インデックスへの高速マッピングを行い、
/// dense配列で連続メモリ走査を実現する。
///
/// @tparam T 格納する値の型（ムーブ可能であること）
template <std::movable T>
class SparseSet
{
public:
	/// @brief 無効なインデックスを示す定数
	static constexpr std::size_t INVALID_INDEX = static_cast<std::size_t>(-1);

	/// @brief デフォルトコンストラクタ
	SparseSet() = default;

	/// @brief 要素を追加する
	/// @param id エンティティID
	/// @param value 格納する値
	/// @return 挿入に成功した場合true、既に存在する場合false
	bool insert(std::size_t id, const T& value)
	{
		if (contains(id))
		{
			return false;
		}
		ensureSparseCapacity(id);
		m_sparse[id] = m_dense.size();
		m_dense.push_back(value);
		m_ids.push_back(id);
		return true;
	}

	/// @brief 要素をムーブ挿入する
	/// @param id エンティティID
	/// @param value 格納する値（ムーブ）
	/// @return 挿入に成功した場合true
	bool insert(std::size_t id, T&& value)
	{
		if (contains(id))
		{
			return false;
		}
		ensureSparseCapacity(id);
		m_sparse[id] = m_dense.size();
		m_dense.push_back(std::move(value));
		m_ids.push_back(id);
		return true;
	}

	/// @brief 要素を削除する（末尾要素とスワップ）
	/// @param id 削除するエンティティID
	/// @return 削除に成功した場合true
	bool remove(std::size_t id)
	{
		if (!contains(id))
		{
			return false;
		}
		const std::size_t denseIdx = m_sparse[id];
		const std::size_t lastIdx = m_dense.size() - 1;

		if (denseIdx != lastIdx)
		{
			// 末尾要素とスワップ
			const std::size_t lastId = m_ids[lastIdx];
			m_dense[denseIdx] = std::move(m_dense[lastIdx]);
			m_ids[denseIdx] = lastId;
			m_sparse[lastId] = denseIdx;
		}

		m_dense.pop_back();
		m_ids.pop_back();
		m_sparse[id] = INVALID_INDEX;
		return true;
	}

	/// @brief 指定IDの要素が存在するか確認する
	/// @param id エンティティID
	/// @return 存在する場合true
	[[nodiscard]] bool contains(std::size_t id) const noexcept
	{
		if (id >= m_sparse.size())
		{
			return false;
		}
		const std::size_t denseIdx = m_sparse[id];
		return denseIdx != INVALID_INDEX
			&& denseIdx < m_dense.size()
			&& m_ids[denseIdx] == id;
	}

	/// @brief 指定IDの要素への参照を取得する
	/// @param id エンティティID
	/// @return 要素への参照
	/// @throw std::out_of_range IDが存在しない場合
	[[nodiscard]] T& get(std::size_t id)
	{
		if (!contains(id))
		{
			throw std::out_of_range("SparseSet::get - ID not found");
		}
		return m_dense[m_sparse[id]];
	}

	/// @brief 指定IDの要素へのconst参照を取得する
	/// @param id エンティティID
	/// @return 要素へのconst参照
	/// @throw std::out_of_range IDが存在しない場合
	[[nodiscard]] const T& get(std::size_t id) const
	{
		if (!contains(id))
		{
			throw std::out_of_range("SparseSet::get - ID not found");
		}
		return m_dense[m_sparse[id]];
	}

	/// @brief 指定IDの要素を安全に取得する
	/// @param id エンティティID
	/// @return 要素が存在すればその値、なければnullopt
	[[nodiscard]] std::optional<std::reference_wrapper<T>> tryGet(std::size_t id)
	{
		if (!contains(id))
		{
			return std::nullopt;
		}
		return std::ref(m_dense[m_sparse[id]]);
	}

	/// @brief 格納されている要素数を返す
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_dense.size();
	}

	/// @brief コンテナが空か判定する
	[[nodiscard]] bool empty() const noexcept
	{
		return m_dense.empty();
	}

	/// @brief 全要素を削除する
	void clear()
	{
		m_dense.clear();
		m_ids.clear();
		std::fill(m_sparse.begin(), m_sparse.end(), INVALID_INDEX);
	}

	/// @brief dense配列のイテレータ（先頭）
	[[nodiscard]] auto begin() noexcept { return m_dense.begin(); }
	/// @brief dense配列のイテレータ（末尾）
	[[nodiscard]] auto end() noexcept { return m_dense.end(); }
	/// @brief dense配列のconstイテレータ（先頭）
	[[nodiscard]] auto begin() const noexcept { return m_dense.begin(); }
	/// @brief dense配列のconstイテレータ（末尾）
	[[nodiscard]] auto end() const noexcept { return m_dense.end(); }

	/// @brief ID配列への読み取りアクセス
	[[nodiscard]] const std::vector<std::size_t>& ids() const noexcept
	{
		return m_ids;
	}

	/// @brief dense配列への読み取りアクセス
	[[nodiscard]] const std::vector<T>& data() const noexcept
	{
		return m_dense;
	}

private:
	/// @brief sparse配列の容量を確保する
	void ensureSparseCapacity(std::size_t id)
	{
		if (id >= m_sparse.size())
		{
			m_sparse.resize(id + 1, INVALID_INDEX);
		}
	}

	std::vector<T> m_dense;                ///< 密配列（値を連続格納）
	std::vector<std::size_t> m_ids;        ///< ID配列（denseと対応）
	std::vector<std::size_t> m_sparse;     ///< 疎配列（ID→denseインデックス）
};

} // namespace sgc::containers
