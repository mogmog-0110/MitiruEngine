#pragma once

/// @file MemoryStats.hpp
/// @brief メモリ使用量統計とスコープ付きトラッカー
///
/// メモリ割り当て/解放を追跡し、統計情報を提供する。
/// ScopedMemoryTrackerでRAIIベースの自動計測が可能。
///
/// @code
/// sgc::debug::MemoryStats stats;
/// {
///     sgc::debug::ScopedMemoryTracker tracker(stats, 1024);
///     // ... 1024バイトを使用するスコープ
/// } // 自動的にfreeが記録される
/// auto report = stats.summary();
/// @endcode

#include <cstddef>
#include <cstdio>
#include <string>

namespace sgc::debug
{

/// @brief メモリ使用量統計
struct MemoryStats
{
	size_t totalAllocated = 0;    ///< 総割当量（バイト）
	size_t totalFreed = 0;        ///< 総解放量（バイト）
	size_t currentUsage = 0;      ///< 現在使用量
	size_t peakUsage = 0;         ///< ピーク使用量
	size_t allocationCount = 0;   ///< 割当回数
	size_t freeCount = 0;         ///< 解放回数

	/// @brief メモリ割り当てを記録する
	/// @param bytes 割り当てサイズ（バイト）
	void recordAllocation(size_t bytes) noexcept
	{
		totalAllocated += bytes;
		currentUsage += bytes;
		++allocationCount;
		if (currentUsage > peakUsage)
		{
			peakUsage = currentUsage;
		}
	}

	/// @brief メモリ解放を記録する
	/// @param bytes 解放サイズ（バイト）
	void recordFree(size_t bytes) noexcept
	{
		totalFreed += bytes;
		if (bytes <= currentUsage)
		{
			currentUsage -= bytes;
		}
		else
		{
			currentUsage = 0;
		}
		++freeCount;
	}

	/// @brief 統計をリセットする
	void reset() noexcept
	{
		totalAllocated = 0;
		totalFreed = 0;
		currentUsage = 0;
		peakUsage = 0;
		allocationCount = 0;
		freeCount = 0;
	}

	/// @brief アクティブな割り当て数を返す
	/// @return 割り当て回数 - 解放回数
	[[nodiscard]] size_t activeAllocations() const noexcept
	{
		if (allocationCount >= freeCount)
		{
			return allocationCount - freeCount;
		}
		return 0;
	}

	/// @brief 平均割り当てサイズを返す
	/// @return 総割り当て量 / 割り当て回数（0回の場合は0.0）
	[[nodiscard]] double averageAllocationSize() const noexcept
	{
		if (allocationCount == 0) return 0.0;
		return static_cast<double>(totalAllocated) / static_cast<double>(allocationCount);
	}

	/// @brief フォーマットされたサマリー文字列を返す
	/// @return 統計情報の文字列表現
	[[nodiscard]] std::string summary() const
	{
		char buf[256]{};
		std::snprintf(buf, sizeof(buf),
			"Memory: %zu bytes used (peak: %zu), "
			"%zu allocs, %zu frees, %zu active",
			currentUsage, peakUsage,
			allocationCount, freeCount, activeAllocations());
		return std::string(buf);
	}
};

/// @brief 自動メモリ計測スコープ
///
/// コンストラクタでrecordAllocation、デストラクタでrecordFreeを呼ぶ。
/// RAII方式でスコープベースのメモリ追跡を実現する。
class ScopedMemoryTracker
{
public:
	/// @brief コンストラクタ。割り当てを記録する
	/// @param stats 統計オブジェクトへの参照
	/// @param bytes 追跡するバイト数
	explicit ScopedMemoryTracker(MemoryStats& stats, size_t bytes)
		: m_stats(stats), m_bytes(bytes)
	{
		m_stats.recordAllocation(m_bytes);
	}

	/// @brief デストラクタ。解放を記録する
	~ScopedMemoryTracker()
	{
		m_stats.recordFree(m_bytes);
	}

	/// @brief コピー禁止
	ScopedMemoryTracker(const ScopedMemoryTracker&) = delete;

	/// @brief コピー代入禁止
	ScopedMemoryTracker& operator=(const ScopedMemoryTracker&) = delete;

private:
	MemoryStats& m_stats;  ///< 統計オブジェクトへの参照
	size_t m_bytes;        ///< 追跡中のバイト数
};

} // namespace sgc::debug
