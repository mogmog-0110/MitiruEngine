#pragma once

/// @file AchievementSystem.hpp
/// @brief 実績・トロフィーシステム
/// @details 条件式ベースの実績管理、アンロック通知トースト、
///          FlagManagerとの自動連動、シークレット実績、直列化をサポートする。
///
/// @code
/// mitiru::vn::AchievementSystem achievements;
/// achievements.registerAchievement({
///     "first_blood", "First Blood", "Complete the first battle",
///     "icon_first_blood", false, "", false, "battle_won == true"
/// });
///
/// // FlagManagerのコールバックと連携
/// flags.onChange([&](const std::string&, const FlagValue&, const FlagValue&) {
///     achievements.evaluateAll(flags);
/// });
///
/// achievements.update(dt); // トースト更新
/// auto toast = achievements.currentToast();
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mitiru/data/Json.hpp>
#include <mitiru/vn/FlagManager.hpp>

namespace mitiru::vn
{

// ── 実績定義 ────────────────────────────────────────────────────

/// @brief 実績の1項目
struct Achievement
{
	std::string id;               ///< 実績識別子
	std::string title;            ///< 表示タイトル
	std::string description;      ///< 説明文
	std::string iconId;           ///< アイコン識別子
	bool isUnlocked = false;      ///< アンロック済みか
	std::string unlockDate;       ///< アンロック日時（文字列）
	bool isSecret = false;        ///< シークレット実績か
	std::string unlockCondition;  ///< アンロック条件（FlagManager式）
};

// ── トースト通知 ────────────────────────────────────────────────

/// @brief トースト通知の外観・タイミング設定
/// @details setToastAppearance()で上書き可能。新規トーストに適用される。
struct AchievementToastAppearance
{
	float positionX       = -1.0f;  ///< トーストX座標（-1 = 自動: 右上配置）
	float positionY       = 20.0f;  ///< トーストY座標
	float width           = 300.0f; ///< トースト幅
	float height          = 60.0f;  ///< トースト高さ
	float slideInDuration = 0.4f;   ///< スライドイン秒数
	float displayDuration = 3.0f;   ///< 表示秒数
	float fadeOutDuration = 0.5f;   ///< フェードアウト秒数
	float titleFontSize   = 12.0f;  ///< タイトルフォントサイズ
	float descFontSize    = 10.0f;  ///< 説明フォントサイズ
	float iconSize        = 40.0f;  ///< アイコンサイズ
	float padding         = 10.0f;  ///< 内側余白
};

/// @brief トースト通知のアニメーション段階
enum class ToastPhase : std::uint8_t
{
	SlideIn,   ///< スライドイン
	Display,   ///< 表示中
	FadeOut,   ///< フェードアウト
	Done,      ///< 完了
};

/// @brief アンロック通知トーストの状態
struct AchievementToast
{
	std::string achievementId;     ///< 対象実績ID
	std::string title;             ///< 表示タイトル
	std::string description;       ///< 説明文
	std::string iconId;            ///< アイコンID
	ToastPhase phase = ToastPhase::SlideIn; ///< 現在のフェーズ
	float elapsed = 0.0f;          ///< 現フェーズ内の経過時間
	float slideInDuration = 0.4f;  ///< スライドイン秒数
	float displayDuration = 3.0f;  ///< 表示秒数
	float fadeOutDuration = 0.5f;  ///< フェードアウト秒数
	float positionX       = -1.0f; ///< 表示X座標（-1 = 自動）
	float positionY       = 20.0f; ///< 表示Y座標
	float width           = 300.0f;///< トースト幅
	float height          = 60.0f; ///< トースト高さ
	float titleFontSize   = 12.0f; ///< タイトルフォントサイズ
	float descFontSize    = 10.0f; ///< 説明フォントサイズ
	float iconSize        = 40.0f; ///< アイコンサイズ
	float padding         = 10.0f; ///< 内側余白

	/// @brief トーストが完了したか
	[[nodiscard]] bool isDone() const noexcept
	{
		return phase == ToastPhase::Done;
	}

	/// @brief 表示のアルファ値 [0,1]
	[[nodiscard]] float alpha() const noexcept
	{
		switch (phase)
		{
		case ToastPhase::SlideIn:
			return std::min(1.0f, elapsed / std::max(0.001f, slideInDuration));
		case ToastPhase::Display:
			return 1.0f;
		case ToastPhase::FadeOut:
			return std::max(0.0f, 1.0f - elapsed / std::max(0.001f, fadeOutDuration));
		case ToastPhase::Done:
			return 0.0f;
		}
		return 0.0f;
	}

	/// @brief スライドインのオフセット比率 [0,1]（0=完全表示、1=画面外）
	[[nodiscard]] float slideOffset() const noexcept
	{
		if (phase != ToastPhase::SlideIn) { return 0.0f; }
		return std::max(0.0f, 1.0f - elapsed / std::max(0.001f, slideInDuration));
	}

