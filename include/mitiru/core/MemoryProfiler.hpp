#pragma once

/// @file MemoryProfiler.hpp
/// @brief メモリトラッキングとプロファイリング
/// @details アロケーション追跡・リーク検出・バジェット管理・オーバーレイ描画を提供する。
///          リングバッファで直近のアロケーション履歴を保持する。

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/debug/Log.hpp>

namespace mitiru
{

class Screen;

/// @brief アロケーション情報
/// @details 個々のメモリアロケーションを記録する。
struct AllocationInfo
{
	const void* ptr = nullptr;      ///< 割り当てられたポインタ
	std::size_t size = 0;           ///< バイト数
	std::string tag;                ///< アロケーションタグ（カテゴリ名）
	std::uint64_t frameNumber = 0;  ///< 割り当てフレーム番号
	std::uint64_t timestamp = 0;    ///< 割り当てタイムスタンプ（ミリ秒）
};

/// @brief リーク情報
/// @details 解放されていないアロケーションの詳細。
struct LeakInfo
{
	const void* ptr = nullptr;      ///< リークしたポインタ
	std::size_t size = 0;           ///< リークしたバイト数
	std::string tag;                ///< アロケーションタグ
	std::uint64_t frameNumber = 0;  ///< 割り当てフレーム番号
};

/// @brief メモリバジェット
/// @details タグごとのメモリ使用量上限を定義する。
struct MemoryBudget
{
	std::size_t maxBytes = 0;       ///< 最大バイト数
	std::size_t currentBytes = 0;   ///< 現在の使用量
};

/// @brief メモリプロファイラー
/// @details アロケーション追跡・リーク検出・バジェット管理を行う。
///          スレッドセーフな実装。
///
/// @code
/// mitiru::MemoryProfiler profiler;
///
/// void* p = malloc(1024);
/// profiler.trackAllocation(p, 1024, "Texture");
///
/// auto usage = profiler.getCurrentUsage();
/// auto peak = profiler.getPeakUsage();
///
/// free(p);
/// profiler.trackDeallocation(p);
///
/// auto leaks = profiler.detectLeaks();
/// @endcode
class MemoryProfiler
{
public:
	/// @brief リングバッファのデフォルトサイズ
	static constexpr std::size_t kDefaultHistorySize = 1024;

	/// @brief デフォルトコンストラクタ
	/// @param historySize リングバッファサイズ
	explicit MemoryProfiler(std::size_t historySize = kDefaultHistorySize) noexcept
		: m_historyCapacity(historySize)
	{
		m_history.reserve(historySize);
	}

	/// @brief アロケーションを追跡する
	/// @param ptr 割り当てられたポインタ
	/// @param size バイト数
	/// @param tag アロケーションタグ（カテゴリ名）
	void trackAllocation(const void* ptr, std::size_t size,
	                     const std::string& tag = "default")
	{
		if (!ptr) return;

		std::lock_guard<std::mutex> lock(m_mutex);

		AllocationInfo info;
		info.ptr = ptr;
		info.size = size;
		info.tag = tag;
		info.frameNumber = m_frameCounter;
		info.timestamp = currentTimestampMs();

		m_allocations[ptr] = info;
		m_totalAllocated += size;
		m_peakUsage = std::max(m_peakUsage, m_totalAllocated);

		/// タグ別使用量を更新する
		m_tagUsage[tag] += size;

		/// リングバッファに記録する
		pushHistory(info);

		/// アロケーションカウントを増加する
		++m_allocationCount;
	}

	/// @brief デアロケーションを追跡する
	/// @param ptr 解放されるポインタ
	void trackDeallocation(const void* ptr)
	{
		if (!ptr) return;

		std::lock_guard<std::mutex> lock(m_mutex);

		const auto it = m_allocations.find(ptr);
		if (it == m_allocations.end())
		{
			MITIRU_LOG_WARN("MemoryProfiler",
				"trackDeallocation: unknown pointer");
			return;
		}

		const std::size_t size = it->second.size;
		const std::string& tag = it->second.tag;

		m_totalAllocated -= size;

		/// タグ別使用量を減少する
		auto tagIt = m_tagUsage.find(tag);
		if (tagIt != m_tagUsage.end())
		{
			if (tagIt->second >= size)
			{
				tagIt->second -= size;
			}
			else
			{
				tagIt->second = 0;
			}
		}

		m_allocations.erase(it);
		++m_deallocationCount;
	}

	/// @brief 現在の総メモリ使用量を取得する
	/// @return 使用中のバイト数
	[[nodiscard]] std::size_t getCurrentUsage() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_totalAllocated;
	}

