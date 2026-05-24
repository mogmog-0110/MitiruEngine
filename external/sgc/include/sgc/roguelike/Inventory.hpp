#pragma once

/// @file Inventory.hpp
/// @brief インベントリシステム
///
/// ローグライクゲームのアイテム管理を提供する。
/// スタック可能アイテム・重量制限・装備スロットを実装する。
///
/// @code
/// using namespace sgc::roguelike;
/// Inventory inv(20);  // 20スロット
/// Item sword{"sword_01", "Iron Sword", false, 1, 3.5f};
/// Item potion{"potion_hp", "Health Potion", true, 10, 0.5f};
/// inv.addItem(sword, 1);
/// inv.addItem(potion, 5);
/// auto weight = inv.totalWeight();
/// @endcode

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sgc::roguelike
{

/// @brief アイテム定義
struct Item
{
	std::string id;              ///< アイテムID
	std::string name;            ///< 表示名
	bool stackable = false;      ///< スタック可能か
	int maxStack = 1;            ///< 最大スタック数
	float weight = 0.0f;         ///< 1個あたりの重量
};

/// @brief アイテムスタック（スロット内のアイテム）
struct ItemStack
{
	Item item;                   ///< アイテム定義
	int count = 0;               ///< 個数
};

/// @brief 装備スロット種別
enum class EquipSlot : uint8_t
{
	Weapon,      ///< 武器
	Armor,       ///< 防具
	Accessory,   ///< アクセサリ
	Head,        ///< 頭装備
	Feet         ///< 足装備
};

/// @brief インベントリクラス
///
/// 固定容量のスロット制インベントリを管理する。
/// スタック可能アイテムの自動合算、重量計算、装備管理を提供する。
class Inventory
{
public:
	/// @brief コンストラクタ
	/// @param capacity スロット数の上限
	explicit Inventory(std::size_t capacity = 20)
		: m_capacity(capacity)
	{
	}

	/// @brief アイテムを追加する
	///
	/// スタック可能なアイテムは既存スロットに追加する。
	/// 空きスロットがない場合はfalseを返す。
	///
	/// @param item 追加するアイテム
	/// @param count 追加する個数
	/// @return 全て追加できたらtrue
	bool addItem(const Item& item, int count = 1)
	{
		int remaining = count;

		// スタック可能なら既存スロットに追加
		if (item.stackable)
		{
			for (auto& slot : m_slots)
			{
				if (slot.item.id == item.id && slot.count < slot.item.maxStack)
				{
					const int space = slot.item.maxStack - slot.count;
					const int toAdd = std::min(space, remaining);
					slot.count += toAdd;
					remaining -= toAdd;
					if (remaining <= 0)
					{
						return true;
					}
				}
			}
		}

		// 残りを新しいスロットに追加
		while (remaining > 0)
		{
			if (m_slots.size() >= m_capacity)
			{
				return false;
			}

			ItemStack stack;
			stack.item = item;

			if (item.stackable)
			{
				stack.count = std::min(remaining, item.maxStack);
			}
			else
			{
				stack.count = 1;
			}

			remaining -= stack.count;
			m_slots.push_back(std::move(stack));
		}

		return true;
	}

	/// @brief アイテムを削除する
	///
	/// 指定IDのアイテムを指定個数削除する。
	/// スタックから順番に削除し、空になったスロットを除去する。
	///
	/// @param itemId 削除するアイテムID
	/// @param count 削除する個数
	/// @return 全て削除できたらtrue
	bool removeItem(const std::string& itemId, int count = 1)
	{
		// まず十分な数があるか確認
		int total = 0;
		for (const auto& slot : m_slots)
		{
			if (slot.item.id == itemId)
			{
				total += slot.count;
			}
		}
		if (total < count)
		{
			return false;
		}

		int remaining = count;
		for (auto it = m_slots.begin(); it != m_slots.end() && remaining > 0;)
		{
			if (it->item.id == itemId)
			{
				const int toRemove = std::min(it->count, remaining);
				it->count -= toRemove;
				remaining -= toRemove;
				if (it->count <= 0)
				{
					it = m_slots.erase(it);
					continue;
				}
			}
			++it;
		}

		return true;
	}

	/// @brief 指定インデックスのスロットを取得する
	/// @param index スロットインデックス
	/// @return スロットへのポインタ。範囲外ならnullptr
	[[nodiscard]] const ItemStack* getSlot(std::size_t index) const noexcept
	{
		if (index >= m_slots.size())
		{
			return nullptr;
		}
		return &m_slots[index];
	}

	/// @brief 指定IDのアイテム合計数を取得する
	/// @param itemId アイテムID
	/// @return アイテムの合計個数
	[[nodiscard]] int countItem(const std::string& itemId) const
	{
		int total = 0;
		for (const auto& slot : m_slots)
		{
			if (slot.item.id == itemId)
			{
				total += slot.count;
			}
		}
		return total;
	}

	/// @brief スロット容量を取得する
	/// @return スロット数の上限
	[[nodiscard]] std::size_t capacity() const noexcept
	{
		return m_capacity;
	}

	/// @brief 使用中のスロット数を取得する
	/// @return 使用中のスロット数
	[[nodiscard]] std::size_t usedSlots() const noexcept
	{
		return m_slots.size();
	}

	/// @brief 合計重量を計算する
	/// @return 全アイテムの合計重量
	[[nodiscard]] float totalWeight() const noexcept
	{
		float weight = 0.0f;
		for (const auto& slot : m_slots)
		{
			weight += slot.item.weight * static_cast<float>(slot.count);
		}
		return weight;
	}

	/// @brief 装備スロットにアイテムを装備する
	/// @param slot 装備スロット種別
	/// @param item 装備するアイテム
	void equip(EquipSlot slot, Item item)
	{
		m_equipment[slot] = std::move(item);
	}

	/// @brief 装備スロットからアイテムを外す
	/// @param slot 装備スロット種別
	void unequip(EquipSlot slot)
	{
		m_equipment.erase(slot);
	}

	/// @brief 装備スロットのアイテムを取得する
	/// @param slot 装備スロット種別
	/// @return 装備中のアイテム。未装備ならstd::nullopt
	[[nodiscard]] std::optional<Item> getEquipped(EquipSlot slot) const
	{
		const auto it = m_equipment.find(slot);
		if (it != m_equipment.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

	/// @brief インベントリを空にする
	void clear()
	{
		m_slots.clear();
	}

private:
	std::vector<ItemStack> m_slots;                    ///< アイテムスロット一覧
	std::size_t m_capacity;                            ///< スロット容量
	std::unordered_map<EquipSlot, Item> m_equipment;   ///< 装備スロットマップ

	/// @brief EquipSlotのハッシュ関数（unordered_map用）
	struct EquipSlotHash
	{
		std::size_t operator()(EquipSlot s) const noexcept
		{
			return static_cast<std::size_t>(s);
		}
	};
};

} // namespace sgc::roguelike
