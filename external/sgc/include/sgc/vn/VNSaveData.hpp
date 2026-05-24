#pragma once

/// @file VNSaveData.hpp
/// @brief VNセーブ/ロードシステム
///
/// ビジュアルノベルのセーブスロット管理を提供する。
/// スクリプト位置・変数マップ・サムネイルデータの保存と復元を行う。
///
/// @code
/// using namespace sgc::vn;
/// VNSaveManager manager;
/// VNSaveSlot slot;
/// slot.slotId = 1;
/// slot.timestamp = "2026-03-12T10:30:00";
/// slot.scriptPosition = 42;
/// slot.variables["flag_met_hero"] = "true";
/// manager.save(slot);
/// auto loaded = manager.load(1);
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace sgc::vn
{

/// @brief セーブスロット
///
/// VNゲームの1つのセーブデータを表す。
/// スクリプト実行位置、ゲーム変数、サムネイル等を保持する。
struct VNSaveSlot
{
	int slotId = 0;                                ///< スロットID
	std::string timestamp;                         ///< 保存日時の文字列表現
	std::size_t scriptPosition = 0;                ///< スクリプト内の位置（コマンドインデックス）
	std::map<std::string, std::string> variables;  ///< ゲーム変数マップ
	std::vector<uint8_t> thumbnailData;            ///< サムネイル画像データ（任意形式）
};

/// @brief セーブスロットを文字列にシリアライズする
///
/// 簡易テキスト形式でセーブデータを出力する。
/// 形式: "SLOT:id\nTIME:timestamp\nPOS:position\nVAR:key=value\n..."
///
/// @param slot シリアライズ対象
/// @return シリアライズされた文字列
[[nodiscard]] inline std::string serializeSaveSlot(const VNSaveSlot& slot)
{
	std::ostringstream oss;
	oss << "SLOT:" << slot.slotId << "\n";
	oss << "TIME:" << slot.timestamp << "\n";
	oss << "POS:" << slot.scriptPosition << "\n";

	for (const auto& [key, value] : slot.variables)
	{
		oss << "VAR:" << key << "=" << value << "\n";
	}

	if (!slot.thumbnailData.empty())
	{
		oss << "THUMB:" << slot.thumbnailData.size() << "\n";
	}

	return oss.str();
}

/// @brief 文字列からセーブスロットをデシリアライズする
///
/// serializeSaveSlotで出力された形式を解析する。
///
/// @param data シリアライズ済み文字列
/// @return デシリアライズされたスロット。失敗時はstd::nullopt
[[nodiscard]] inline std::optional<VNSaveSlot> deserializeSaveSlot(const std::string& data)
{
	if (data.empty())
	{
		return std::nullopt;
	}

	VNSaveSlot slot;
	std::istringstream iss(data);
	std::string line;

	while (std::getline(iss, line))
	{
		if (line.empty())
		{
			continue;
		}

		const auto colonPos = line.find(':');
		if (colonPos == std::string::npos)
		{
			continue;
		}

		const std::string prefix = line.substr(0, colonPos);
		const std::string value = line.substr(colonPos + 1);

		if (prefix == "SLOT")
		{
			slot.slotId = std::stoi(value);
		}
		else if (prefix == "TIME")
		{
			slot.timestamp = value;
		}
		else if (prefix == "POS")
		{
			slot.scriptPosition = static_cast<std::size_t>(std::stoull(value));
		}
		else if (prefix == "VAR")
		{
			const auto eqPos = value.find('=');
			if (eqPos != std::string::npos)
			{
				slot.variables[value.substr(0, eqPos)] = value.substr(eqPos + 1);
			}
		}
	}

	return slot;
}

/// @brief VNセーブマネージャ
///
/// 複数のセーブスロットを管理する。
/// メモリ上でスロットの保存・読み込み・削除・一覧取得を行う。
class VNSaveManager
{
public:
	/// @brief セーブスロットを保存する
	///
	/// 既存のスロットIDがあれば上書きする。
	///
	/// @param slot 保存するスロット
	void save(VNSaveSlot slot)
	{
		const int id = slot.slotId;
		// 既存スロットを検索して上書き
		for (auto& existing : m_slots)
		{
			if (existing.slotId == id)
			{
				existing = std::move(slot);
				return;
			}
		}
		m_slots.push_back(std::move(slot));
	}

	/// @brief セーブスロットを読み込む
	/// @param slotId 読み込むスロットID
	/// @return スロットデータ。存在しない場合はstd::nullopt
	[[nodiscard]] std::optional<VNSaveSlot> load(int slotId) const
	{
		for (const auto& slot : m_slots)
		{
			if (slot.slotId == slotId)
			{
				return slot;
			}
		}
		return std::nullopt;
	}

	/// @brief 全セーブスロットの一覧を取得する
	/// @return スロット一覧への参照
	[[nodiscard]] const std::vector<VNSaveSlot>& listSlots() const noexcept
	{
		return m_slots;
	}

	/// @brief セーブスロットを削除する
	/// @param slotId 削除するスロットID
	/// @return 削除成功ならtrue
	bool deleteSlot(int slotId)
	{
		const auto it = std::remove_if(m_slots.begin(), m_slots.end(),
			[slotId](const VNSaveSlot& s) { return s.slotId == slotId; });

		if (it == m_slots.end())
		{
			return false;
		}

		m_slots.erase(it, m_slots.end());
		return true;
	}

	/// @brief スロット数を取得する
	/// @return 保存されているスロット数
	[[nodiscard]] std::size_t slotCount() const noexcept
	{
		return m_slots.size();
	}

private:
	std::vector<VNSaveSlot> m_slots;  ///< セーブスロット一覧
};

} // namespace sgc::vn
