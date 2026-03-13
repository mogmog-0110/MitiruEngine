#pragma once

/// @file AchievementSystem.hpp
/// @brief 実績追跡システム
///
/// ゲーム内実績の定義・アンロック・最新アンロック通知を管理する。
/// SharedDataの状態に基づいて自動チェックも可能。
///
/// @code
/// mitiru::gw::AchievementSystem ach;
/// mitiru::gw::SharedData shared;
/// ach.define({"first_formula", "First Formula", "Submit your first formula", false});
/// ach.unlock("first_formula", shared);
/// auto recent = ach.getRecentUnlock(); // ポップアップ表示用
/// @endcode

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "mitiru/graphwalker/Common.hpp"

namespace mitiru::gw
{

/// @brief 実績情報
struct Achievement
{
	std::string id;          ///< 一意な実績ID
	std::string name;        ///< 表示名
	std::string description; ///< 説明文
	bool unlocked{false};    ///< アンロック済みか
};

/// @brief 実績管理システム
///
/// 定義された実績を追跡し、条件を満たした際にアンロックする。
/// 最近アンロックされた実績をポップアップ表示用に保持する。
class AchievementSystem
{
public:
	/// @brief デフォルトコンストラクタ（組み込み実績を登録）
	AchievementSystem()
	{
		defineBuiltins();
	}

	/// @brief 実績を定義（登録）する
	/// @param achievement 実績情報
	void define(Achievement achievement)
	{
		// 同一IDが既に存在する場合は上書き
		for (auto& a : m_achievements)
		{
			if (a.id == achievement.id)
			{
				a = std::move(achievement);
				return;
			}
		}
		m_achievements.push_back(std::move(achievement));
	}

	/// @brief 全実績の条件をチェックする
	/// @param shared ゲーム共有データ
	void check(const SharedData& shared)
	{
		// explorer_10: 10箇所のチェックポイント訪問
		if (shared.visitedCheckpoints.size() >= 10)
		{
			unlock("explorer_10", shared);
		}

		// collector_5: 5つのアイテム収集
		if (shared.collectedItems.size() >= 5)
		{
			unlock("collector_5", shared);
		}

		// speedrun: 30分以内にクリア
		if (shared.state == GameState::Clear
			&& shared.playTime < 1800.0f)
		{
			unlock("speedrun", shared);
		}
	}

	/// @brief 実績をアンロックする
	/// @param id 実績ID
	/// @param shared ゲーム共有データ（将来の拡張用）
	/// @return 新規アンロックならtrue、既にアンロック済みならfalse
	bool unlock(const std::string& id, const SharedData& /*shared*/)
	{
		for (auto& a : m_achievements)
		{
			if (a.id == id && !a.unlocked)
			{
				a.unlocked = true;
				m_recentUnlock = a;
				return true;
			}
		}
		return false;
	}

	/// @brief 全実績を取得する
	/// @return 実績配列のコピー
	[[nodiscard]] std::vector<Achievement> getAll() const
	{
		return m_achievements;
	}

	/// @brief アンロック済み実績を取得する
	/// @return アンロック済み実績の配列
	[[nodiscard]] std::vector<Achievement> getUnlocked() const
	{
		std::vector<Achievement> result;
		for (const auto& a : m_achievements)
		{
			if (a.unlocked)
			{
				result.push_back(a);
			}
		}
		return result;
	}

	/// @brief 最近アンロックされた実績を取得する（ポップアップ用）
	/// @return 最近の実績（なければnullopt）
	[[nodiscard]] std::optional<Achievement> getRecentUnlock() const
	{
		return m_recentUnlock;
	}

	/// @brief 最近のアンロック情報をクリアする
	void clearRecent()
	{
		m_recentUnlock.reset();
	}

private:
	/// @brief 組み込み実績を登録する
	void defineBuiltins()
	{
		m_achievements = {
			{"first_formula", "First Formula", "Submit your first formula", false},
			{"explorer_10", "Explorer", "Visit 10 checkpoints", false},
			{"collector_5", "Collector", "Collect 5 items", false},
			{"all_buttons", "Calculator Master", "Unlock all calculator buttons", false},
			{"speedrun", "Speed Runner", "Complete in under 30 minutes", false},
		};
	}

	std::vector<Achievement> m_achievements; ///< 全実績
	std::optional<Achievement> m_recentUnlock; ///< 最近アンロックした実績
};

} // namespace mitiru::gw
