#pragma once

/// @file ReadTracker.hpp
/// @brief VN既読管理システム
/// @details プレイヤーが読んだテキスト行をハッシュベースで追跡する。
///          スキップモード（既読のみスキップ）、バックログの既読マーク、
///          シーン/全体の読了率計算、JSON直列化を提供する。

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <mitiru/data/Json.hpp>

namespace mitiru::vn
{

/// @brief VN既読管理システム
/// @details シーンIDと行インデックスからユニークハッシュを生成し、
///          既読状態を永続化する。セーブデータとは独立して管理される。
///
/// @code
/// mitiru::vn::ReadTracker tracker;
/// tracker.registerSceneLineCount("chapter1", 50);
/// tracker.markAsRead("chapter1", 0);
/// tracker.markAsRead("chapter1", 1);
///
/// bool read = tracker.isRead("chapter1", 0);  // true
/// float pct = tracker.getReadPercentage("chapter1");  // 4.0 (2/50)
///
/// std::string json = tracker.toJson();
/// ReadTracker loaded;
/// loaded.fromJson(json);
/// @endcode
class ReadTracker
{
public:
	/// @brief 行を既読としてマークする
	/// @param sceneId シーンID
	/// @param lineIndex 行インデックス
	void markAsRead(const std::string& sceneId, std::size_t lineIndex)
	{
		auto hash = computeHash(sceneId, lineIndex);
		m_readHashes.insert(hash);
		m_sceneReadCounts[sceneId].insert(lineIndex);
	}

	/// @brief 行が既読かどうか確認する
	/// @param sceneId シーンID
	/// @param lineIndex 行インデックス
	/// @return 既読ならtrue
	[[nodiscard]] bool isRead(const std::string& sceneId, std::size_t lineIndex) const noexcept
	{
		auto hash = computeHash(sceneId, lineIndex);
		return m_readHashes.count(hash) > 0;
	}

	/// @brief シーンの総行数を登録する
	/// @param sceneId シーンID
	/// @param lineCount 総行数
	void registerSceneLineCount(const std::string& sceneId, std::size_t lineCount)
	{
		m_sceneLineCounts[sceneId] = lineCount;
	}

	/// @brief シーンの読了率を取得する
	/// @param sceneId シーンID
	/// @return 読了率（0.0〜100.0）。シーン未登録の場合は0.0
	[[nodiscard]] float getReadPercentage(const std::string& sceneId) const
	{
		auto countIt = m_sceneLineCounts.find(sceneId);
		if (countIt == m_sceneLineCounts.end() || countIt->second == 0)
		{
			return 0.0f;
		}

		auto readIt = m_sceneReadCounts.find(sceneId);
		if (readIt == m_sceneReadCounts.end())
		{
			return 0.0f;
		}

		auto readCount = readIt->second.size();
		return static_cast<float>(readCount) / static_cast<float>(countIt->second) * 100.0f;
	}

	/// @brief 全体の読了率を取得する
	/// @return 読了率（0.0〜100.0）。シーン未登録の場合は0.0
	[[nodiscard]] float getTotalReadPercentage() const
	{
		std::size_t totalLines = 0;
		std::size_t totalRead = 0;

		for (const auto& [sceneId, lineCount] : m_sceneLineCounts)
		{
			totalLines += lineCount;
			auto readIt = m_sceneReadCounts.find(sceneId);
			if (readIt != m_sceneReadCounts.end())
			{
				totalRead += readIt->second.size();
			}
		}

		if (totalLines == 0)
		{
			return 0.0f;
		}

		return static_cast<float>(totalRead) / static_cast<float>(totalLines) * 100.0f;
	}

	/// @brief シーンの既読行数を取得する
	/// @param sceneId シーンID
	/// @return 既読行数
	[[nodiscard]] std::size_t getReadCount(const std::string& sceneId) const
	{
		auto it = m_sceneReadCounts.find(sceneId);
		if (it == m_sceneReadCounts.end())
		{
			return 0;
		}
		return it->second.size();
	}

	/// @brief 全既読行数を取得する
	/// @return 既読行数の合計
	[[nodiscard]] std::size_t getTotalReadCount() const noexcept
	{
		return m_readHashes.size();
	}

	/// @brief 登録済みシーン数を取得する
	/// @return シーン数
	[[nodiscard]] std::size_t sceneCount() const noexcept
	{
		return m_sceneLineCounts.size();
	}

