#pragma once

/// @file ChapterManager.hpp
/// @brief VNチャプター管理システム
/// @details チャプター/ルートの定義、解放条件、進行状態、エンディング到達記録、
///          チャプターセレクト画面用データ提供、JSON直列化を提供する。

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <mitiru/data/Json.hpp>

namespace mitiru::vn
{

/// @brief チャプター定義
struct ChapterDef
{
	std::string id;								///< チャプターID
	std::string title;							///< タイトル
	std::string description;					///< 説明文
	std::string arcId;							///< 所属アークID（空ならメイン）
	int sortOrder = 0;							///< 表示順序
	std::string unlockCondition;				///< 解放条件式（FlagManagerで評価）
	bool unlockedByDefault = false;				///< 初期状態で解放済みか
};

/// @brief エンディング定義
struct EndingDef
{
	std::string id;								///< エンディングID
	std::string title;							///< タイトル
	std::string description;					///< 説明文
	std::string chapterId;						///< 所属チャプターID
	bool isTrue = false;						///< トゥルーエンドか
};

/// @brief チャプターの進行状態
enum class ChapterStatus
{
	Locked,		///< 未解放
	Unlocked,	///< 解放済み（未プレイ）
	InProgress,	///< プレイ中
	Completed,	///< 完了
};

/// @brief チャプターセレクト画面用の情報
struct ChapterSelectInfo
{
	std::string id;								///< チャプターID
	std::string title;							///< タイトル
	std::string description;					///< 説明文
	ChapterStatus status = ChapterStatus::Locked;	///< 進行状態
	float readPercentage = 0.0f;				///< 読了率
	std::vector<std::string> seenEndingIds;		///< 到達済みエンディングID
	int totalEndings = 0;						///< エンディング総数
};

/// @brief VNチャプター管理システム
/// @details チャプターの定義、解放判定、進行管理、エンディング到達記録を管理する。
///
/// @code
/// mitiru::vn::ChapterManager chapters;
///
/// chapters.defineChapter({"ch1", "Chapter 1", "The beginning", "", 0, "", true});
/// chapters.defineChapter({"ch2", "Chapter 2", "Continuation", "", 1, "ch1_completed == true", false});
/// chapters.defineEnding({"end_normal", "Normal End", "A quiet ending", "ch2", false});
///
/// chapters.startChapter("ch1");
/// chapters.completeChapter("ch1");
///
/// // FlagManagerと連携して解放条件を評価
/// auto evaluator = [&](const std::string& cond) { return flags.evaluate(cond); };
/// chapters.refreshUnlockStatus(evaluator);
///
/// auto list = chapters.getChapterSelectList();
/// @endcode
class ChapterManager
{
public:
	/// @brief 条件評価関数型
	using ConditionEvaluator = std::function<bool(const std::string& expression)>;

	// ── チャプター定義 ─────────────────────────────────────

	/// @brief チャプターを定義する
	/// @param def チャプター定義
	void defineChapter(ChapterDef def)
	{
		auto id = def.id;
		if (def.unlockedByDefault)
		{
			m_chapterStatus[id] = ChapterStatus::Unlocked;
		}
		else if (m_chapterStatus.find(id) == m_chapterStatus.end())
		{
			m_chapterStatus[id] = ChapterStatus::Locked;
		}
		m_chapters[id] = std::move(def);
		m_chapterOrder.push_back(id);
	}

	/// @brief エンディングを定義する
	/// @param def エンディング定義
	void defineEnding(EndingDef def)
	{
		auto id = def.id;
		m_endings[id] = std::move(def);
	}

	// ── チャプター進行 ─────────────────────────────────────

	/// @brief チャプターを開始する
	/// @param chapterId チャプターID
	/// @return 成功ならtrue（未定義または未解放の場合はfalse）
	bool startChapter(const std::string& chapterId)
	{
		auto statusIt = m_chapterStatus.find(chapterId);
		if (statusIt == m_chapterStatus.end())
		{
			return false;
		}
		if (statusIt->second == ChapterStatus::Locked)
		{
			return false;
		}

		statusIt->second = ChapterStatus::InProgress;
		m_currentChapterId = chapterId;
		return true;
	}

	/// @brief チャプターを完了にする
	/// @param chapterId チャプターID
	void completeChapter(const std::string& chapterId)
	{
		m_chapterStatus[chapterId] = ChapterStatus::Completed;
		if (m_currentChapterId == chapterId)
		{
			m_currentChapterId.clear();
		}
	}

