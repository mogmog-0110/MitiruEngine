#pragma once

/// @file FrameTimer.hpp
/// @brief フレームタイマー
/// @details フレーム間のデルタタイム計測、FPS算出、
///          固定タイムステップのアキュムレータパターンを提供する。

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <numeric>

namespace mitiru
{

/// @brief フレームタイミング・デルタタイム管理クラス
/// @details std::chrono::steady_clockを使用した高精度フレーム計測。
///          スムージング（直近Nフレームの移動平均）と固定タイムステップを提供する。
///
/// @code
/// mitiru::FrameTimer timer;
/// timer.setFixedDeltaTime(1.0f / 60.0f);
/// while (running)
/// {
///     const float dt = timer.tick();
///     while (timer.consumeFixedStep())
///     {
///         physicsUpdate(timer.getFixedDeltaTime());
///     }
///     render(dt);
/// }
/// @endcode
class FrameTimer
{
public:
	/// @brief スムージングに使用するフレーム数のデフォルト値
	static constexpr std::size_t DEFAULT_SMOOTH_FRAMES = 10;

	/// @brief デルタタイム上限のデフォルト値（秒）
	/// @details スパイラルオブデスを防止するためのキャップ値
	static constexpr float DEFAULT_MAX_DELTA = 0.1f;

	/// @brief コンストラクタ
	/// @param smoothFrames スムージングに使用するフレーム数
	/// @param maxDelta デルタタイム上限（秒）
	explicit FrameTimer(
		std::size_t smoothFrames = DEFAULT_SMOOTH_FRAMES,
		float maxDelta = DEFAULT_MAX_DELTA) noexcept
		: m_smoothFrameCount(smoothFrames > 0 ? smoothFrames : 1)
		, m_maxDelta(maxDelta)
	{
		m_deltaSamples.fill(0.0f);
	}

	/// @brief 1フレーム進め、デルタタイムを返す
	/// @return 今フレームのスムージング済みデルタタイム（秒）
	/// @details steady_clockで前回tick()からの経過時間を計測し、
	///          maxDeltaでキャップした後、移動平均でスムージングする。
	///          固定タイムステップのアキュムレータにも加算する。
	[[nodiscard]] float tick() noexcept
	{
		const auto now = SteadyClock::now();

		float rawDelta = 0.0f;
		if (m_frameCount == 0)
		{
			/// 初回フレームはゼロデルタ
			rawDelta = 0.0f;
		}
		else
		{
			const auto duration = std::chrono::duration<float>(now - m_lastTime);
			rawDelta = duration.count();
		}
		m_lastTime = now;

		/// スパイラルオブデス防止：デルタタイムをキャップ
		rawDelta = std::min(rawDelta, m_maxDelta);

		/// サンプルバッファに記録
		const auto idx = m_frameCount % m_smoothFrameCount;
		m_deltaSamples[idx] = rawDelta;
		++m_frameCount;

		/// スムージング済みデルタタイムを計算
		const auto sampleCount = std::min(
			static_cast<std::size_t>(m_frameCount), m_smoothFrameCount);
		float sum = 0.0f;
		for (std::size_t i = 0; i < sampleCount; ++i)
		{
			sum += m_deltaSamples[i];
		}
		m_smoothedDelta = (sampleCount > 0) ? (sum / static_cast<float>(sampleCount)) : 0.0f;

		/// 固定タイムステップのアキュムレータに加算
		m_accumulator += rawDelta;

		return m_smoothedDelta;
	}

	/// @brief 現在のFPSを取得する
	/// @return スムージング済みデルタタイムから算出したFPS
	[[nodiscard]] float getFps() const noexcept
	{
		if (m_smoothedDelta <= 0.0f)
		{
			return 0.0f;
		}
		return 1.0f / m_smoothedDelta;
	}

	/// @brief 累計フレーム数を取得する
	/// @return tick()が呼ばれた回数
	[[nodiscard]] std::uint64_t getFrameCount() const noexcept
	{
		return m_frameCount;
	}

	/// @brief スムージング済みデルタタイムを取得する
	/// @return 直近の移動平均デルタタイム（秒）
	[[nodiscard]] float getSmoothedDelta() const noexcept
	{
		return m_smoothedDelta;
	}

	/// @brief 固定タイムステップのデルタタイムを設定する
	/// @param dt 固定デルタタイム（秒）
	void setFixedDeltaTime(float dt) noexcept
	{
		m_fixedDeltaTime = dt;
	}

	/// @brief 固定タイムステップのデルタタイムを取得する
	/// @return 固定デルタタイム（秒）
	[[nodiscard]] float getFixedDeltaTime() const noexcept
	{
		return m_fixedDeltaTime;
	}

	/// @brief アキュムレータの現在値を取得する
	/// @return アキュムレータ値（秒）
	[[nodiscard]] float getAccumulator() const noexcept
	{
		return m_accumulator;
	}

	/// @brief 固定ステップを1回消費する
	/// @return 消費できた場合 true
	/// @details アキュムレータが固定デルタタイム以上なら減算して true を返す。
	///          物理更新ループで while(consumeFixedStep()) として使用する。
	[[nodiscard]] bool consumeFixedStep() noexcept
	{
		if (m_fixedDeltaTime <= 0.0f)
		{
			return false;
		}
		if (m_accumulator >= m_fixedDeltaTime)
		{
			m_accumulator -= m_fixedDeltaTime;
			return true;
		}
		return false;
	}

	/// @brief デルタタイム上限を設定する
	/// @param maxDelta 最大デルタタイム（秒）
	void setMaxDelta(float maxDelta) noexcept
	{
		m_maxDelta = maxDelta;
	}

	/// @brief デルタタイム上限を取得する
	/// @return 最大デルタタイム（秒）
	[[nodiscard]] float getMaxDelta() const noexcept
	{
		return m_maxDelta;
	}

	/// @brief タイマーをリセットする
	void reset() noexcept
	{
		m_frameCount = 0;
		m_smoothedDelta = 0.0f;
		m_accumulator = 0.0f;
		m_lastTime = SteadyClock::now();
		m_deltaSamples.fill(0.0f);
	}

private:
	using SteadyClock = std::chrono::steady_clock;
	using TimePoint = SteadyClock::time_point;

	/// @brief スムージング用サンプルバッファの最大サイズ
	static constexpr std::size_t MAX_SMOOTH_FRAMES = 64;

	std::size_t m_smoothFrameCount;                      ///< スムージングフレーム数
	float m_maxDelta;                                    ///< デルタタイム上限（秒）
	std::uint64_t m_frameCount = 0;                      ///< 累計フレーム数
	float m_smoothedDelta = 0.0f;                        ///< スムージング済みデルタ
	float m_accumulator = 0.0f;                          ///< 固定ステップ用アキュムレータ
	float m_fixedDeltaTime = 1.0f / 60.0f;               ///< 固定デルタタイム
	TimePoint m_lastTime = SteadyClock::now();            ///< 前回tick時刻
	std::array<float, MAX_SMOOTH_FRAMES> m_deltaSamples{}; ///< デルタタイムサンプル
};

} // namespace mitiru
