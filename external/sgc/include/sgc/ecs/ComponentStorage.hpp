#pragma once

/// @file ComponentStorage.hpp
/// @brief スパースセット方式のコンポーネントストレージ
///
/// エンティティIDをキーとしてコンポーネントを格納する。
/// swap-and-pop方式のO(1)削除と、密にパックされたイテレーションを実現する。

#include <cassert>
#include <concepts>
#include <cstddef>
#include <limits>
#include <vector>

#include "sgc/ecs/Entity.hpp"

namespace sgc::ecs
{

/// @brief 無効なインデックス
constexpr std::size_t INVALID_INDEX = std::numeric_limits<std::size_t>::max();

/// @brief スパースセット方式のコンポーネントストレージ
/// @tparam T コンポーネント型（ムーブ可能であること）
///
/// m_sparse[EntityId] → denseインデックス
/// m_dense[index]     → EntityId
/// m_components[index]→ コンポーネント（denseと並列）
///
/// @code
/// sgc::ecs::ComponentStorage<Position> positions;
/// positions.add(entityId, Position{10.0f, 20.0f});
/// auto* p = positions.get(entityId);
/// @endcode
template <std::movable T>
class ComponentStorage
{
public:
	/// @brief エンティティにコンポーネントを追加する
	/// @param id エンティティID
	/// @param component コンポーネント
	void add(EntityId id, T component)
	{
		ensureSparseSize(id);
		assert(!has(id) && "Component already exists for this entity");
		m_sparse[id] = m_dense.size();
		m_dense.push_back(id);
		m_components.push_back(std::move(component));
	}

	/// @brief エンティティからコンポーネントを削除する（swap-and-pop, O(1)）
	/// @param id エンティティID
	void remove(EntityId id)
	{
		if (!has(id)) return;

		const std::size_t removedIndex = m_sparse[id];
		const std::size_t lastIndex = m_dense.size() - 1;

		if (removedIndex != lastIndex)
		{
			// 末尾要素と交換
			const EntityId lastEntity = m_dense[lastIndex];
			m_dense[removedIndex] = lastEntity;
			m_components[removedIndex] = std::move(m_components[lastIndex]);
			m_sparse[lastEntity] = removedIndex;
		}

		m_dense.pop_back();
		m_components.pop_back();
		m_sparse[id] = INVALID_INDEX;
	}

	/// @brief エンティティのコンポーネントを取得する
	/// @param id エンティティID
	/// @return コンポーネントへのポインタ（存在しなければnullptr）
	[[nodiscard]] T* get(EntityId id)
	{
		if (!has(id)) return nullptr;
		return &m_components[m_sparse[id]];
	}

	/// @brief エンティティのコンポーネントを取得する（const版）
	/// @param id エンティティID
	/// @return コンポーネントへのconstポインタ（存在しなければnullptr）
	[[nodiscard]] const T* get(EntityId id) const
	{
		if (!has(id)) return nullptr;
		return &m_components[m_sparse[id]];
	}

	/// @brief エンティティがコンポーネントを持つか確認する
	/// @param id エンティティID
	/// @return 持っていればtrue
	[[nodiscard]] bool has(EntityId id) const noexcept
	{
		return id < m_sparse.size() && m_sparse[id] != INVALID_INDEX;
	}

	/// @brief 格納されているコンポーネント数
	/// @return 要素数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_dense.size();
	}

	/// @brief パック済みエンティティID列
	/// @return エンティティID配列への参照
	[[nodiscard]] const std::vector<EntityId>& entities() const noexcept
	{
		return m_dense;
	}

	/// @brief パック済みコンポーネント列
	/// @return コンポーネント配列への参照
	[[nodiscard]] const std::vector<T>& components() const noexcept
	{
		return m_components;
	}

private:
	std::vector<std::size_t> m_sparse;    ///< EntityId → denseインデックス
	std::vector<EntityId> m_dense;        ///< パック済みEntityID
	std::vector<T> m_components;          ///< パック済みコンポーネント

	/// @brief sparseベクタのサイズを確保する
	void ensureSparseSize(EntityId id)
	{
		if (id >= m_sparse.size())
		{
			m_sparse.resize(static_cast<std::size_t>(id) + 1, INVALID_INDEX);
		}
	}
};

} // namespace sgc::ecs