	/// @brief 現在プレイ中のチャプターIDを取得する
	/// @return チャプターID（なければ空文字列）
	[[nodiscard]] const std::string& currentChapterId() const noexcept
	{
		return m_currentChapterId;
	}

	/// @brief チャプターの状態を取得する
	/// @param chapterId チャプターID
	/// @return チャプター状態（未定義の場合はLocked）
	[[nodiscard]] ChapterStatus getStatus(const std::string& chapterId) const
	{
		auto it = m_chapterStatus.find(chapterId);
		if (it == m_chapterStatus.end())
		{
			return ChapterStatus::Locked;
		}
		return it->second;
	}

	/// @brief チャプターが解放済みか確認する
	/// @param chapterId チャプターID
	/// @return 解放済み（Unlocked, InProgress, Completed）ならtrue
	[[nodiscard]] bool isUnlocked(const std::string& chapterId) const
	{
		auto status = getStatus(chapterId);
		return status != ChapterStatus::Locked;
	}

	/// @brief チャプターが完了済みか確認する
	/// @param chapterId チャプターID
	/// @return 完了済みならtrue
	[[nodiscard]] bool isCompleted(const std::string& chapterId) const
	{
		return getStatus(chapterId) == ChapterStatus::Completed;
	}

	/// @brief チャプターの解放状態を再評価する
	/// @param evaluator 条件評価関数（FlagManager::evaluateを渡す）
	void refreshUnlockStatus(const ConditionEvaluator& evaluator)
	{
		for (const auto& [id, def] : m_chapters)
		{
			auto& status = m_chapterStatus[id];
			if (status != ChapterStatus::Locked) continue;

			if (def.unlockedByDefault)
			{
				status = ChapterStatus::Unlocked;
				continue;
			}

			if (!def.unlockCondition.empty() && evaluator(def.unlockCondition))
			{
				status = ChapterStatus::Unlocked;
			}
		}
	}

	/// @brief チャプターを強制的に解放する
	/// @param chapterId チャプターID
	void unlock(const std::string& chapterId)
	{
		auto it = m_chapterStatus.find(chapterId);
		if (it != m_chapterStatus.end() && it->second == ChapterStatus::Locked)
		{
			it->second = ChapterStatus::Unlocked;
		}
	}

	// ── エンディング記録 ───────────────────────────────────

	/// @brief エンディング到達を記録する
	/// @param endingId エンディングID
	void markEndingSeen(const std::string& endingId)
	{
		m_seenEndings.insert(endingId);
	}

	/// @brief エンディングが到達済みか確認する
	/// @param endingId エンディングID
	/// @return 到達済みならtrue
	[[nodiscard]] bool isEndingSeen(const std::string& endingId) const
	{
		return m_seenEndings.count(endingId) > 0;
	}

	/// @brief 到達済みエンディング数を取得する
	/// @return 到達済みエンディング数
	[[nodiscard]] std::size_t seenEndingCount() const noexcept
	{
		return m_seenEndings.size();
	}

	/// @brief 全エンディング数を取得する
	/// @return 定義されたエンディングの総数
	[[nodiscard]] std::size_t totalEndingCount() const noexcept
	{
		return m_endings.size();
	}

	/// @brief 全エンディングコンプリート率を取得する
	/// @return 到達率（0.0〜100.0）
	[[nodiscard]] float endingCompletionRate() const
	{
		if (m_endings.empty()) return 0.0f;
		return static_cast<float>(m_seenEndings.size()) / static_cast<float>(m_endings.size()) * 100.0f;
	}

	/// @brief 特定チャプターのエンディング到達状況を取得する
	/// @param chapterId チャプターID
	/// @return {到達済み数, 総数}
	[[nodiscard]] std::pair<std::size_t, std::size_t> chapterEndingProgress(const std::string& chapterId) const
	{
		std::size_t total = 0;
		std::size_t seen = 0;
		for (const auto& [id, def] : m_endings)
		{
			if (def.chapterId == chapterId)
			{
				++total;
				if (m_seenEndings.count(id) > 0)
				{
					++seen;
				}
			}
		}
		return {seen, total};
	}

	// ── チャプターセレクト ─────────────────────────────────