	/// @brief トーストを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		elapsed += dt;
		switch (phase)
		{
		case ToastPhase::SlideIn:
			if (elapsed >= slideInDuration)
			{
				phase = ToastPhase::Display;
				elapsed = 0.0f;
			}
			break;
		case ToastPhase::Display:
			if (elapsed >= displayDuration)
			{
				phase = ToastPhase::FadeOut;
				elapsed = 0.0f;
			}
			break;
		case ToastPhase::FadeOut:
			if (elapsed >= fadeOutDuration)
			{
				phase = ToastPhase::Done;
				elapsed = 0.0f;
			}
			break;
		case ToastPhase::Done:
			break;
		}
	}
};

// ── 実績システム ────────────────────────────────────────────────

/// @brief 実績・トロフィー管理クラス
/// @details 実績の登録、条件評価、アンロック通知、直列化を統合管理する。
class AchievementSystem
{
	std::vector<Achievement> m_achievements;
	std::unordered_map<std::string, std::size_t> m_idIndex;
	std::vector<AchievementToast> m_toastQueue;
	std::optional<AchievementToast> m_activeToast;
	std::function<void(const Achievement&)> m_onUnlockCallback;
	AchievementToastAppearance m_toastAppearance; ///< トースト外観設定
	std::string m_currentDate; ///< 現在日時文字列（外部からset）

public:
	// ── 登録 ────────────────────────────────────────────────

	/// @brief 実績を登録する
	/// @param achievement 実績定義
	void registerAchievement(Achievement achievement)
	{
		const auto id = achievement.id;
		m_idIndex[id] = m_achievements.size();
		m_achievements.push_back(std::move(achievement));
	}

	/// @brief 実績を一括登録する
	/// @param achievements 実績群
	void registerAchievements(std::vector<Achievement> achievements)
	{
		for (auto& a : achievements)
		{
			registerAchievement(std::move(a));
		}
	}

	// ── アンロック ──────────────────────────────────────────

	/// @brief 実績をアンロックする
	/// @param id 実績ID
	/// @return 新たにアンロックされたならtrue
	bool unlock(const std::string& id)
	{
		auto* achievement = findAchievement(id);
		if (!achievement || achievement->isUnlocked) { return false; }

		achievement->isUnlocked = true;
		achievement->unlockDate = m_currentDate;

		// トースト通知をキューに追加（外観設定を反映）
		AchievementToast toast;
		toast.achievementId   = achievement->id;
		toast.title           = achievement->title;
		toast.description     = achievement->description;
		toast.iconId          = achievement->iconId;
		toast.slideInDuration = m_toastAppearance.slideInDuration;
		toast.displayDuration = m_toastAppearance.displayDuration;
		toast.fadeOutDuration = m_toastAppearance.fadeOutDuration;
		toast.positionX       = m_toastAppearance.positionX;
		toast.positionY       = m_toastAppearance.positionY;
		toast.width           = m_toastAppearance.width;
		toast.height          = m_toastAppearance.height;
		toast.titleFontSize   = m_toastAppearance.titleFontSize;
		toast.descFontSize    = m_toastAppearance.descFontSize;
		toast.iconSize        = m_toastAppearance.iconSize;
		toast.padding         = m_toastAppearance.padding;
		m_toastQueue.push_back(std::move(toast));

		if (m_onUnlockCallback)
		{
			m_onUnlockCallback(*achievement);
		}

		return true;
	}

	/// @brief 全実績をFlagManagerの状態で評価し、条件を満たすものをアンロックする
	/// @param flags FlagManager
	void evaluateAll(const FlagManager& flags)
	{
		for (auto& achievement : m_achievements)
		{
			if (achievement.isUnlocked) { continue; }
			if (achievement.unlockCondition.empty()) { continue; }
			if (flags.evaluate(achievement.unlockCondition))
			{
				unlock(achievement.id);
			}
		}
	}

	// ── 参照 ────────────────────────────────────────────────

	/// @brief 全実績を取得する
	[[nodiscard]] const std::vector<Achievement>& achievements() const noexcept
	{
		return m_achievements;
	}

	/// @brief IDで実績を取得する
	/// @param id 実績ID
	/// @return 実績（存在しなければnullopt）
	[[nodiscard]] std::optional<Achievement> getAchievement(const std::string& id) const
	{
		const auto* a = findAchievement(id);
		if (!a) { return std::nullopt; }
		return *a;
	}

	/// @brief 表示用の実績リストを取得する（シークレット未アンロックは隠す）
	[[nodiscard]] std::vector<Achievement> displayList() const
	{
		std::vector<Achievement> result;
		result.reserve(m_achievements.size());
		for (const auto& a : m_achievements)
		{
			if (a.isSecret && !a.isUnlocked)
			{
				// シークレット実績は隠す
				Achievement hidden;
				hidden.id = a.id;
				hidden.title = "???";
				hidden.description = "Secret achievement";
				hidden.iconId = "";
				hidden.isUnlocked = false;
				hidden.isSecret = true;
				result.push_back(std::move(hidden));
			}
			else
			{
				result.push_back(a);
			}
		}
		return result;
	}

