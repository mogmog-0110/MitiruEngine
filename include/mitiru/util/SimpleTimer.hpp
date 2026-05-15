#pragma once

#include <algorithm>
#include <stdexcept>

/// @file SimpleTimer.hpp
/// @brief インターバルタイマーとカウントダウンタイマー

namespace mitiru::util
{

/// @brief 一定間隔で繰り返しティックするインターバルタイマー
/// @note 1フレーム内に複数回ティックが発生する場合にも対応する
class IntervalTimer
{
public:
	/// @brief コンストラクタ
	/// @param interval ティック間隔（秒）。正の値であること
	/// @throws std::invalid_argument interval <= 0 の場合
	explicit IntervalTimer(float interval)
		: m_interval(interval)
	{
		if (interval <= 0.0f)
		{
			throw std::invalid_argument("IntervalTimer: interval must be positive");
		}
	}

	/// @brief タイマーを更新する
	/// @param dt 経過時間（秒）
	/// @return このフレームで発生したティック数
	int update(float dt) noexcept
	{
		m_accumulator += dt;
		int ticks = 0;
		while (m_accumulator >= m_interval)
		{
			m_accumulator -= m_interval;
			++ticks;
		}
		return ticks;
	}

	/// @brief 蓄積時間をリセットする
	void reset() noexcept
	{
		m_accumulator = 0.0f;
	}

	/// @brief 現在のティック間隔を取得する
	/// @return ティック間隔（秒）
	float interval() const noexcept
	{
		return m_interval;
	}

	/// @brief ティック間隔を変更する
	/// @param interval 新しいティック間隔（秒）。正の値であること
	/// @throws std::invalid_argument interval <= 0 の場合
	void setInterval(float interval)
	{
		if (interval <= 0.0f)
		{
			throw std::invalid_argument("IntervalTimer::setInterval: interval must be positive");
		}
		m_interval = interval;
	}

private:
	/// @brief ティック間隔（秒）
	float m_interval;

	/// @brief 蓄積された経過時間
	float m_accumulator = 0.0f;
};

/// @brief 指定時間後に完了するカウントダウンタイマー
class CountdownTimer
{
public:
	/// @brief コンストラクタ
	/// @param duration カウントダウン時間（秒）。正の値であること
	/// @throws std::invalid_argument duration <= 0 の場合
	explicit CountdownTimer(float duration)
		: m_duration(duration)
	{
		if (duration <= 0.0f)
		{
			throw std::invalid_argument("CountdownTimer: duration must be positive");
		}
	}

	/// @brief タイマーを更新する
	/// @param dt 経過時間（秒）
	void update(float dt) noexcept
	{
		m_elapsed = std::min(m_elapsed + dt, m_duration);
	}

	/// @brief タイマーが完了したかを返す
	/// @return 経過時間がduration以上ならtrue
	bool isFinished() const noexcept
	{
		return m_elapsed >= m_duration;
	}

	/// @brief 残り時間を返す
	/// @return 残り時間（秒）。0未満にはならない
	float remaining() const noexcept
	{
		return m_duration - m_elapsed;
	}

	/// @brief 進捗率を返す
	/// @return 0.0（開始直後）〜1.0（完了）の範囲
	float progress() const noexcept
	{
		return m_elapsed / m_duration;
	}

	/// @brief 新しいdurationでタイマーをリセットする
	/// @param duration 新しいカウントダウン時間（秒）
	/// @throws std::invalid_argument duration <= 0 の場合
	void reset(float duration)
	{
		if (duration <= 0.0f)
		{
			throw std::invalid_argument("CountdownTimer::reset: duration must be positive");
		}
		m_duration = duration;
		m_elapsed = 0.0f;
	}

	/// @brief 同じdurationでタイマーをリセットする
	void reset() noexcept
	{
		m_elapsed = 0.0f;
	}

private:
	/// @brief カウントダウン時間（秒）
	float m_duration;

	/// @brief 経過時間（秒）
	float m_elapsed = 0.0f;
};

} // namespace mitiru::util
