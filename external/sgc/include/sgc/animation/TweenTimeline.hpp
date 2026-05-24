#pragma once

/// @file TweenTimeline.hpp
/// @brief パラレルトゥイーンタイムライン
///
/// 複数のTweenを同時に実行し、すべて完了したら終了する。
/// TweenSequence（逐次実行）と組み合わせて複雑なアニメーションを構築できる。

#include <functional>
#include <utility>
#include <vector>

#include "sgc/animation/Tween.hpp"

namespace sgc
{

/// @brief パラレルトゥイーンタイムライン
///
/// 追加されたすべてのTweenを同時に更新し、
/// 全Tweenが完了した時点で終了とみなす。
///
/// @tparam T 値の型（FloatingPoint）
///
/// @code
/// sgc::TweenTimelinef timeline;
/// timeline.add(sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f));
/// timeline.add(sgc::Tweenf{}.from(0.0f).to(200.0f).during(2.0f));
/// timeline.onComplete([]{ /* 全Tween完了時 */ });
///
/// while (!timeline.isFinished())
/// {
///     timeline.step(deltaTime);
/// }
/// @endcode
template <FloatingPoint T>
class TweenTimeline
{
public:
	/// @brief 完了コールバック型
	using CompleteCallback = std::function<void()>;

	/// @brief Tweenを追加する
	/// @param tween 追加するTween
	/// @return 自身への参照（メソッドチェーン用）
	TweenTimeline& add(Tween<T> tween)
	{
		m_tweens.push_back(std::move(tween));
		return *this;
	}

	/// @brief 完了コールバックを設定する
	/// @param callback 全Tween完了時に呼ばれるコールバック
	/// @return 自身への参照（メソッドチェーン用）
	TweenTimeline& onComplete(CompleteCallback callback)
	{
		m_onComplete = std::move(callback);
		return *this;
	}

	/// @brief 全Tweenを同時に更新する
	/// @param deltaTime 経過時間（秒）
	void step(T deltaTime)
	{
		if (m_finished) return;

		bool allDone = true;
		for (auto& tween : m_tweens)
		{
			if (!tween.isFinished())
			{
				tween.step(deltaTime);
				if (!tween.isFinished()) allDone = false;
			}
		}

		if (allDone && !m_tweens.empty())
		{
			m_finished = true;
			if (m_onComplete) m_onComplete();
		}
	}

	/// @brief すべてのTweenが完了したか
	/// @return 全完了ならtrue
	[[nodiscard]] bool isFinished() const noexcept { return m_finished; }

	/// @brief リセットする（全Tweenをリセットし、完了フラグを解除）
	void reset()
	{
		m_finished = false;
		for (auto& t : m_tweens) t.reset();
	}

	/// @brief 登録されたTween数を返す
	/// @return Tween数
	[[nodiscard]] std::size_t size() const noexcept { return m_tweens.size(); }

private:
	std::vector<Tween<T>> m_tweens;  ///< 並列実行するTween群
	CompleteCallback m_onComplete;    ///< 完了コールバック
	bool m_finished{false};           ///< 全完了フラグ
};

// ── エイリアス ──────────────────────────────────────────────────

using TweenTimelinef = TweenTimeline<float>;    ///< float版 TweenTimeline
using TweenTimelined = TweenTimeline<double>;   ///< double版 TweenTimeline

} // namespace sgc