	/// @brief 実績がアンロック済みか確認する
	[[nodiscard]] bool isUnlocked(const std::string& id) const
	{
		const auto* a = findAchievement(id);
		return a && a->isUnlocked;
	}

	// ── 統計 ────────────────────────────────────────────────

	/// @brief 総実績数
	[[nodiscard]] std::size_t totalCount() const noexcept
	{
		return m_achievements.size();
	}

	/// @brief アンロック済み数
	[[nodiscard]] std::size_t unlockedCount() const noexcept
	{
		return static_cast<std::size_t>(
			std::count_if(m_achievements.begin(), m_achievements.end(),
				[](const Achievement& a) { return a.isUnlocked; }));
	}

	/// @brief 完了率 [0,100]
	[[nodiscard]] float completionPercentage() const noexcept
	{
		if (m_achievements.empty()) { return 100.0f; }
		return static_cast<float>(unlockedCount()) / static_cast<float>(m_achievements.size()) * 100.0f;
	}

	// ── トースト通知 ────────────────────────────────────────

	/// @brief トーストを更新する（毎フレーム呼ぶ）
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		// アクティブなトーストを更新
		if (m_activeToast.has_value())
		{
			m_activeToast->update(dt);
			if (m_activeToast->isDone())
			{
				m_activeToast.reset();
			}
		}

		// キューから次のトーストを取り出す
		if (!m_activeToast.has_value() && !m_toastQueue.empty())
		{
			m_activeToast = std::move(m_toastQueue.front());
			m_toastQueue.erase(m_toastQueue.begin());
		}
	}

	/// @brief 現在表示中のトーストを取得する
	/// @return トースト（表示中でなければnullopt）
	[[nodiscard]] std::optional<AchievementToast> currentToast() const
	{
		return m_activeToast;
	}

	/// @brief トーストキューが空か
	[[nodiscard]] bool isToastQueueEmpty() const noexcept
	{
		return m_toastQueue.empty() && !m_activeToast.has_value();
	}

	// ── コールバック ────────────────────────────────────────

	/// @brief アンロック時のコールバックを設定する
	/// @param callback コールバック関数
	void setOnUnlockCallback(std::function<void(const Achievement&)> callback)
	{
		m_onUnlockCallback = std::move(callback);
	}

	// ── トースト外観設定 ────────────────────────────────────

	/// @brief トースト通知の外観を取得する
	[[nodiscard]] const AchievementToastAppearance& toastAppearance() const noexcept
	{
		return m_toastAppearance;
	}

	/// @brief トースト通知の外観を設定する（以降のトーストに適用）
	/// @param appearance 外観設定
	void setToastAppearance(const AchievementToastAppearance& appearance) noexcept
	{
		m_toastAppearance = appearance;
	}

	// ── 日時設定 ────────────────────────────────────────────

	/// @brief 現在日時を設定する（アンロック日時記録用）
	/// @param date 日時文字列
	void setCurrentDate(const std::string& date)
	{
		m_currentDate = date;
	}

	// ── 直列化 ──────────────────────────────────────────────

	/// @brief アンロック状態をJSON文字列に出力する
	[[nodiscard]] std::string toJson() const
	{
		mitiru::data::Json j;
		mitiru::data::Json arr = mitiru::data::Json::array();
		for (const auto& a : m_achievements)
		{
			if (!a.isUnlocked) { continue; }
			mitiru::data::Json entry;
			entry["id"] = a.id;
			if (!a.unlockDate.empty())
			{
				entry["date"] = a.unlockDate;
			}
			arr.push_back(std::move(entry));
		}
		j["achievements"] = std::move(arr);
		return j.dump();
	}

	/// @brief アンロック状態をJSON文字列から復元する
	/// @param json JSON文字列
	/// @return 成功ならtrue
	bool fromJson(std::string_view json)
	{
		// 全実績のアンロック状態をリセット
		for (auto& a : m_achievements)
		{
			a.isUnlocked = false;
			a.unlockDate.clear();
		}

		auto j = mitiru::data::Json::parse(std::string(json), nullptr, false);
		if (j.is_discarded()) return false;

		if (!j.contains("achievements") || !j["achievements"].is_array())
		{
			return false;
		}

		for (const auto& entry : j["achievements"])
		{
			if (!entry.contains("id") || !entry["id"].is_string()) continue;

			auto id = entry["id"].get<std::string>();
			std::string date;
			if (entry.contains("date") && entry["date"].is_string())
			{
				date = entry["date"].get<std::string>();
			}

			auto* achievement = findAchievement(id);
			if (achievement)
			{
				achievement->isUnlocked = true;
				achievement->unlockDate = date;
			}
		}

		return true;
	}

private:
	[[nodiscard]] Achievement* findAchievement(const std::string& id)
	{
		auto it = m_idIndex.find(id);
		if (it == m_idIndex.end()) { return nullptr; }
		return &m_achievements[it->second];
	}

	[[nodiscard]] const Achievement* findAchievement(const std::string& id) const
	{
		auto it = m_idIndex.find(id);
		if (it == m_idIndex.end()) { return nullptr; }
		return &m_achievements[it->second];
	}
};

} // namespace mitiru::vn
