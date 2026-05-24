#pragma once

/// @file SaveSystem.hpp
/// @brief セーブ/ロード管理システム
///
/// セーブスロットの管理、セーブデータの保存・読み込み、
/// インメモリストレージを提供する。
///
/// @code
/// using namespace sgc::save;
/// MemorySaveStorage storage;
/// SaveData data;
/// data.set("playerName", std::string("Hero"));
/// data.set("level", 10);
/// data.set("hp", 85.5f);
/// SaveSlot slot{"slot1", "Save 1", 12345, 1};
/// storage.save(slot, data);
/// auto loaded = storage.load(slot);
/// @endcode

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace sgc::save
{

/// @brief セーブデータの値型
using SaveValue = std::variant<int, float, bool, std::string, std::vector<uint8_t>>;

/// @brief セーブスロットのメタデータ
struct SaveSlot
{
	std::string id;             ///< スロット識別子
	std::string name;           ///< 表示名
	uint64_t timestamp{0};      ///< タイムスタンプ（エポック秒）
	uint32_t version{1};        ///< セーブデータバージョン
};

/// @brief セーブデータ（キー・バリューストア）
///
/// 文字列キーで様々な型の値を管理する。
/// int, float, bool, string, バイナリデータ(vector<uint8_t>)をサポート。
class SaveData
{
public:
	/// @brief 値を設定する
	/// @param key キー名
	/// @param value 設定する値
	void set(const std::string& key, SaveValue value)
	{
		m_entries[key] = std::move(value);
	}

	/// @brief 値を取得する
	/// @param key キー名
	/// @return 値。存在しなければstd::nullopt
	[[nodiscard]] std::optional<SaveValue> get(const std::string& key) const
	{
		const auto it = m_entries.find(key);
		if (it == m_entries.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	/// @brief 指定型で値を取得する
	/// @tparam T 期待する型
	/// @param key キー名
	/// @return 値。存在しないか型が一致しなければstd::nullopt
	template <typename T>
	[[nodiscard]] std::optional<T> getAs(const std::string& key) const
	{
		const auto it = m_entries.find(key);
		if (it == m_entries.end())
		{
			return std::nullopt;
		}
		if (const auto* ptr = std::get_if<T>(&it->second))
		{
			return *ptr;
		}
		return std::nullopt;
	}

	/// @brief キーが存在するか
	/// @param key キー名
	/// @return 存在すればtrue
	[[nodiscard]] bool has(const std::string& key) const noexcept
	{
		return m_entries.find(key) != m_entries.end();
	}

	/// @brief キーを削除する
	/// @param key キー名
	/// @return 削除に成功すればtrue
	bool remove(const std::string& key)
	{
		return m_entries.erase(key) > 0;
	}

	/// @brief エントリ数を取得
	/// @return エントリ数
	[[nodiscard]] size_t size() const noexcept { return m_entries.size(); }

	/// @brief データが空か
	/// @return 空ならtrue
	[[nodiscard]] bool empty() const noexcept { return m_entries.empty(); }

	/// @brief 全データをクリアする
	void clear() noexcept { m_entries.clear(); }

	/// @brief バージョン番号を設定する
	/// @param v バージョン番号
	void setVersion(uint32_t v) noexcept { m_version = v; }

	/// @brief バージョン番号を取得
	/// @return バージョン番号
	[[nodiscard]] uint32_t version() const noexcept { return m_version; }

	/// @brief エントリマップへの読み取り専用アクセス
	/// @return エントリマップ
	[[nodiscard]] const std::unordered_map<std::string, SaveValue>& entries() const noexcept
	{
		return m_entries;
	}

private:
	std::unordered_map<std::string, SaveValue> m_entries;  ///< データエントリ
	uint32_t m_version{1};                                  ///< バージョン番号
};

/// @brief セーブストレージインターフェース
///
/// セーブデータの永続化を抽象化する。
/// ファイル、データベース、クラウドなど様々な実装に対応。
class ISaveStorage
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ISaveStorage() = default;

	/// @brief セーブデータを保存する
	/// @param slot セーブスロット
	/// @param data セーブデータ
	/// @return 保存に成功すればtrue
	virtual bool save(const SaveSlot& slot, const SaveData& data) = 0;

	/// @brief セーブデータを読み込む
	/// @param slot セーブスロット
	/// @return セーブデータ。存在しなければstd::nullopt
	[[nodiscard]] virtual std::optional<SaveData> load(const SaveSlot& slot) = 0;

	/// @brief 全スロット一覧を取得する
	/// @return スロット一覧
	[[nodiscard]] virtual std::vector<SaveSlot> listSlots() = 0;

	/// @brief スロットを削除する
	/// @param slot セーブスロット
	/// @return 削除に成功すればtrue
	virtual bool deleteSlot(const SaveSlot& slot) = 0;
};

/// @brief インメモリセーブストレージ（テスト用）
///
/// メモリ上にセーブデータを保持する。
/// テストやプロトタイピングに最適。
class MemorySaveStorage final : public ISaveStorage
{
public:
	/// @brief セーブデータを保存する
	/// @param slot セーブスロット
	/// @param data セーブデータ
	/// @return 常にtrue
	bool save(const SaveSlot& slot, const SaveData& data) override
	{
		m_storage[slot.id] = {slot, data};
		return true;
	}

	/// @brief セーブデータを読み込む
	/// @param slot セーブスロット
	/// @return セーブデータ。存在しなければstd::nullopt
	[[nodiscard]] std::optional<SaveData> load(const SaveSlot& slot) override
	{
		const auto it = m_storage.find(slot.id);
		if (it == m_storage.end())
		{
			return std::nullopt;
		}
		return it->second.second;
	}

	/// @brief 全スロット一覧を取得する
	/// @return スロット一覧
	[[nodiscard]] std::vector<SaveSlot> listSlots() override
	{
		std::vector<SaveSlot> result;
		result.reserve(m_storage.size());
		for (const auto& [id, entry] : m_storage)
		{
			result.push_back(entry.first);
		}
		return result;
	}

	/// @brief スロットを削除する
	/// @param slot セーブスロット
	/// @return 削除に成功すればtrue
	bool deleteSlot(const SaveSlot& slot) override
	{
		return m_storage.erase(slot.id) > 0;
	}

	/// @brief 保存されたスロット数を取得
	/// @return スロット数
	[[nodiscard]] size_t slotCount() const noexcept { return m_storage.size(); }

private:
	/// @brief スロットID → (メタデータ, データ) のマップ
	std::unordered_map<std::string, std::pair<SaveSlot, SaveData>> m_storage;
};

} // namespace sgc::save
