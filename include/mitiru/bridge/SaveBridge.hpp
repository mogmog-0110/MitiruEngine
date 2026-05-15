#pragma once

/// @file SaveBridge.hpp
/// @brief sgcセーブシステム統合ブリッジ
/// @details sgcのSaveSystem（MemorySaveStorage + SaveData）を
///          Mitiruエンジンに統合する。スロットベースのセーブ/ロードを提供。

#include <string>
#include <vector>

#include <sgc/save/SaveSystem.hpp>

namespace mitiru::bridge
{

/// @brief sgcセーブシステム統合ブリッジ
/// @details インメモリストレージを使用したスロットベースのセーブ/ロードを提供する。
///          データは文字列として保存・読み込みする簡易API。
///
/// @code
/// mitiru::bridge::SaveBridge save;
/// save.save(1, "player_data_json...");
/// auto data = save.load(1); // "player_data_json..."
/// save.deleteSave(1);
/// @endcode
class SaveBridge
{
public:
	/// @brief セーブデータを保存する
	/// @param slot スロット番号
	/// @param data セーブデータ文字列
	void save(int slot, const std::string& data)
	{
		const auto slotId = slotToId(slot);
		sgc::save::SaveSlot meta{slotId, "Slot " + std::to_string(slot), 0, 1};
		sgc::save::SaveData saveData;
		saveData.set("data", data);
		m_storage.save(meta, saveData);
	}

	/// @brief セーブデータを読み込む
	/// @param slot スロット番号
	/// @return セーブデータ文字列（存在しなければ空文字列）
	[[nodiscard]] std::string load(int slot) const
	{
		const auto slotId = slotToId(slot);
		const sgc::save::SaveSlot meta{slotId, "", 0, 1};
		const auto loaded = m_storage.load(meta);
		if (!loaded.has_value())
		{
			return "";
		}
		const auto val = loaded->getAs<std::string>("data");
		return val.value_or("");
	}

	/// @brief 指定スロットにセーブデータが存在するか
	/// @param slot スロット番号
	/// @return 存在すればtrue
	[[nodiscard]] bool exists(int slot) const
	{
		const auto slotId = slotToId(slot);
		const sgc::save::SaveSlot meta{slotId, "", 0, 1};
		return m_storage.load(meta).has_value();
	}

	/// @brief セーブデータを削除する
	/// @param slot スロット番号
	void deleteSave(int slot)
	{
		const auto slotId = slotToId(slot);
		const sgc::save::SaveSlot meta{slotId, "", 0, 1};
		m_storage.deleteSlot(meta);
	}

	/// @brief 存在するスロット番号一覧を取得する
	/// @return スロット番号のベクタ
	[[nodiscard]] std::vector<int> listSlots() const
	{
		const auto slots = m_storage.listSlots();
		std::vector<int> result;
		result.reserve(slots.size());
		for (const auto& s : slots)
		{
			/// "slot_N" からNを抽出する
			const auto prefix = std::string("slot_");
			if (s.id.size() > prefix.size() && s.id.substr(0, prefix.size()) == prefix)
			{
				try
				{
					result.push_back(std::stoi(s.id.substr(prefix.size())));
				}
				catch (...)
				{
					/// 変換失敗は無視する
				}
			}
		}
		return result;
	}

	/// @brief 保存スロット数を取得する
	/// @return スロット数
	[[nodiscard]] std::size_t slotCount() const noexcept
	{
		return m_storage.slotCount();
	}

	// ── シリアライズ ────────────────────────────────────────

	/// @brief セーブ状態をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"slotCount\":" + std::to_string(m_storage.slotCount());

		const auto slots = m_storage.listSlots();
		json += ",\"slots\":[";
		bool first = true;
		for (const auto& s : slots)
		{
			if (!first) json += ",";
			json += "{\"id\":\"" + s.id + "\",\"name\":\"" + s.name + "\"}";
			first = false;
		}
		json += "]";

		json += "}";
		return json;
	}

private:
	/// @brief スロット番号をスロットIDに変換する
	/// @param slot スロット番号
	/// @return スロットID文字列
	[[nodiscard]] static std::string slotToId(int slot)
	{
		return "slot_" + std::to_string(slot);
	}

	mutable sgc::save::MemorySaveStorage m_storage;  ///< インメモリストレージ
};

} // namespace mitiru::bridge
