#pragma once

/// @file UIAnimation.hpp
/// @brief UI要素のアニメーションシステム
/// @details フェード・スライド・スケール・バウンス・パルス等のアニメーションを
///          UINode単位で管理する。複数のアニメーションを同一ノードに並列適用可能。
///          イージング関数はvn::EasingFunctionsを再利用する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <unordered_map>
#include <vector>

#include <mitiru/ui/UINode.hpp>
#include <mitiru/vn/EasingFunctions.hpp>

namespace mitiru::ui
{

// ── アニメーション種別 ────────────────────────────────────────

/// @brief UIアニメーションの種類
enum class UIAnimationType : std::uint8_t
{
	FadeIn,      ///< フェードイン（alpha 0→1）
	FadeOut,     ///< フェードアウト（alpha 1→0）
	SlideLeft,   ///< 左からスライドイン
	SlideRight,  ///< 右からスライドイン
	SlideUp,     ///< 上からスライドイン
	SlideDown,   ///< 下からスライドイン
	ScaleIn,     ///< スケールイン（0→1）
	ScaleOut,    ///< スケールアウト（1→0）
	Bounce,      ///< バウンス（スケール振動）
	Pulse,       ///< パルス（alpha振動）
};

// ── アニメーション定義 ────────────────────────────────────────

/// @brief アニメーションの設定
struct UIAnimation
{
	UIAnimationType type = UIAnimationType::FadeIn;     ///< アニメーション種別
	float duration  = 0.3f;                              ///< 再生時間（秒）
	float delay     = 0.0f;                              ///< 開始遅延（秒）
	vn::EasingType easing = vn::EasingType::EaseOutCubic; ///< イージング関数
	bool loop       = false;                             ///< ループ再生
	bool pingPong   = false;                             ///< 往復再生（loop時のみ有効）
	float slideDistance = 100.0f;                         ///< スライドアニメーションの移動距離
};

// ── トランスフォーム ──────────────────────────────────────────

/// @brief アニメーションによる変換結果
struct UIAnimationTransform
{
	float offsetX  = 0.0f;   ///< X方向オフセット
	float offsetY  = 0.0f;   ///< Y方向オフセット
	float scaleX   = 1.0f;   ///< X方向スケール
	float scaleY   = 1.0f;   ///< Y方向スケール
	float alpha    = 1.0f;   ///< アルファ値
	float rotation = 0.0f;   ///< 回転（ラジアン）

	/// @brief 別のトランスフォームを合成する
	/// @param other 合成するトランスフォーム
	/// @return 合成結果
	[[nodiscard]] UIAnimationTransform compose(const UIAnimationTransform& other) const noexcept
	{
		UIAnimationTransform result;
		result.offsetX  = offsetX + other.offsetX;
		result.offsetY  = offsetY + other.offsetY;
		result.scaleX   = scaleX * other.scaleX;
		result.scaleY   = scaleY * other.scaleY;
		result.alpha    = alpha * other.alpha;
		result.rotation = rotation + other.rotation;
		return result;
	}

	/// @brief アイデンティティ（変換なし）かどうかを判定する
	[[nodiscard]] bool isIdentity() const noexcept
	{
		constexpr float eps = 0.001f;
		return std::abs(offsetX) < eps
			&& std::abs(offsetY) < eps
			&& std::abs(scaleX - 1.0f) < eps
			&& std::abs(scaleY - 1.0f) < eps
			&& std::abs(alpha - 1.0f) < eps
			&& std::abs(rotation) < eps;
	}
};

// ── アニメーションステート ────────────────────────────────────

/// @brief 個別アニメーションの実行状態
struct UIAnimationState
{
	UIAnimation animation;  ///< アニメーション設定
	float elapsed  = 0.0f;  ///< 経過時間（秒）
	float progress = 0.0f;  ///< イージング適用済みの進捗（0.0-1.0）
	bool finished  = false; ///< 完了フラグ
	bool forward   = true;  ///< 再生方向（pingPong用）
};

// ── アニメーター ──────────────────────────────────────────────

/// @brief UIアニメーション管理クラス
/// @details ノードIDごとに複数のアニメーションを並列管理し、
///          update()でフレーム更新、getTransform()で合成済み変換を取得する。
///
/// @code
/// mitiru::ui::UIAnimator animator;
///
/// mitiru::ui::UIAnimation fadeIn;
/// fadeIn.type = mitiru::ui::UIAnimationType::FadeIn;
/// fadeIn.duration = 0.5f;
/// animator.animate(nodeId, fadeIn);
///
/// // ゲームループ内
/// animator.update(deltaTime);
/// auto transform = animator.getTransform(nodeId);
/// @endcode
class UIAnimator
{
	std::unordered_map<UINodeId, std::vector<UIAnimationState>> m_animations;

public:
	/// @brief アニメーションを開始する
	/// @param nodeId 対象ノードのID
	/// @param animation アニメーション設定
	void animate(UINodeId nodeId, const UIAnimation& animation)
	{
		UIAnimationState state;
		state.animation = animation;
		m_animations[nodeId].push_back(state);
	}