	/// @brief チャプターセレクト画面用のリストを取得する
	/// @return チャプター情報のリスト（定義順）
	[[nodiscard]] std::vector<ChapterSelectInfo> getChapterSelectList() const
	{
		std::vector<ChapterSelectInfo> list;
		list.reserve(m_chapterOrder.size());

		for (const auto& id : m_chapterOrder)
		{
			auto defIt = m_chapters.find(id);
			if (defIt == m_chapters.end()) continue;

			ChapterSelectInfo info;
			info.id = id;
			info.title = defIt->second.title;
			info.description = defIt->second.description;
			info.status = getStatus(id);

			// エンディング情報
			for (const auto& [endId, endDef] : m_endings)
			{
				if (endDef.chapterId == id)
				{
					++info.totalEndings;
					if (m_seenEndings.count(endId) > 0)
					{
						info.seenEndingIds.push_back(endId);
					}
				}
			}

			list.push_back(std::move(info));
		}

		return list;
	}

	/// @brief チャプター定義を取得する
	/// @param chapterId チャプターID
	/// @return チャプター定義（未定義の場合はnullopt）
	[[nodiscard]] std::optional<ChapterDef> getChapter(const std::string& chapterId) const
	{
		auto it = m_chapters.find(chapterId);
		if (it == m_chapters.end()) return std::nullopt;
		return it->second;
	}

	/// @brief 定義済みチャプター数を取得する
	/// @return チャプター数
	[[nodiscard]] std::size_t chapterCount() const noexcept
	{
		return m_chapters.size();
	}

	/// @brief 完了済みチャプター数を取得する
	/// @return 完了済みチャプター数
	[[nodiscard]] std::size_t completedChapterCount() const
	{
		std::size_t count = 0;
		for (const auto& [id, status] : m_chapterStatus)
		{
			if (status == ChapterStatus::Completed) ++count;
		}
		return count;
	}

	// ── 直列化 ─────────────────────────────────────────────

	/// @brief JSON文字列として出力する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		mitiru::data::Json j;

		// チャプター状態
		mitiru::data::Json statusObj = mitiru::data::Json::object();
		for (const auto& [id, status] : m_chapterStatus)
		{
			statusObj[id] = static_cast<int>(status);
		}
		j["chapterStatus"] = std::move(statusObj);

		// 到達済みエンディング
		mitiru::data::Json endingsArr = mitiru::data::Json::array();
		for (const auto& endId : m_seenEndings)
		{
			endingsArr.push_back(endId);
		}
		j["seenEndings"] = std::move(endingsArr);

		// 現在のチャプター
		j["currentChapter"] = m_currentChapterId;

		return j.dump();
	}

	/// @brief JSON文字列から復元する（チャプター定義は保持、状態のみ復元）
	/// @param json JSON形式の文字列
	/// @return 成功ならtrue
	bool fromJson(std::string_view json)
	{
		auto j = mitiru::data::Json::parse(std::string(json), nullptr, false);
		if (j.is_discarded()) return false;

		// チャプター状態の復元
		if (j.contains("chapterStatus") && j["chapterStatus"].is_object())
		{
			for (auto it = j["chapterStatus"].begin(); it != j["chapterStatus"].end(); ++it)
			{
				if (it.value().is_number_integer())
				{
					m_chapterStatus[it.key()] = static_cast<ChapterStatus>(it.value().get<int>());
				}
			}
		}

		// 到達済みエンディングの復元
		m_seenEndings.clear();
		if (j.contains("seenEndings") && j["seenEndings"].is_array())
		{
			for (const auto& val : j["seenEndings"])
			{
				if (val.is_string())
				{
					m_seenEndings.insert(val.get<std::string>());
				}
			}
		}

		// 現在のチャプター
		if (j.contains("currentChapter") && j["currentChapter"].is_string())
		{
			m_currentChapterId = j["currentChapter"].get<std::string>();
		}

		return true;
	}

private:

	std::unordered_map<std::string, ChapterDef> m_chapters;			///< チャプター定義
	std::vector<std::string> m_chapterOrder;						///< チャプター定義順
	std::unordered_map<std::string, ChapterStatus> m_chapterStatus;	///< チャプター状態
	std::unordered_map<std::string, EndingDef> m_endings;			///< エンディング定義
	std::unordered_set<std::string> m_seenEndings;					///< 到達済みエンディング
	std::string m_currentChapterId;									///< 現在プレイ中のチャプターID
};

} // namespace mitiru::vn
