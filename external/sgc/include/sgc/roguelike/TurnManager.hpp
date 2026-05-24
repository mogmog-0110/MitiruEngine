#pragma once

/// @file TurnManager.hpp
/// @brief ターン管理システム
///
/// ローグライクゲームのターン制御を提供する。
/// スピードベースのイニシアチブシステムにより、
/// 速いアクターがより頻繁に行動する。
///
/// @code
/// using namespace sgc::roguelike;
/// TurnManager manager;
/// manager.addActor(0, 100);  // プレイヤー: スピード100
/// manager.addActor(1, 50);   // 敵: スピード50
/// manager.advance();
/// auto id = manager.currentActorId();
/// @endcode

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sgc::roguelike
{

/// @brief ターンフェーズ
enum class TurnPhase : uint8_t
{
	PlayerInput,   ///< プレイヤー入力待ち
	PlayerAction,  ///< プレイヤーアクション実行中
	EnemyAction,   ///< 敵アクション実行中
	EndOfTurn      ///< ターン終了処理
};

/// @brief ターンアクション種別
enum class TurnAction : uint8_t
{
	Move,     ///< 移動
	Attack,   ///< 攻撃
	Wait,     ///< 待機
	UseItem   ///< アイテム使用
};

/// @brief アクター情報（内部用）
struct TurnActor
{
	int id = 0;              ///< アクターID
	int speed = 100;         ///< スピード値（高いほど行動頻度が高い）
	int energy = 0;          ///< 蓄積エネルギー
	bool isPlayer = false;   ///< プレイヤーかどうか
};

/// @brief ターン管理クラス
///
/// スピードベースのイニシアチブを管理する。
/// 各アクターはスピード値に応じたエネルギーを蓄積し、
/// 閾値に達したアクターから順に行動する。
class TurnManager
{
public:
	/// @brief アクション実行に必要なエネルギー閾値
	static constexpr int ENERGY_THRESHOLD = 100;

	/// @brief アクターを追加する
	/// @param actorId アクターID
	/// @param speed スピード値
	/// @param isPlayer プレイヤーかどうか
	void addActor(int actorId, int speed, bool isPlayer = false)
	{
		TurnActor actor;
		actor.id = actorId;
		actor.speed = speed;
		actor.energy = 0;
		actor.isPlayer = isPlayer;
		m_actors.push_back(actor);
	}

	/// @brief アクターを削除する
	/// @param actorId 削除するアクターID
	/// @return 削除成功ならtrue
	bool removeActor(int actorId)
	{
		const auto it = std::remove_if(m_actors.begin(), m_actors.end(),
			[actorId](const TurnActor& a) { return a.id == actorId; });

		if (it == m_actors.end())
		{
			return false;
		}

		m_actors.erase(it, m_actors.end());

		// 現在のアクターが削除された場合のリセット
		if (m_currentIndex >= static_cast<int>(m_actors.size()))
		{
			m_currentIndex = 0;
		}

		return true;
	}

	/// @brief ターンを進行する
	///
	/// 全アクターにスピード値分のエネルギーを加算し、
	/// 閾値に達したアクターの中から最もエネルギーの高いものを
	/// 現在のアクターに設定する。
	///
	/// @return 行動可能なアクターが見つかったらtrue
	bool advance()
	{
		if (m_actors.empty())
		{
			return false;
		}

		// エネルギー加算
		for (auto& actor : m_actors)
		{
			actor.energy += actor.speed;
		}

		// 最もエネルギーの高いアクターを選択
		int bestIdx = -1;
		int bestEnergy = ENERGY_THRESHOLD - 1;

		for (int i = 0; i < static_cast<int>(m_actors.size()); ++i)
		{
			if (m_actors[static_cast<std::size_t>(i)].energy > bestEnergy)
			{
				bestEnergy = m_actors[static_cast<std::size_t>(i)].energy;
				bestIdx = i;
			}
		}

		if (bestIdx < 0)
		{
			return false;
		}

		m_currentIndex = bestIdx;
		m_phase = m_actors[static_cast<std::size_t>(bestIdx)].isPlayer
			? TurnPhase::PlayerInput
			: TurnPhase::EnemyAction;

		return true;
	}

	/// @brief 現在のアクターのアクションを消費する
	///
	/// 現在のアクターのエネルギーを閾値分減算する。
	/// advance()で次のアクターに進む前に呼ぶ。
	void consumeAction()
	{
		if (m_currentIndex >= 0 &&
			m_currentIndex < static_cast<int>(m_actors.size()))
		{
			m_actors[static_cast<std::size_t>(m_currentIndex)].energy -= ENERGY_THRESHOLD;
		}
		m_phase = TurnPhase::EndOfTurn;
	}

	/// @brief 現在のフェーズを取得する
	/// @return ターンフェーズ
	[[nodiscard]] TurnPhase currentPhase() const noexcept
	{
		return m_phase;
	}

	/// @brief 現在のアクターIDを取得する
	/// @return アクターID（アクターがない場合は-1）
	[[nodiscard]] int currentActorId() const noexcept
	{
		if (m_currentIndex < 0 ||
			m_currentIndex >= static_cast<int>(m_actors.size()))
		{
			return -1;
		}
		return m_actors[static_cast<std::size_t>(m_currentIndex)].id;
	}

	/// @brief 現在のアクターがプレイヤーか判定する
	/// @return プレイヤーならtrue
	[[nodiscard]] bool isCurrentPlayer() const noexcept
	{
		if (m_currentIndex < 0 ||
			m_currentIndex >= static_cast<int>(m_actors.size()))
		{
			return false;
		}
		return m_actors[static_cast<std::size_t>(m_currentIndex)].isPlayer;
	}

	/// @brief アクター数を取得する
	/// @return 登録されているアクター数
	[[nodiscard]] std::size_t actorCount() const noexcept
	{
		return m_actors.size();
	}

	/// @brief フェーズを設定する
	/// @param phase 設定するフェーズ
	void setPhase(TurnPhase phase) noexcept
	{
		m_phase = phase;
	}

private:
	std::vector<TurnActor> m_actors;       ///< アクター一覧
	int m_currentIndex = -1;               ///< 現在のアクターインデックス
	TurnPhase m_phase = TurnPhase::EndOfTurn;  ///< 現在のフェーズ
};

} // namespace sgc::roguelike