	/// @brief 全アクティブアニメーションを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		for (auto& [nodeId, states] : m_animations)
		{
			for (auto& state : states)
			{
				if (state.finished) continue;

				state.elapsed += dt;

				// 遅延中
				if (state.elapsed < state.animation.delay)
				{
					state.progress = 0.0f;
					continue;
				}

				const float activeTime = state.elapsed - state.animation.delay;
				const float duration = std::max(0.001f, state.animation.duration);

				if (state.animation.loop)
				{
					float rawProgress = std::fmod(activeTime, duration) / duration;

					if (state.animation.pingPong)
					{
						const int cycle = static_cast<int>(activeTime / duration);
						state.forward = (cycle % 2) == 0;
						if (!state.forward)
						{
							rawProgress = 1.0f - rawProgress;
						}
					}

					state.progress = vn::Easing::apply(state.animation.easing, rawProgress);
				}
				else
				{
					if (activeTime >= duration)
					{
						state.progress = 1.0f;
						state.finished = true;
					}
					else
					{
						const float rawProgress = activeTime / duration;
						state.progress = vn::Easing::apply(state.animation.easing, rawProgress);
					}
				}
			}
		}

		// 完了したアニメーションをクリーンアップする
		for (auto it = m_animations.begin(); it != m_animations.end(); )
		{
			auto& states = it->second;
			states.erase(
				std::remove_if(states.begin(), states.end(),
					[](const UIAnimationState& s) { return s.finished; }),
				states.end());

			if (states.empty())
			{
				it = m_animations.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	/// @brief ノードの合成済みトランスフォームを取得する
	/// @param nodeId 対象ノードのID
	/// @return 全アクティブアニメーションを合成した変換
	[[nodiscard]] UIAnimationTransform getTransform(UINodeId nodeId) const
	{
		UIAnimationTransform result;

		const auto it = m_animations.find(nodeId);
		if (it == m_animations.end())
		{
			return result;
		}

		for (const auto& state : it->second)
		{
			const auto partial = computeTransform(state);
			result = result.compose(partial);
		}

		return result;
	}

	/// @brief ノードにアクティブなアニメーションがあるか判定する
	/// @param nodeId 対象ノードのID
	/// @return アニメーション実行中ならtrue
	[[nodiscard]] bool isAnimating(UINodeId nodeId) const
	{
		const auto it = m_animations.find(nodeId);
		return it != m_animations.end() && !it->second.empty();
	}

	/// @brief ノードの全アニメーションをキャンセルする
	/// @param nodeId 対象ノードのID
	void cancelAnimation(UINodeId nodeId)
	{
		m_animations.erase(nodeId);
	}

	/// @brief 全アニメーションをキャンセルする
	void cancelAll()
	{
		m_animations.clear();
	}

	/// @brief アクティブなアニメーション総数を取得する
	[[nodiscard]] std::size_t activeCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto& [nodeId, states] : m_animations)
		{
			count += states.size();
		}
		return count;
	}

	/// @brief ノードのアニメーションステート一覧を取得する
	/// @param nodeId 対象ノードのID
	/// @return アニメーションステートのベクタ（未登録時は空）
	[[nodiscard]] const std::vector<UIAnimationState>& getStates(UINodeId nodeId) const
	{
		static const std::vector<UIAnimationState> empty;
		const auto it = m_animations.find(nodeId);
		if (it != m_animations.end())
		{
			return it->second;
		}
		return empty;
	}

private:
	/// @brief アニメーションステートからトランスフォームを計算する
	/// @param state アニメーション実行状態
	/// @return 計算されたトランスフォーム
	[[nodiscard]] static UIAnimationTransform computeTransform(const UIAnimationState& state) noexcept
	{
		UIAnimationTransform transform;
		const float p = state.progress;
		const float dist = state.animation.slideDistance;

		switch (state.animation.type)
		{
		case UIAnimationType::FadeIn:
			transform.alpha = p;
			break;

		case UIAnimationType::FadeOut:
			transform.alpha = 1.0f - p;
			break;

		case UIAnimationType::SlideLeft:
			transform.offsetX = -dist * (1.0f - p);
			break;

		case UIAnimationType::SlideRight:
			transform.offsetX = dist * (1.0f - p);
			break;

		case UIAnimationType::SlideUp:
			transform.offsetY = -dist * (1.0f - p);
			break;

		case UIAnimationType::SlideDown:
			transform.offsetY = dist * (1.0f - p);
			break;

		case UIAnimationType::ScaleIn:
			transform.scaleX = p;
			transform.scaleY = p;
			break;

		case UIAnimationType::ScaleOut:
			transform.scaleX = 1.0f - p;
			transform.scaleY = 1.0f - p;
			break;

		case UIAnimationType::Bounce:
		{
			// スケール振動: 1.0 → 1.2 → 1.0（バウンスイージング的な動き）
			const float bounce = std::sin(p * std::numbers::pi_v<float>) * 0.2f;
			transform.scaleX = 1.0f + bounce;
			transform.scaleY = 1.0f + bounce;
			break;
		}

		case UIAnimationType::Pulse:
		{
			// アルファ振動: 1.0 → 0.5 → 1.0
			const float pulse = std::sin(p * std::numbers::pi_v<float>) * 0.5f;
			transform.alpha = 1.0f - pulse;
			break;
		}
		}

		return transform;
	}
};

} // namespace mitiru::ui
