#pragma once

/// @file StateSync.hpp
/// @brief 状態同期ヘルパー
///
/// エンティティやオブジェクトの状態をバージョン管理し、
/// 変更があったもののみを差分同期するための仕組みを提供する。
///
/// @code
/// sgc::StateSyncManager sync;
///
/// // 状態の更新
/// std::vector<std::byte> data = serializeEntity(entity);
/// sync.updateSlot(entityId, std::move(data));
///
/// // 差分の取得（リモート側が知っているバージョンとの差分）
/// auto changed = sync.getChangedSlots(remoteVersions);
/// // changed を送信...
/// @endcode

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sgc
{

/// @brief 同期スロットのスナップショット
struct SyncSlot
{
	std::uint32_t id{0};          ///< スロットID
	std::uint64_t version{0};     ///< バージョン番号（変更時にインクリメント）
	std::vector<std::byte> data;  ///< シリアライズ済みデータ
};

/// @brief 状態同期マネージャ
///
/// エンティティやオブジェクトの状態をバージョン管理し、
/// 変更があったもののみを差分同期する。
class StateSyncManager
{
public:
	/// @brief スロットのデータを更新する
	/// @param id スロットID
	/// @param data シリアライズ済みデータ
	void updateSlot(std::uint32_t id, std::vector<std::byte> data)
	{
		auto& slot = m_slots[id];
		slot.id = id;
		slot.data = std::move(data);
		++slot.version;
	}

	/// @brief 指定バージョン以降に変更されたスロットを取得する
	/// @param knownVersions リモート側が知っているバージョンマップ
	/// @return 変更されたスロットのリスト
	[[nodiscard]] std::vector<SyncSlot> getChangedSlots(
		const std::unordered_map<std::uint32_t, std::uint64_t>& knownVersions) const
	{
		std::vector<SyncSlot> changed;
		for (const auto& [id, slot] : m_slots)
		{
			auto it = knownVersions.find(id);
			if (it == knownVersions.end() || it->second < slot.version)
			{
				changed.push_back(slot);
			}
		}
		return changed;
	}

	/// @brief 全スロットの現在バージョンマップを取得する
	/// @return スロットIDからバージョン番号へのマップ
	[[nodiscard]] std::unordered_map<std::uint32_t, std::uint64_t> versionMap() const
	{
		std::unordered_map<std::uint32_t, std::uint64_t> map;
		for (const auto& [id, slot] : m_slots)
		{
			map[id] = slot.version;
		}
		return map;
	}

	/// @brief リモートからのスロットデータを適用する
	/// @param remote リモートからのスロットデータ
	void applyRemoteSlot(const SyncSlot& remote)
	{
		auto& local = m_slots[remote.id];
		if (remote.version > local.version)
		{
			local = remote;
		}
	}

	/// @brief スロットを削除する
	/// @param id 削除するスロットID
	void removeSlot(std::uint32_t id) { m_slots.erase(id); }

	/// @brief 全スロットをクリアする
	void clear() { m_slots.clear(); }

	/// @brief スロット数を返す
	/// @return 登録されているスロット数
	[[nodiscard]] std::size_t slotCount() const noexcept { return m_slots.size(); }

	/// @brief 指定スロットが存在するか
	/// @param id スロットID
	/// @return 存在すればtrue
	[[nodiscard]] bool hasSlot(std::uint32_t id) const { return m_slots.contains(id); }

	/// @brief 指定スロットのデータを取得する
	/// @param id スロットID
	/// @return データへのポインタ（存在しなければnullptr）
	[[nodiscard]] const std::vector<std::byte>* slotData(std::uint32_t id) const
	{
		auto it = m_slots.find(id);
		if (it == m_slots.end()) return nullptr;
		return &it->second.data;
	}

private:
	std::unordered_map<std::uint32_t, SyncSlot> m_slots;  ///< スロットマップ
};

} // namespace sgc
