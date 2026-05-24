#pragma once

/// @file AnimationStateMachine.hpp
/// @brief スプライトアニメーション・ステートマシン
///
/// 状態ベースのアニメーション遷移を管理する。
/// 各状態にSpriteAnimationを紐づけ、条件付き遷移を自動評価する。
///
/// @code
/// sgc::asset::SpriteSheet sheet;
/// sheet.addFrame("f0", {0, 0, 32, 32});
/// sheet.addFrame("f1", {32, 0, 32, 32});
///
/// sgc::asset::SpriteAnimation idleAnim;
/// idleAnim.name = "idle";
/// idleAnim.frameIndices = {0, 1};
/// idleAnim.frameDuration = 0.2f;
///
/// sgc::animation::AnimationStateMachine sm(sheet);
/// sm.addState({"idle", idleAnim, true, true});
/// sm.setInitialState("idle");
/// sm.update(0.016f);
/// @endcode

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "sgc/animation/AnimationState.hpp"
#include "sgc/asset/SpriteSheet.hpp"

namespace sgc::animation
{

/// @brief アニメーション・ステートマシン
///
/// 複数のAnimationStateを保持し、AnimationTransitionの条件に基づいて
/// 自動的に状態遷移を行う。内部にSpriteAnimatorを持ち、
/// フレーム再生を管理する。
class AnimationStateMachine
{
public:
	/// @brief コンストラクタ
	/// @param sheet スプライトシートへの参照
	explicit AnimationStateMachine(const asset::SpriteSheet& sheet)
		: m_animator(sheet)
	{
	}

	/// @brief 状態を追加する
	/// @param state 追加するアニメーション状態
	void addState(AnimationState state)
	{
		const std::string name = state.name;
		m_states.emplace(name, std::move(state));
	}

	/// @brief 遷移を追加する
	/// @param transition 追加する遷移定義
	void addTransition(AnimationTransition transition)
	{
		m_transitions.push_back(std::move(transition));
	}

	/// @brief 初期状態を設定する
	/// @param name 初期状態の名前
	void setInitialState(const std::string& name)
	{
		m_currentState = name;
		const auto it = m_states.find(name);
		if (it != m_states.end())
		{
			auto anim = it->second.animation;
			anim.loop = it->second.looping;
			m_animator.play(anim);
		}
	}

	/// @brief ステートマシンを更新する
	/// @param dt 経過時間（秒）
	void update(float dt)
	{
		// 遷移を優先度順に評価する
		evaluateTransitions();

		// ブレンド中の場合はブレンドを進める
		if (m_isTransitioning)
		{
			m_transitionElapsed += dt;
			if (m_transitionElapsed >= m_transitionDuration)
			{
				m_isTransitioning = false;
				m_transitionElapsed = 0.0f;
				m_transitionDuration = 0.0f;
			}
		}

		// アニメーターを更新する
		m_animator.update(dt);
	}

	/// @brief 条件を無視して強制的に状態を遷移させる
	/// @param name 遷移先の状態名
	void forceState(const std::string& name)
	{
		switchToState(name, 0.0f);
	}

	/// @brief 現在の状態名を取得する
	/// @return 現在の状態名
	[[nodiscard]] const std::string& currentStateName() const noexcept
	{
		return m_currentState;
	}

	/// @brief 現在のフレームを取得する
	/// @return 現在表示すべきスプライトフレーム
	[[nodiscard]] const asset::SpriteFrame& currentFrame() const
	{
		return m_animator.currentFrame();
	}

	/// @brief 遷移中かどうかを取得する
	/// @return 遷移中ならtrue
	[[nodiscard]] bool isTransitioning() const noexcept
	{
		return m_isTransitioning;
	}

	/// @brief 遷移の進行度を取得する
	/// @return 0.0〜1.0の遷移進行度
	[[nodiscard]] float transitionProgress() const noexcept
	{
		if (!m_isTransitioning || m_transitionDuration <= 0.0f)
		{
			return 0.0f;
		}
		return m_transitionElapsed / m_transitionDuration;
	}

	/// @brief 登録された状態数を取得する
	/// @return 状態数
	[[nodiscard]] std::size_t stateCount() const noexcept
	{
		return m_states.size();
	}

private:
	/// @brief 遷移条件を評価し、該当する場合は状態遷移する
	void evaluateTransitions()
	{
		if (m_currentState.empty())
		{
			return;
		}

		// 現在の状態が割り込み不可の場合はスキップする
		const auto currentIt = m_states.find(m_currentState);
		if (currentIt != m_states.end() && !currentIt->second.interruptible)
		{
			// アニメーション完了チェック
			if (!m_animator.isFinished())
			{
				return;
			}
		}

		// 優先度順にソートされた遷移を評価する
		const AnimationTransition* bestTransition = nullptr;

		for (const auto& trans : m_transitions)
		{
			// fromStateが空の場合はワイルドカード（任意の状態からマッチ）
			if (!trans.fromState.empty() && trans.fromState != m_currentState)
			{
				continue;
			}

			// 既に遷移先と同じ状態の場合はスキップする
			if (trans.toState == m_currentState)
			{
				continue;
			}

			// 条件を評価する
			if (trans.condition && trans.condition())
			{
				if (!bestTransition || trans.priority > bestTransition->priority)
				{
					bestTransition = &trans;
				}
			}
		}

		if (bestTransition)
		{
			switchToState(bestTransition->toState, bestTransition->blendDuration);
		}
	}

	/// @brief 指定した状態に遷移する
	/// @param name 遷移先の状態名
	/// @param blendDuration ブレンド時間
	void switchToState(const std::string& name, float blendDuration)
	{
		const auto it = m_states.find(name);
		if (it == m_states.end())
		{
			return;
		}

		m_currentState = name;
		auto anim = it->second.animation;
		anim.loop = it->second.looping;
		m_animator.play(anim);

		if (blendDuration > 0.0f)
		{
			m_isTransitioning = true;
			m_transitionDuration = blendDuration;
			m_transitionElapsed = 0.0f;
		}
		else
		{
			m_isTransitioning = false;
			m_transitionDuration = 0.0f;
			m_transitionElapsed = 0.0f;
		}
	}

	std::unordered_map<std::string, AnimationState> m_states;  ///< 状態マップ
	std::vector<AnimationTransition> m_transitions;             ///< 遷移リスト
	std::string m_currentState;                                 ///< 現在の状態名
	asset::SpriteAnimator m_animator;                           ///< アニメーション再生器

	bool m_isTransitioning = false;     ///< 遷移中フラグ
	float m_transitionDuration = 0.0f;  ///< 遷移時間
	float m_transitionElapsed = 0.0f;   ///< 遷移経過時間
};

} // namespace sgc::animation
