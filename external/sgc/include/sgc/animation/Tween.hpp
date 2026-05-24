#pragma once

/// @file Tween.hpp
/// @brief トゥイーンアニメーション
///
/// ビルダーパターンで設定するトゥイーンとシーケンス。
/// イージング関数との組み合わせで柔軟なアニメーションを実現する。

#include <functional>
#include <vector>

#include "sgc/types/Concepts.hpp"
#include "sgc/math/Interpolation.hpp"

namespace sgc
{

/// @brief トゥイーンアニメーション
/// @tparam T 値の型（FloatingPoint）
///
/// @code
/// sgc::Tweenf tween;
/// tween.from(0.0f).to(100.0f).during(2.0f)
///      .withEasing(sgc::easing::outCubic<float>);
///
/// while (!tween.isFinished()) {
///     float value = tween.step(deltaTime);
///     // value を使用
/// }
/// @endcode
template <FloatingPoint T>
class Tween
{
public:
	/// @brief イージング関数型
	using EasingFunc = std::function<T(T)>;

	/// @brief コールバック関数型
	using UpdateCallback = std::function<void(T)>;
	using CompleteCallback = std::function<void()>;

	/// @brief デフォルトコンストラクタ
	Tween() = default;

	// ── ビルダーAPI ─────────────────────────────────────────

	/// @brief 開始値を設定する
	Tween& from(T startValue) noexcept
	{
		m_start = startValue;
		return *this;
	}

	/// @brief 終了値を設定する
	Tween& to(T endValue) noexcept
	{
		m_end = endValue;
		return *this;
	}

	/// @brief 持続時間を設定する（秒）
	Tween& during(T seconds) noexcept
	{
		m_duration = seconds;
		return *this;
	}

	/// @brief イージング関数を設定する
	Tween& withEasing(EasingFunc func)
	{
		m_easing = std::move(func);
		return *this;
	}

	/// @brief ループ回数を設定する（0 = ループなし、-1 = 無限）
	Tween& setLoopCount(int count) noexcept
	{
		m_loopCount = count;
		return *this;
	}

	/// @brief ヨーヨー（往復）モードを設定する
	Tween& setYoyo(bool enabled) noexcept
	{
		m_yoyo = enabled;
		return *this;
	}

	/// @brief 更新コールバックを設定する
	Tween& onUpdate(UpdateCallback callback)
	{
		m_onUpdate = std::move(callback);
		return *this;
	}

	/// @brief 完了コールバックを設定する
	Tween& onComplete(CompleteCallback callback)
	{
		m_onComplete = std::move(callback);
		return *this;
	}

	// ── 制御 ────────────────────────────────────────────────

	/// @brief 時間を進めて現在値を返す
	/// @param deltaTime 経過時間（秒）
	/// @return 現在のトゥイーン値
	T step(T deltaTime)
	{
		if (m_finished || m_paused || m_duration <= T{0}) return currentValue();

		m_elapsed += deltaTime;

		if (m_elapsed >= m_duration)
		{
			if (m_loopCount == 0)
			{
				m_elapsed = m_duration;
				m_finished = true;
				const T val = currentValue();
				if (m_onUpdate) m_onUpdate(val);
				if (m_onComplete) m_onComplete();
				return val;
			}

			// ループ処理
			m_elapsed -= m_duration;
			if (m_loopCount > 0) --m_loopCount;
			if (m_yoyo) m_reverse = !m_reverse;
		}

		const T val = currentValue();
		if (m_onUpdate) m_onUpdate(val);
		return val;
	}

	/// @brief 直接進行度を指定する
	/// @param t 進行度 [0, 1]
	void seek(T t)
	{
		if (t < T{0}) t = T{0};
		if (t > T{1}) t = T{1};
		m_elapsed = t * m_duration;
		m_finished = (t >= T{1} && m_loopCount == 0);
	}

	/// @brief 一時停止する
	void pause() noexcept { m_paused = true; }

	/// @brief 再開する
	void resume() noexcept { m_paused = false; }

	/// @brief リセットする
	void reset() noexcept
	{
		m_elapsed = T{0};
		m_finished = false;
		m_paused = false;
		m_reverse = false;
	}

	/// @brief 完了したかどうかを返す
	[[nodiscard]] bool isFinished() const noexcept { return m_finished; }

	/// @brief 一時停止中かどうかを返す
	[[nodiscard]] bool isPaused() const noexcept { return m_paused; }

	/// @brief 現在の進行度 [0, 1] を返す
	[[nodiscard]] T progress() const noexcept
	{
		if (m_duration <= T{0}) return T{1};
		T t = m_elapsed / m_duration;
		if (t > T{1}) t = T{1};
		return t;
	}

private:
	/// @brief 現在値を計算する
	[[nodiscard]] T currentValue() const noexcept
	{
		T t = progress();
		if (m_reverse) t = T{1} - t;
		if (m_easing) t = m_easing(t);
		return m_start + (m_end - m_start) * t;
	}

	T m_start{};
	T m_end{};
	T m_duration{T{1}};
	T m_elapsed{};
	EasingFunc m_easing;
	UpdateCallback m_onUpdate;
	CompleteCallback m_onComplete;
	int m_loopCount{0};
	bool m_yoyo{false};
	bool m_paused{false};
	bool m_finished{false};
	bool m_reverse{false};
};

/// @brief トゥイーンシーケンス（複数Tweenの連結実行）
/// @tparam T 値の型
template <FloatingPoint T>
class TweenSequence
{
public:
	/// @brief トゥイーンを追加する
	TweenSequence& add(Tween<T> tween)
	{
		m_tweens.push_back(std::move(tween));
		return *this;
	}

	/// @brief 時間を進めて現在値を返す
	T step(T deltaTime)
	{
		if (m_currentIndex >= m_tweens.size()) return T{0};

		const T val = m_tweens[m_currentIndex].step(deltaTime);

		if (m_tweens[m_currentIndex].isFinished())
		{
			++m_currentIndex;
		}

		return val;
	}

	/// @brief すべてのトゥイーンが完了したか
	[[nodiscard]] bool isFinished() const noexcept
	{
		return m_currentIndex >= m_tweens.size();
	}

	/// @brief リセットする
	void reset()
	{
		m_currentIndex = 0;
		for (auto& t : m_tweens)
		{
			t.reset();
		}
	}

private:
	std::vector<Tween<T>> m_tweens;
	std::size_t m_currentIndex{0};
};

// ── エイリアス ──────────────────────────────────────────────────

using Tweenf = Tween<float>;    ///< float版 Tween
using Tweend = Tween<double>;   ///< double版 Tween

} // namespace sgc
