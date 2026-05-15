#pragma once

/// @file ProfilerOverlay.hpp
/// @brief パフォーマンスプロファイラーオーバーレイ
/// @details フレームごとのプロファイルサンプルを収集し、フレームグラフデータ、
///          FPS履歴、メモリ使用量、システム別タイミングを提供する。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

namespace mitiru::debug
{

/// @brief プロファイルサンプル
/// @details 1つの計測区間の名前、開始時刻、所要時間、深度、表示色を保持する。
struct ProfileSample
{
	std::string name;           ///< サンプル名
	float startTimeMs = 0.0f;   ///< 開始時刻（ミリ秒）
	float durationMs = 0.0f;    ///< 所要時間（ミリ秒）
	int depth = 0;              ///< ネスト深度
	std::uint32_t color = 0xFFFFFFFF; ///< 表示色（RGBA）
};

/// @brief フレームプロファイル
/// @details 1フレーム内の全サンプルとフレーム全体の所要時間を保持する。
struct FrameProfile
{
	std::vector<ProfileSample> samples; ///< サンプルリスト
	float totalFrameMs = 0.0f;          ///< フレーム全体の所要時間（ミリ秒）
	float gpuMs = 0.0f;                 ///< GPU所要時間（ミリ秒）
};

/// @brief メモリ使用量統計
struct MemoryStats
{
	std::uint64_t allocated = 0;  ///< 累計割り当てバイト数
	std::uint64_t freed = 0;      ///< 累計解放バイト数
	std::uint64_t peak = 0;       ///< ピーク使用バイト数
	std::uint64_t current = 0;    ///< 現在の使用バイト数
};

/// @brief システム別タイミング情報
struct SystemTiming
{
	std::string name;           ///< システム名
	float durationMs = 0.0f;    ///< 所要時間（ミリ秒）
};

/// @brief パフォーマンスプロファイラーオーバーレイ
/// @details フレームプロファイルの収集、FPS履歴の管理、メモリ統計の追跡を行う。
///
/// @code
/// mitiru::debug::ProfilerOverlay profiler(120);
/// // 毎フレーム:
/// mitiru::debug::FrameProfile frame;
/// frame.totalFrameMs = 16.6f;
/// frame.samples.push_back({"Update", 0.0f, 5.0f, 0, 0xFF00FF00});
/// profiler.pushFrame(frame);
///
/// float avgFps = profiler.averageFps();
/// auto flameData = profiler.getFlameGraphData();
/// @endcode
class ProfilerOverlay
{
public:
	/// @brief コンストラクタ
	/// @param historySize FPS履歴の最大フレーム数
	explicit ProfilerOverlay(std::size_t historySize = 120) noexcept
		: m_maxHistory(historySize)
	{
	}

	/// @brief フレームプロファイルを追加する
	/// @param frame フレームプロファイル
	void pushFrame(FrameProfile frame)
	{
		/// FPS履歴を更新する
		if (frame.totalFrameMs > 0.0f)
		{
			const float fps = 1000.0f / frame.totalFrameMs;
			m_fpsHistory.push_back(fps);
			if (m_fpsHistory.size() > m_maxHistory)
			{
				m_fpsHistory.erase(m_fpsHistory.begin());
			}
		}

		/// システム別タイミングを更新する
		m_systemTimings.clear();
		for (const auto& sample : frame.samples)
		{
			if (sample.depth == 0)
			{
				m_systemTimings.push_back(SystemTiming{sample.name, sample.durationMs});
			}
		}

		m_latestFrame = std::move(frame);
	}

	/// @brief 最新フレームのフレームグラフデータを取得する
	/// @return 開始時刻順にソートされたサンプルリスト
	[[nodiscard]] std::vector<ProfileSample> getFlameGraphData() const
	{
		auto sorted = m_latestFrame.samples;
		std::sort(sorted.begin(), sorted.end(),
			[](const ProfileSample& a, const ProfileSample& b)
			{
				if (a.depth != b.depth)
				{
					return a.depth < b.depth;
				}
				return a.startTimeMs < b.startTimeMs;
			}
		);
		return sorted;
	}

	/// @brief FPS履歴を取得する
	/// @return FPS値のリスト（古い順）
	[[nodiscard]] const std::vector<float>& fpsHistory() const noexcept
	{
		return m_fpsHistory;
	}

	/// @brief 平均FPSを取得する
	/// @return 履歴内の平均FPS（履歴が空の場合は0.0f）
	[[nodiscard]] float averageFps() const noexcept
	{
		if (m_fpsHistory.empty())
		{
			return 0.0f;
		}
		const float sum = std::accumulate(m_fpsHistory.begin(), m_fpsHistory.end(), 0.0f);
		return sum / static_cast<float>(m_fpsHistory.size());
	}

	/// @brief 最小FPSを取得する
	/// @return 履歴内の最小FPS
	[[nodiscard]] float minFps() const noexcept
	{
		if (m_fpsHistory.empty())
		{
			return 0.0f;
		}
		return *std::min_element(m_fpsHistory.begin(), m_fpsHistory.end());
	}

	/// @brief 最大FPSを取得する
	/// @return 履歴内の最大FPS
	[[nodiscard]] float maxFps() const noexcept
	{
		if (m_fpsHistory.empty())
		{
			return 0.0f;
		}
		return *std::max_element(m_fpsHistory.begin(), m_fpsHistory.end());
	}

	/// @brief メモリ割り当てを記録する
	/// @param bytes 割り当てバイト数
	void recordAllocation(std::uint64_t bytes) noexcept
	{
		m_memory.allocated += bytes;
		m_memory.current += bytes;
		if (m_memory.current > m_memory.peak)
		{
			m_memory.peak = m_memory.current;
		}
	}

	/// @brief メモリ解放を記録する
	/// @param bytes 解放バイト数
	void recordFree(std::uint64_t bytes) noexcept
	{
		m_memory.freed += bytes;
		if (bytes <= m_memory.current)
		{
			m_memory.current -= bytes;
		}
		else
		{
			m_memory.current = 0;
		}
	}

	/// @brief メモリ統計を取得する
	/// @return メモリ使用量統計
	[[nodiscard]] const MemoryStats& memoryStats() const noexcept
	{
		return m_memory;
	}

	/// @brief システム別タイミングを取得する
	/// @return システムタイミングのリスト
	[[nodiscard]] const std::vector<SystemTiming>& systemTimings() const noexcept
	{
		return m_systemTimings;
	}

	/// @brief 最新フレームプロファイルを取得する
	/// @return 最新のフレームプロファイル
	[[nodiscard]] const FrameProfile& latestFrame() const noexcept
	{
		return m_latestFrame;
	}

	/// @brief FPS履歴の最大サイズを取得する
	/// @return 最大フレーム数
	[[nodiscard]] std::size_t maxHistory() const noexcept
	{
		return m_maxHistory;
	}

private:
	std::size_t m_maxHistory;                ///< FPS履歴の最大フレーム数
	std::vector<float> m_fpsHistory;         ///< FPS履歴（循環バッファ）
	FrameProfile m_latestFrame;              ///< 最新フレームプロファイル
	MemoryStats m_memory;                    ///< メモリ統計
	std::vector<SystemTiming> m_systemTimings; ///< システム別タイミング
};

} // namespace mitiru::debug