	/// @brief ピークメモリ使用量を取得する
	/// @return これまでの最大バイト数
	[[nodiscard]] std::size_t getPeakUsage() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_peakUsage;
	}

	/// @brief タグ別メモリ使用量を取得する
	/// @param tag アロケーションタグ
	/// @return タグに紐付くバイト数
	[[nodiscard]] std::size_t getUsageByTag(const std::string& tag) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_tagUsage.find(tag);
		if (it != m_tagUsage.end())
		{
			return it->second;
		}
		return 0;
	}

	/// @brief 現在のアロケーション一覧を取得する
	/// @return アロケーション情報のベクター
	[[nodiscard]] std::vector<AllocationInfo> getAllocations() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<AllocationInfo> result;
		result.reserve(m_allocations.size());
		for (const auto& [ptr, info] : m_allocations)
		{
			result.push_back(info);
		}
		return result;
	}

	/// @brief メモリリークを検出する
	/// @return リーク候補のベクター（現在未解放のアロケーション）
	[[nodiscard]] std::vector<LeakInfo> detectLeaks() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<LeakInfo> leaks;
		leaks.reserve(m_allocations.size());

		for (const auto& [ptr, info] : m_allocations)
		{
			LeakInfo leak;
			leak.ptr = info.ptr;
			leak.size = info.size;
			leak.tag = info.tag;
			leak.frameNumber = info.frameNumber;
			leaks.push_back(leak);
		}

		/// サイズ降順でソートする（大きなリークを先に表示）
		std::sort(leaks.begin(), leaks.end(),
			[](const LeakInfo& a, const LeakInfo& b)
			{
				return a.size > b.size;
			});

		return leaks;
	}

	/// @brief タグにメモリバジェットを設定する
	/// @param tag アロケーションタグ
	/// @param maxBytes 最大許容バイト数
	void setBudget(const std::string& tag, std::size_t maxBytes)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_budgets[tag].maxBytes = maxBytes;
	}

	/// @brief タグがバジェットを超過しているかを判定する
	/// @param tag アロケーションタグ
	/// @return バジェット超過ならtrue
	[[nodiscard]] bool isOverBudget(const std::string& tag) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		const auto budgetIt = m_budgets.find(tag);
		if (budgetIt == m_budgets.end()) return false;

		const auto usageIt = m_tagUsage.find(tag);
		const std::size_t usage =
			(usageIt != m_tagUsage.end()) ? usageIt->second : 0;

		return usage > budgetIt->second.maxBytes;
	}

	/// @brief フレーム番号を進める
	void advanceFrame() noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_frameCounter;
	}

	/// @brief 直近のアロケーション履歴を取得する
	/// @return リングバッファ内のアロケーション情報
	[[nodiscard]] std::vector<AllocationInfo> getHistory() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_history;
	}

	/// @brief アロケーション数を取得する
	[[nodiscard]] std::uint64_t allocationCount() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_allocationCount;
	}

	/// @brief デアロケーション数を取得する
	[[nodiscard]] std::uint64_t deallocationCount() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_deallocationCount;
	}

	/// @brief 現在のアクティブアロケーション数を取得する
	[[nodiscard]] std::size_t activeAllocationCount() const noexcept
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_allocations.size();
	}

	/// @brief タグ別使用量マップを取得する
	[[nodiscard]] std::unordered_map<std::string, std::size_t> tagUsageMap() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_tagUsage;
	}

	/// @brief 統計をリセットする（テスト用）
	void reset()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_allocations.clear();
		m_tagUsage.clear();
		m_budgets.clear();
		m_history.clear();
		m_historyIndex = 0;
		m_totalAllocated = 0;
		m_peakUsage = 0;
		m_allocationCount = 0;
		m_deallocationCount = 0;
		m_frameCounter = 0;
	}

	/// @brief デバッグオーバーレイを描画する
	/// @param screen 描画先スクリーン
	/// @details メモリ使用量・ピーク・タグ別内訳・バジェットバーを描画する。
	void drawOverlay(Screen& screen) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		/// テキスト行を構築する
		std::vector<std::string> lines;

		/// 総使用量・ピーク
		lines.push_back("Memory: " + formatBytes(m_totalAllocated)
			+ " / Peak: " + formatBytes(m_peakUsage));

		/// アクティブアロケーション数
		lines.push_back("Active: " + std::to_string(m_allocations.size())
			+ " allocs (" + std::to_string(m_allocationCount)
			+ " total, " + std::to_string(m_deallocationCount) + " freed)");

		/// タグ別トップ5
		std::vector<std::pair<std::string, std::size_t>> sorted(
			m_tagUsage.begin(), m_tagUsage.end());
		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) { return a.second > b.second; });

		const std::size_t topN = std::min(sorted.size(), std::size_t{5});
		for (std::size_t i = 0; i < topN; ++i)
		{
			const auto& [tag, usage] = sorted[i];
			std::string line = "  " + tag + ": " + formatBytes(usage);

			/// バジェットが設定されている場合、バーを表示する
			const auto budgetIt = m_budgets.find(tag);
			if (budgetIt != m_budgets.end() && budgetIt->second.maxBytes > 0)
			{
				const float ratio = static_cast<float>(usage)
					/ static_cast<float>(budgetIt->second.maxBytes);
				const int barWidth = 20;
				const int filled = std::clamp(
					static_cast<int>(ratio * static_cast<float>(barWidth)),
					0, barWidth);

				line += " [";
				for (int j = 0; j < barWidth; ++j)
				{
					line += (j < filled) ? '#' : '.';
				}
				line += "] ";
				line += std::to_string(static_cast<int>(ratio * 100.0f)) + "%";

				if (usage > budgetIt->second.maxBytes)
				{
					line += " OVER!";
				}
			}

			lines.push_back(line);
		}

		/// Screen にテキストとして描画する（Screenの drawText を使用）
		drawOverlayLines(screen, lines);
	}

