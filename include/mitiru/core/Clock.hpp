#pragma once

/// @file Clock.hpp
/// @brief 決定論的クロック
/// @details フレーム番号と経過時間を管理する。
///          決定論的モードでは dt = 1/TPS の固定値を返す。
///          非決定論的モードでは std::chrono による実時間差分を返す。

#include <chrono>
#include <cstdint>

namespace mitiru
{

/// @brief 決定論的/実時間対応のゲームクロック
/// @details エンジン内部で毎フレーム tick() を呼び、デルタタイムを取得する。
class Clock
{
public:
	/// @brief コンストラクタ
	/// @param targetTps 目標TPS（tick/秒）
	/// @param deterministic 決定論的モードで動作するか
	explicit Clock(float targetTps = 60.0f, bool deterministic = true) noexcept
		: m_targetTps(targetTps)
		, m_fixedDt(1.0f / targetTps)
		, m_deterministic(deterministic)
	{
	}

	/// @brief 1フレーム進め、デルタタイムを返す
	/// @return 今フレームのデルタタイム（秒）
	/// @details 決定論的モードでは常に 1/TPS を返す。
	///          非決定論的モードでは前回 tick() からの実経過時間を返す。
	[[nodiscard]] float tick() noexcept
	{
		++m_frameNumber;

		if (m_deterministic)
		{
			m_elapsed += m_fixedDt;
			return m_fixedDt;
		}

		/// 非決定論的モード: 実時間計測
		const auto now = SteadyClock::now();
		if (m_frameNumber == 1)
		{
			/// 初回フレームは固定dtを返す
			m_lastTime = now;
			m_elapsed += m_fixedDt;
			return m_fixedDt;
		}

		const auto duration = std::chrono::duration<float>(now - m_lastTime);
		const float dt = duration.count();
		m_lastTime = now;
		m_elapsed += dt;
		return dt;
	}

	/// @brief 現在のフレーム番号を取得する
	/// @return フレーム番号（0始まり、tick()呼び出し回数）
	[[nodiscard]] std::uint64_t frameNumber() const noexcept
	{
		return m_frameNumber;
	}

	/// @brief 累計経過時間を取得する（秒）
	[[nodiscard]] float elapsed() const noexcept
	{
		return m_elapsed;
	}

	/// @brief 目標TPSを取得する
	[[nodiscard]] float targetTps() const noexcept
	{
		return m_targetTps;
	}

	/// @brief 固定デルタタイムを取得する（1/TPS）
	[[nodiscard]] float fixedDt() const noexcept
	{
		return m_fixedDt;
	}

	/// @brief 決定論的モードかどうか
	[[nodiscard]] bool isDeterministic() const noexcept
	{
		return m_deterministic;
	}

	/// @brief クロックをリセットする
	void reset() noexcept
	{
		m_frameNumber = 0;
		m_elapsed = 0.0f;
		m_lastTime = SteadyClock::now();
	}

private:
	using SteadyClock = std::chrono::steady_clock;
	using TimePoint = SteadyClock::time_point;

	float m_targetTps;                ///< 目標TPS
	float m_fixedDt;                  ///< 固定デルタタイム（1/TPS）
	bool m_deterministic;             ///< 決定論的モードフラグ
	std::uint64_t m_frameNumber = 0;  ///< 現在のフレーム番号
	float m_elapsed = 0.0f;           ///< 累計経過時間（秒）
	TimePoint m_lastTime = SteadyClock::now();  ///< 前回tick時刻
};

} // namespace mitiru
