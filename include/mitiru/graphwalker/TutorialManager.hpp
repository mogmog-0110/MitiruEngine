#pragma once

/// @file TutorialManager.hpp
/// @brief チュートリアル・ヒントシステム
///
/// ゲーム進行に応じたヒントメッセージを順次表示する。
/// 各ステップにはトリガー条件があり、条件を満たすと次に進む。
///
/// @code
/// mitiru::gw::TutorialManager tut;
/// mitiru::gw::SharedData shared;
/// tut.update(shared, playerPos);
/// if (auto hint = tut.getCurrentHint())
/// {
///     renderHint(*hint);
/// }
/// @endcode

#include <optional>
#include <string>
#include <vector>

#include "sgc/math/Vec2.hpp"
#include "mitiru/graphwalker/Common.hpp"

namespace mitiru::gw
{

/// @brief チュートリアルステップ
struct TutorialStep
{
	std::string id;               ///< ステップID
	std::string message;          ///< 表示メッセージ
	std::string triggerCondition; ///< トリガー条件（文字列識別子）
};

/// @brief チュートリアルマネージャー
///
/// 順序付きのステップを管理し、条件に応じて
/// 次のヒントを提示する。dismissで手動スキップも可能。
class TutorialManager
{
public:
	/// @brief デフォルトコンストラクタ（デフォルトステップを登録）
	TutorialManager()
	{
		addDefaultSteps();
	}

	/// @brief ステップを追加する
	/// @param step チュートリアルステップ
	void addStep(TutorialStep step)
	{
		m_steps.push_back(std::move(step));
	}

	/// @brief 状態を更新しトリガー条件をチェックする
	/// @param shared ゲーム共有データ
	/// @param playerPos プレイヤー位置
	void update(const SharedData& shared, sgc::Vec2f playerPos)
	{
		if (isComplete())
		{
			return;
		}

		const auto& step = m_steps[m_currentIndex];
		const auto& cond = step.triggerCondition;

		// 各トリガー条件の評価
		if (cond == "moved")
		{
			// プレイヤーが初期位置から移動した
			if (playerPos.x != 0.0f || playerPos.y != 0.0f)
			{
				advance();
			}
		}
		else if (cond == "formula_submitted")
		{
			// 数式投入は収集アイテム数で間接的に判定
			if (!shared.collectedItems.empty())
			{
				advance();
			}
		}
		// jumped, npc_interact, calculator_opened は外部からdismissCurrent()で通知

		(void)shared; // 将来の拡張用
	}

	/// @brief 現在のヒントメッセージを取得する
	/// @return ヒント文字列（完了済みならnullopt）
	[[nodiscard]] std::optional<std::string> getCurrentHint() const
	{
		if (isComplete())
		{
			return std::nullopt;
		}
		return m_steps[m_currentIndex].message;
	}

	/// @brief 現在のステップを手動でスキップする
	void dismissCurrent()
	{
		if (!isComplete())
		{
			advance();
		}
	}

	/// @brief 全ステップが完了したか
	/// @return 完了済みならtrue
	[[nodiscard]] bool isComplete() const noexcept
	{
		return m_currentIndex >= m_steps.size();
	}

	/// @brief チュートリアルをリセットする
	void reset()
	{
		m_currentIndex = 0;
	}

	/// @brief 現在のステップインデックスを取得する
	/// @return 現在のインデックス
	[[nodiscard]] std::size_t getCurrentIndex() const noexcept
	{
		return m_currentIndex;
	}

	/// @brief 全ステップ数を取得する
	/// @return ステップ数
	[[nodiscard]] std::size_t stepCount() const noexcept
	{
		return m_steps.size();
	}

private:
	/// @brief 次のステップに進む
	void advance()
	{
		if (m_currentIndex < m_steps.size())
		{
			++m_currentIndex;
		}
	}

	/// @brief デフォルトのチュートリアルステップを登録する
	void addDefaultSteps()
	{
		m_steps = {
			{"move",       "Use WASD or arrow keys to move", "moved"},
			{"jump",       "Press SPACE to jump",            "jumped"},
			{"npc",        "Press E near an NPC to talk",    "npc_interact"},
			{"calculator", "Press C to open the calculator", "calculator_opened"},
			{"formula",    "Enter a formula and press =",    "formula_submitted"},
		};
	}

	std::vector<TutorialStep> m_steps; ///< 全ステップ
	std::size_t m_currentIndex{0};     ///< 現在のステップインデックス
};

} // namespace mitiru::gw