	/// @brief 全既読データをクリアする
	void clear()
	{
		m_readHashes.clear();
		m_sceneReadCounts.clear();
		// シーン行数登録はクリアしない（構造情報のため）
	}

	/// @brief 全データ（行数登録含む）をリセットする
	void reset()
	{
		m_readHashes.clear();
		m_sceneReadCounts.clear();
		m_sceneLineCounts.clear();
	}

	// ── 直列化 ─────────────────────────────────────────────

	/// @brief JSON文字列として出力する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		mitiru::data::Json j;
		mitiru::data::Json scenes = mitiru::data::Json::object();

		for (const auto& [sceneId, lineCount] : m_sceneLineCounts)
		{
			mitiru::data::Json sceneObj;
			sceneObj["lineCount"] = lineCount;

			mitiru::data::Json readLines = mitiru::data::Json::array();
			auto readIt = m_sceneReadCounts.find(sceneId);
			if (readIt != m_sceneReadCounts.end())
			{
				for (std::size_t lineIdx : readIt->second)
				{
					readLines.push_back(lineIdx);
				}
			}
			sceneObj["readLines"] = std::move(readLines);
			scenes[sceneId] = std::move(sceneObj);
		}

		// 未登録シーンの既読データも保存
		for (const auto& [sceneId, readLines] : m_sceneReadCounts)
		{
			if (m_sceneLineCounts.count(sceneId) > 0) continue;

			mitiru::data::Json sceneObj;
			sceneObj["lineCount"] = 0;

			mitiru::data::Json readLinesArr = mitiru::data::Json::array();
			for (std::size_t lineIdx : readLines)
			{
				readLinesArr.push_back(lineIdx);
			}
			sceneObj["readLines"] = std::move(readLinesArr);
			scenes[sceneId] = std::move(sceneObj);
		}

		j["scenes"] = std::move(scenes);
		return j.dump();
	}

	/// @brief JSON文字列から復元する
	/// @param json JSON形式の文字列
	/// @return 成功ならtrue
	bool fromJson(std::string_view json)
	{
		m_readHashes.clear();
		m_sceneReadCounts.clear();
		m_sceneLineCounts.clear();

		auto j = mitiru::data::Json::parse(std::string(json), nullptr, false);
		if (j.is_discarded()) return false;

		if (!j.contains("scenes") || !j["scenes"].is_object()) return false;

		for (auto it = j["scenes"].begin(); it != j["scenes"].end(); ++it)
		{
			const auto& sceneId = it.key();
			const auto& sceneObj = it.value();

			if (sceneObj.contains("lineCount") && sceneObj["lineCount"].is_number())
			{
				auto lineCount = sceneObj["lineCount"].get<std::size_t>();
				if (lineCount > 0)
				{
					m_sceneLineCounts[sceneId] = lineCount;
				}
			}

			if (sceneObj.contains("readLines") && sceneObj["readLines"].is_array())
			{
				for (const auto& lineVal : sceneObj["readLines"])
				{
					if (lineVal.is_number())
					{
						markAsRead(sceneId, lineVal.get<std::size_t>());
					}
				}
			}
		}

		return true;
	}

private:
	/// @brief シーンIDと行インデックスからユニークハッシュを計算する
	/// @param sceneId シーンID
	/// @param lineIndex 行インデックス
	/// @return 64ビットハッシュ値
	[[nodiscard]] static std::uint64_t computeHash(const std::string& sceneId, std::size_t lineIndex) noexcept
	{
		// FNV-1a ハッシュ
		std::uint64_t hash = 14695981039346656037ULL;
		for (char c : sceneId)
		{
			hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
			hash *= 1099511628211ULL;
		}
		// 行インデックスをセパレータ付きで混合
		hash ^= 0xFF;
		hash *= 1099511628211ULL;
		auto idx = static_cast<std::uint64_t>(lineIndex);
		for (int i = 0; i < 8; ++i)
		{
			hash ^= (idx >> (i * 8)) & 0xFF;
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	std::unordered_set<std::uint64_t> m_readHashes;									///< 既読ハッシュ集合
	std::unordered_map<std::string, std::unordered_set<std::size_t>> m_sceneReadCounts;	///< シーン別既読行
	std::unordered_map<std::string, std::size_t> m_sceneLineCounts;						///< シーン別総行数
};

} // namespace mitiru::vn
