#pragma once

/// @file Timer.hpp
/// @brief ストップウォッチとデルタタイム計測
///
/// ゲームループでのフレーム時間計測に使用する。

#include <chrono>

namespace sgc
{

/// @brief 高精度ストップウォッチ
///
/// 経過時間の計測、一時停止・再開が可能。
///
/// @code
/// sgc::Stopwatch sw;
/// sw.start();
/// // ... 処理 ...
/// float elapsed = sw.elapsedSeconds();
/// @endcode
class Stopwatch
{
public:
	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = Clock::time_point;

	/// @brief ストップウォッチを開始する
	void start() noexcept
	{
		m_startTime = Clock::now();
		m_running = true;
	}

	/// @brief ストップウォッチを停止する
	void stop() noexcept
	{
		if (m_running)
		{
			m_elapsed += Clock::now() - m_startTime;
			m_running = false;
		}
	}

	/// @brief ストップウォッチをリセットする
	void reset() noexcept
	{
		m_elapsed = Clock::duration::zero();
		m_running = false;
	}

	/// @brief リセットして再スタートする
	void restart() noexcept
	{
		reset();
		start();
	}

	/// @brief 経過時間をミリ秒で返す
	[[nodiscard]] double elapsedMilliseconds() const noexcept
	{
		return std::chrono::duration<double, std::milli>(totalElapsed()).count();
	}

	/// @brief 経過時間を秒で返す
	[[nodiscard]] double elapsedSeconds() const noexcept
	{
		return std::chrono::duration<double>(totalElapsed()).count();
	}

	/// @brief 経過時間をfloat秒で返す（ゲームループ向け）
	[[nodiscard]] float elapsedSecondsF() const noexcept
	{
		return std::chrono::duration<float>(totalElapsed()).count();
	}

	/// @brief 計測中かどうかを返す
	[[nodiscard]] bool isRunning() const noexcept { return m_running; }

private:
	[[nodiscard]] Clock::duration totalElapsed() const noexcept
	{
		if (m_running)
		{
			return m_elapsed + (Clock::now() - m_startTime);
		}
		return m_elapsed;
	}

	TimePoint m_startTime{};
	Clock::duration m_elapsed = Clock::duration::zero();
	bool m_running = false;
};

/// @brief フレーム間デルタタイム計測器
///
/// 毎フレーム tick() を呼ぶとデルタタイムが計算される。
/// タイムスケール機能で全体の速度調整が可能。
///
/// @code
/// sgc::DeltaClock clock;
/// clock.setTimeScale(0.5f);  // スローモーション
/// while (running) {
///     float dt = clock.tick();
///     update(dt);
///     render();
/// }
/// @endcode
class DeltaClock
{
public:
	using Clock = std::chrono::high_resolution_clock;

	/// @brief フレームを進め、前フレームからの経過秒数を返す（タイムスケール適用済み）
	/// @return デルタタイム（秒）
	[[nodiscard]] float tick() noexcept
	{
		const auto now = Clock::now();
		const float rawDt = std::chrono::duration<float>(now - m_lastTime).count();
		m_lastTime = now;
		m_lastDt = rawDt * m_timeScale;
		return m_lastDt;
	}

	/// @brief 計測をリセットする（シーン切り替え時等）
	void reset() noexcept
	{
		m_lastTime = Clock::now();
		m_lastDt = 0.0f;
	}

	/// @brief タイムスケールを設定する
	/// @param scale スケール値（1.0 = 通常、0.5 = スロー、2.0 = 倍速）
	void setTimeScale(float scale) noexcept { m_timeScale = scale; }

	/// @brief 現在のタイムスケールを取得する
	[[nodiscard]] float getTimeScale() const noexcept { return m_timeScale; }

	/// @brief 直前のデルタタイムからFPSを算出する
	/// @return FPS値（dtが0の場合は0を返す）
	[[nodiscard]] float getFPS() const noexcept
	{
		if (m_lastDt <= 0.0f) return 0.0f;
		return 1.0f / m_lastDt;
	}

private:
	Clock::time_point m_lastTime = Clock::now();
	float m_timeScale{1.0f};  ///< タイムスケール
	float m_lastDt{0.0f};     ///< 直前のデルタタイム
};

/// @brief カウントダウンタイマー
///
/// 指定した持続時間からカウントダウンし、期限切れを判定する。
///
/// @code
/// sgc::CountdownTimer timer(3.0f);  // 3秒タイマー
/// while (!timer.isExpired()) {
///     timer.update(dt);
///     float pct = timer.progress();
/// }
/// @endcode
class CountdownTimer
{
public:
	/// @brief カウントダウンタイマーを構築する
	/// @param duration 持続時間（秒）
	explicit CountdownTimer(float duration) noexcept
		: m_duration(duration), m_remaining(duration) {}

	/// @brief タイマーを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		m_remaining -= dt;
		if (m_remaining < 0.0f) m_remaining = 0.0f;
	}

	/// @brief 期限切れかどうかを判定する
	[[nodiscard]] bool isExpired() const noexcept { return m_remaining <= 0.0f; }

	/// @brief 残り時間を返す
	[[nodiscard]] float remaining() const noexcept { return m_remaining; }

	/// @brief 進行度を返す（0=開始 → 1=期限切れ）
	[[nodiscard]] float progress() const noexcept
	{
		if (m_duration <= 0.0f) return 1.0f;
		return 1.0f - (m_remaining / m_duration);
	}

	/// @brief タイマーをリスタートする
	void restart() noexcept { m_remaining = m_duration; }

private:
	float m_duration;   ///< 持続時間
	float m_remaining;  ///< 残り時間
};

/// @brief インターバルタイマー
///
/// 一定間隔で繰り返し発火するタイマー。consume()で発火回数を消費する。
///
/// @code
/// sgc::IntervalTimer timer(0.5f);  // 0.5秒ごと
/// timer.update(dt);
/// while (timer.consume()) {
///     // 0.5秒ごとの処理
/// }
/// @endcode
class IntervalTimer
{
public:
	/// @brief インターバルタイマーを構築する
	/// @param interval 発火間隔（秒）
	explicit IntervalTimer(float interval) noexcept
		: m_interval(interval) {}

	/// @brief タイマーを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		m_accumulated += dt;
	}

	/// @brief 発火可能であれば1回分消費してtrueを返す
	/// @return 発火したらtrue
	[[nodiscard]] bool consume() noexcept
	{
		if (m_interval <= 0.0f) return false;
		if (m_accumulated >= m_interval)
		{
			m_accumulated -= m_interval;
			return true;
		}
		return false;
	}

	/// @brief 発火間隔を設定する
	/// @param interval 新しい間隔（秒）
	void setInterval(float interval) noexcept { m_interval = interval; }

	/// @brief 発火間隔を取得する
	[[nodiscard]] float interval() const noexcept { return m_interval; }

private:
	float m_interval;         ///< 発火間隔
	float m_accumulated{0.0f}; ///< 蓄積時間
};

} // namespace sgc
