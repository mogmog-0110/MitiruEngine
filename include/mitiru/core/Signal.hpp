#pragma once

/// @file Signal.hpp
/// @brief Godot風Signal/Slotイベントシステム
/// @details 型安全なイベント通信。connect/emit/disconnectパターン。
///          任意の引数型をサポートし、複数のスロットに同時配信する。
///
/// @code
/// mitiru::Signal<int, float> onDamage;
/// auto id = onDamage.connect([](int dmg, float knockback) {
///     // ダメージ処理
/// });
/// onDamage.emit(10, 1.5f);
/// onDamage.disconnect(id);
/// @endcode

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace mitiru
{

/// @brief Signal（イベント発行器）
/// @tparam Args イベント引数の型リスト
template <typename... Args>
class Signal
{
public:
	/// @brief スロット関数型
	using Slot = std::function<void(Args...)>;

	/// @brief スロットID型
	using SlotId = uint32_t;

	/// @brief スロットを接続する
	/// @param slot コールバック関数
	/// @return 接続ID（disconnect用）
	SlotId connect(Slot slot)
	{
		const SlotId id = m_nextId++;
		m_slots[id] = std::move(slot);
		return id;
	}

	/// @brief スロットを切断する
	/// @param id connect()が返したID
	void disconnect(SlotId id)
	{
		m_slots.erase(id);
	}

	/// @brief 全スロットを切断する
	void disconnectAll()
	{
		m_slots.clear();
	}

	/// @brief シグナルを発行する（全スロットを呼び出す）
	/// @param args イベント引数
	/// @note スロット内からの connect/disconnect に対して安全
	void emit(Args... args) const
	{
		/// コールバック中のconnect/disconnectによるiterator invalidation対策
		const auto snapshot = m_slots;
		for (const auto& [id, slot] : snapshot)
		{
			slot(args...);
		}
	}

	/// @brief 接続数を取得する
	/// @return 接続中のスロット数
	[[nodiscard]] int connectionCount() const noexcept
	{
		return static_cast<int>(m_slots.size());
	}

private:
	std::unordered_map<SlotId, Slot> m_slots; ///< スロットマップ
	SlotId m_nextId = 1;                       ///< 次のスロットID
};

} // namespace mitiru