private:
	/// @brief リングバッファにアロケーション情報を追加する
	void pushHistory(const AllocationInfo& info)
	{
		if (m_history.size() < m_historyCapacity)
		{
			m_history.push_back(info);
		}
		else
		{
			m_history[m_historyIndex] = info;
		}
		m_historyIndex = (m_historyIndex + 1) % m_historyCapacity;
	}

	/// @brief 現在のタイムスタンプ（ミリ秒）を取得する
	[[nodiscard]] static std::uint64_t currentTimestampMs() noexcept
	{
		const auto now = std::chrono::steady_clock::now();
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				now.time_since_epoch()).count());
	}

	/// @brief バイト数を人間可読な文字列に変換する
	[[nodiscard]] static std::string formatBytes(std::size_t bytes)
	{
		if (bytes < 1024)
		{
			return std::to_string(bytes) + " B";
		}
		if (bytes < 1024 * 1024)
		{
			std::ostringstream oss;
			oss << std::fixed;
			oss.precision(1);
			oss << static_cast<double>(bytes) / 1024.0 << " KB";
			return oss.str();
		}
		if (bytes < 1024ULL * 1024 * 1024)
		{
			std::ostringstream oss;
			oss << std::fixed;
			oss.precision(1);
			oss << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MB";
			return oss.str();
		}
		std::ostringstream oss;
		oss << std::fixed;
		oss.precision(2);
		oss << static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) << " GB";
		return oss.str();
	}

	/// @brief オーバーレイテキスト行を描画する
	/// @param screen 描画先スクリーン
	/// @param lines テキスト行
	void drawOverlayLines(Screen& screen, const std::vector<std::string>& lines) const
	{
		/// Screen::drawText が利用可能な場合に描画する
		/// 左上からY方向に16px間隔で配置する
		static constexpr int kStartX = 8;
		static constexpr int kStartY = 8;
		static constexpr int kLineHeight = 16;

		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			const int y = kStartY + static_cast<int>(i) * kLineHeight;
			/// Screen APIを通じてテキストを描画する
			/// （Screen::drawDebugText はデバッグ文字列を受け取る簡易API）
			static_cast<void>(screen);
			static_cast<void>(y);
			static_cast<void>(kStartX);
			/// NOTE: Screen に drawDebugText() が追加された際にここを接続する
			/// screen.drawDebugText(kStartX, y, lines[i]);
		}
	}

	mutable std::mutex m_mutex;                                 ///< スレッド安全用ミューテックス
	std::unordered_map<const void*, AllocationInfo> m_allocations; ///< アクティブなアロケーション
	std::unordered_map<std::string, std::size_t> m_tagUsage;    ///< タグ別使用量
	std::unordered_map<std::string, MemoryBudget> m_budgets;    ///< タグ別バジェット
	std::vector<AllocationInfo> m_history;                       ///< リングバッファ履歴
	std::size_t m_historyCapacity;                               ///< リングバッファ容量
	std::size_t m_historyIndex = 0;                              ///< リングバッファの書き込み位置
	std::size_t m_totalAllocated = 0;                            ///< 現在の総アロケーション量
	std::size_t m_peakUsage = 0;                                 ///< ピーク使用量
	std::uint64_t m_allocationCount = 0;                         ///< 総アロケーション回数
	std::uint64_t m_deallocationCount = 0;                       ///< 総デアロケーション回数
	std::uint64_t m_frameCounter = 0;                            ///< フレームカウンター
};

} // namespace mitiru
