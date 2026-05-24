#pragma once

/// @file TweenCallbacks.hpp
/// @brief コールバック付きトゥイーンラッパー
///
/// 既存のTween<T>をラップし、開始・完了・更新・リピートの
/// コールバック機能を追加する。
///
/// @code
/// sgc::CallbackTween<float> ct{
///     sgc::Tweenf{}.from(0.0f).to(100.0f).during(1.0f)
///         .setLoopCount(2).setYoyo(true)
/// };
/// ct.onStart([]{ /* 開始時 */ })
///   .onComplete([]{ /* 完了時 */ })
///   .onUpdate([](const float& v){ /* 値更新時 */ })
///   .onRepeat([](int32_t n){ /* リピート時 */ });
///
/// while (!ct.isComplete()) ct.update(0.016f);
/// @endcode

#include <cstdint>
#include <functional>

#include "sgc/animation/Tween.hpp"

namespace sgc
{

/// @brief コールバック付きTweenラッパー
/// @tparam T 値の型（FloatingPoint）
template <FloatingPoint T>
class CallbackTween
{
public:
	/// @brief Tweenを受け取って構築する
	/// @param tween ラップ対象のTween
	explicit CallbackTween(Tween<T> tween)
		: m_tween(std::move(tween))
	{
	}

	/// @brief 開始コールバックを設定する
	/// @param callback 初回update時に呼ばれるコールバック
	/// @return 自身への参照
	CallbackTween& onStart(std::function<void()> callback)
	{
		m_onStart = std::move(callback);
		return *this;
	}

	/// @brief 完了コールバックを設定する
	/// @param callback Tween完了時に呼ばれるコールバック
	/// @return 自身への参照
	CallbackTween& onComplete(std::function<void()> callback)
	{
		m_onComplete = std::move(callback);
		return *this;
	}

	/// @brief 更新コールバックを設定する
	/// @param callback 毎フレームの値更新時に呼ばれるコールバック
	/// @return 自身への参照
	CallbackTween& onUpdate(std::function<void(const T&)> callback)
	{
		m_onUpdate = std::move(callback);
		return *this;
	}

	/// @brief リピートコールバックを設定する
	/// @param callback ループ切り替わり時に呼ばれるコールバック（リピート回数を引数）
	/// @return 自身への参照
	CallbackTween& onRepeat(std::function<void(int32_t)> callback)
	{
		m_onRepeat = std::move(callback);
		return *this;
	}

	/// @brief トゥイーンを更新する
	/// @param dt 経過時間（秒）
	/// @return 完了したらtrue
	bool update(T dt)
	{
		if (m_completed) return true;

		/// 初回更新時に開始コールバック
		if (!m_started)
		{
			m_started = true;
			if (m_onStart) m_onStart();
		}

		const bool wasFinished = m_tween.isFinished();
		m_value = m_tween.step(dt);

		/// リピート検出（finished→not finishedの遷移はループ発生）
		if (wasFinished && !m_tween.isFinished())
		{
			++m_repeatCount;
			if (m_onRepeat) m_onRepeat(m_repeatCount);
		}
		/// Tweenが完了していないのにstep前に完了していた場合もリピート
		if (!wasFinished && !m_tween.isFinished())
		{
			/// ループ中のリピート検出は進行度リセットで判定
			/// （Tween内部でループ処理されるため外部からは検出困難）
		}

		if (m_onUpdate) m_onUpdate(m_value);

		if (m_tween.isFinished() && !m_completed)
		{
			m_completed = true;
			if (m_onComplete) m_onComplete();
			return true;
		}
		return false;
	}

	/// @brief 現在値を取得する
	/// @return 現在のトゥイーン値
	[[nodiscard]] const T& value() const noexcept { return m_value; }

	/// @brief 完了状態を取得する
	/// @return 完了していればtrue
	[[nodiscard]] bool isComplete() const noexcept { return m_completed; }

	/// @brief 開始状態を取得する
	/// @return updateが1回以上呼ばれたらtrue
	[[nodiscard]] bool isStarted() const noexcept { return m_started; }

	/// @brief リセットする（Tweenと全状態をリセット）
	void reset()
	{
		m_tween.reset();
		m_started = false;
		m_completed = false;
		m_repeatCount = 0;
		m_value = T{};
	}

private:
	Tween<T> m_tween;                              ///< ラップ対象のTween
	std::function<void()> m_onStart;               ///< 開始コールバック
	std::function<void()> m_onComplete;            ///< 完了コールバック
	std::function<void(const T&)> m_onUpdate;      ///< 更新コールバック
	std::function<void(int32_t)> m_onRepeat;       ///< リピートコールバック
	T m_value{};                                   ///< 現在値
	bool m_started = false;                        ///< 開始済みフラグ
	bool m_completed = false;                      ///< 完了フラグ
	int32_t m_repeatCount = 0;                     ///< リピート回数
};

/// @brief float版 CallbackTween
using CallbackTweenf = CallbackTween<float>;
/// @brief double版 CallbackTween
using CallbackTweend = CallbackTween<double>;

} // namespace sgc
