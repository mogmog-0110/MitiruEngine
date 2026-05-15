#pragma once

/// @file HealthCheck.hpp
/// @brief エンジンヘルスメトリクス計測
/// @details FPS、更新時間、描画時間等のヘルスメトリクスを計測・公開する。
///          AIエージェントがパフォーマンス状態を観測するために使用する。

#include <chrono>
#include <cstdint>
#include <string>

namespace mitiru::validate
{

/// @brief ヘルスメトリクス
struct HealthMetrics
{
	float fps = 0.0f;             ///< 現在のFPS
	float updateMs = 0.0f;        ///< update処理にかかった時間（ミリ秒）
	float drawMs = 0.0f;          ///< draw処理にかかった時間（ミリ秒）
	int entityCount = 0;          ///< エンティティ数
	float memoryUsageMB = 0.0f;   ///< メモリ使用量（MB）
	int drawCallCount = 0;        ///< 描画コール数

	/// @brief JSON文字列に変換する
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"fps\":" + std::to_string(fps) + ",";
		json += "\"updateMs\":" + std::to_string(updateMs) + ",";
		json += "\"drawMs\":" + std::to_string(drawMs) + ",";
		json += "\"entityCount\":" + std::to_string(entityCount) + ",";
		json += "\"memoryUsageMB\":" + std::to_string(memoryUsageMB) + ",";
		json += "\"drawCallCount\":" + std::to_string(drawCallCount);
		json += "}";
		return json;
	}
};

/// @brief ヘルスチェッカー
/// @details フレーム毎にbeginFrame/endFrameで計測し、メトリクスを公開する。
///
/// @code
/// mitiru::validate::HealthCheck health;
/// health.beginFrame();
/// game.update(dt);
/// health.endFrame();
/// auto m = health.metrics();
/// @endcode
class HealthCheck
{
public:
	using SteadyClock = std::chrono::steady_clock;
	using TimePoint = SteadyClock::time_point;

	/// @brief フレーム開始を記録する
	void beginFrame()
	{
		m_frameStart = SteadyClock::now();
	}

	/// @brief フレーム終了を記録しFPS等を更新する
	void endFrame()
	{
		const auto now = SteadyClock::now();
		const auto elapsed = std::chrono::duration<float, std::milli>(
			now - m_frameStart).count();

		m_metrics.updateMs = elapsed;

		/// FPS計算（フレーム間隔ベース）
		if (m_lastFrameEnd.time_since_epoch().count() > 0)
		{
			const auto frameDuration = std::chrono::duration<float>(
				now - m_lastFrameEnd).count();
			if (frameDuration > 0.0f)
			{
				m_metrics.fps = 1.0f / frameDuration;
			}
		}

		m_lastFrameEnd = now;
	}

	/// @brief 描画開始を記録する
	void beginDraw()
	{
		m_drawStart = SteadyClock::now();
	}

	/// @brief 描画終了を記録する
	void endDraw()
	{
		const auto now = SteadyClock::now();
		m_metrics.drawMs = std::chrono::duration<float, std::milli>(
			now - m_drawStart).count();
	}

	/// @brief エンティティ数を記録する
	/// @param count エンティティ数
	void recordEntityCount(int count) noexcept
	{
		m_metrics.entityCount = count;
	}

	/// @brief 描画コール数を記録する
	/// @param count 描画コール数
	void recordDrawCalls(int count) noexcept
	{
		m_metrics.drawCallCount = count;
	}

	/// @brief メモリ使用量を記録する
	/// @param megabytes メモリ使用量（MB）
	void recordMemoryUsage(float megabytes) noexcept
	{
		m_metrics.memoryUsageMB = megabytes;
	}

	/// @brief 現在のメトリクスを取得する
	/// @return ヘルスメトリクス
	[[nodiscard]] const HealthMetrics& metrics() const noexcept
	{
		return m_metrics;
	}

	/// @brief メトリクスをJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		return m_metrics.toJson();
	}

private:
	HealthMetrics m_metrics;           ///< 現在のメトリクス
	TimePoint m_frameStart;            ///< フレーム開始時刻
	TimePoint m_lastFrameEnd;          ///< 前フレーム終了時刻
	TimePoint m_drawStart;             ///< 描画開始時刻
};

} // namespace mitiru::validate
