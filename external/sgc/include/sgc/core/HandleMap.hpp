#pragma once

/// @file HandleMap.hpp
/// @brief 世代管理付きスロットマップ
///
/// Handle<Tag>をキーとした高速なデータコンテナ。
/// ComponentStorage.hppのsparse-setパターンを汎用化し、
/// 世代管理を組み込んでいる。
///
/// @code
/// struct EnemyTag {};
/// using EnemyHandle = sgc::Handle<EnemyTag>;
/// sgc::HandleMap<EnemyTag, EnemyData> enemies;
///
/// auto h = enemies.insert(EnemyData{"goblin", 100});
/// auto* data = enemies.get(h);
/// enemies.remove(h);
/// @endcode

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <vector>

#include "sgc/core/Handle.hpp"

namespace sgc
{

/// @brief 世代管理付きスロットマップ
///
/// ハンドルベースのO(1)アクセス・挿入・削除を提供する。
/// swap-and-pop方式で密なイテレーションを実現する。
///
/// @tparam Tag ハンドルのタグ型
/// @tparam T 格納する値の型（ムーブ可能であること）
template <typename Tag, std::movable T>
class HandleMap
{
public:
	/// @brief ハンドル型
	using HandleType = Handle<Tag>;

	/// @brief 新しい値を挿入してハンドルを返す
	/// @param value 挿入する値
	/// @return 発行されたハンドル
	[[nodiscard]] HandleType insert(T value)
	{
		typename HandleType::IndexType slotIndex;

		if (!m_freeSlots.empty())
		{
			slotIndex = m_freeSlots.back();
			m_freeSlots.pop_back();
		}
		else
		{
			slotIndex = static_cast<typename HandleType::IndexType>(m_slots.size());
			m_slots.push_back(Slot{});
		}

		auto& slot = m_slots[slotIndex];
		slot.denseIndex = m_dense.size();
		slot.alive = true;

		m_dense.push_back(slotIndex);
		m_data.push_back(std::move(value));

		return HandleType{slotIndex, slot.generation};
	}

	/// @brief ハンドルに対応する値を削除する（swap-and-pop, O(1)）
	/// @param handle 削除するハンドル
	void remove(HandleType handle)
	{
		if (!isValid(handle)) return;

		auto& slot = m_slots[handle.index];
		const std::size_t removedDense = slot.denseIndex;
		const std::size_t lastDense = m_dense.size() - 1;

		if (removedDense != lastDense)
		{
			// 末尾要素と交換
			const auto lastSlotIndex = m_dense[lastDense];
			m_dense[removedDense] = lastSlotIndex;
			m_data[removedDense] = std::move(m_data[lastDense]);
			m_slots[lastSlotIndex].denseIndex = removedDense;
		}

		m_dense.pop_back();
		m_data.pop_back();

		slot.alive = false;
		++slot.generation;
		m_freeSlots.push_back(handle.index);
	}

	/// @brief ハンドルに対応する値を取得する
	/// @param handle ハンドル
	/// @return 値へのポインタ（無効なら nullptr）
	[[nodiscard]] T* get(HandleType handle)
	{
		if (!isValid(handle)) return nullptr;
		return &m_data[m_slots[handle.index].denseIndex];
	}

	/// @brief ハンドルに対応する値を取得する（const版）
	/// @param handle ハンドル
	/// @return 値へのconstポインタ（無効なら nullptr）
	[[nodiscard]] const T* get(HandleType handle) const
	{
		if (!isValid(handle)) return nullptr;
		return &m_data[m_slots[handle.index].denseIndex];
	}

	/// @brief ハンドルが有効かどうか確認する
	/// @param handle ハンドル
	/// @return 有効ならtrue
	[[nodiscard]] bool isValid(HandleType handle) const noexcept
	{
		if (handle.index >= m_slots.size()) return false;
		const auto& slot = m_slots[handle.index];
		return slot.alive && slot.generation == handle.generation;
	}

	/// @brief 格納されている要素数
	/// @return 要素数
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_data.size();
	}

	/// @brief コンテナが空かどうか
	/// @return 空ならtrue
	[[nodiscard]] bool empty() const noexcept
	{
		return m_data.empty();
	}

	/// @brief 全要素に対して関数を実行する
	/// @tparam Func コールバック型 (HandleType, T&)
	/// @param func コールバック関数
	template <typename Func>
	void forEach(Func&& func)
	{
		for (std::size_t i = 0; i < m_dense.size(); ++i)
		{
			const auto slotIndex = m_dense[i];
			const auto& slot = m_slots[slotIndex];
			HandleType handle{slotIndex, slot.generation};
			func(handle, m_data[i]);
		}
	}

	/// @brief 全要素に対して関数を実行する（const版）
	/// @tparam Func コールバック型 (HandleType, const T&)
	/// @param func コールバック関数
	template <typename Func>
	void forEach(Func&& func) const
	{
		for (std::size_t i = 0; i < m_dense.size(); ++i)
		{
			const auto slotIndex = m_dense[i];
			const auto& slot = m_slots[slotIndex];
			HandleType handle{slotIndex, slot.generation};
			func(handle, m_data[i]);
		}
	}

	/// @brief 全要素を削除する
	void clear()
	{
		m_slots.clear();
		m_dense.clear();
		m_data.clear();
		m_freeSlots.clear();
	}

private:
	/// @brief スロット情報
	struct Slot
	{
		std::size_t denseIndex{0};                              ///< dense配列でのインデックス
		typename HandleType::GenerationType generation{0};      ///< 世代番号
		bool alive{false};                                      ///< 生存フラグ
	};

	std::vector<Slot> m_slots;                                              ///< スロット配列
	std::vector<typename HandleType::IndexType> m_dense;                    ///< パック済みスロットインデックス
	std::vector<T> m_data;                                                  ///< パック済みデータ
	std::vector<typename HandleType::IndexType> m_freeSlots;                ///< 再利用可能スロット
};

} // namespace sgc
