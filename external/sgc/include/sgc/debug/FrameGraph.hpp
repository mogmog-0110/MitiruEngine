#pragma once

/// @file FrameGraph.hpp
/// @brief フレームタイミング可視化データ
///
/// フレーム内の各セクションの実行時間を記録し、
/// 可視化用のデータとして提供する。
/// ScopedSectionによるRAIIベースの自動計測が可能。
///
/// @code
/// sgc::debug::FrameGraph graph;
///
/// {
///     sgc::debug::ScopedSection section(graph, "Update", 0xFF0000);
///     // ... 更新処理
///     {
///         sgc::debug::ScopedSection nested(graph, "Physics", 0x00FF00);
///         // ... 物理演算
///     }
/// }
///
/// for (const auto& entry : graph.entries())
/// {
///     // entry.name, entry.durationMs, entry.depth でUI描画
/// }
///
/// graph.clearFrame();
/// @endcode

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sgc::debug
{

/// @brief フレームタイミングエントリ
///
/// 1つの計測セクションの実行時間情報を保持する。
struct FrameTimingEntry
{
	std::string name;          ///< セクション名
	double startMs{0.0};       ///< フレーム開始からの相対開始時刻（ミリ秒）
	double durationMs{0.0};    ///< 実行時間（ミリ秒）
	std::size_t depth{0};      ///< ネスト深度（0がルート）
	std::uint32_t color{0};    ///< 描画色（0xRRGGBB）
};

/// @brief フレームタイミング収集クラス
///
/// フレーム内の各セクションの開始/終了を記録し、
/// エントリの配列として提供する。
class FrameGraph
{
public:
	/// @brief 使用するクロック型
	using Clock = std::chrono::high_resolution_clock;

	/// @brief 時間点型
	using TimePoint = Clock::time_point;

	/// @brief セクションの計測を開始する
	/// @param name セクション名
	/// @param color 描画色（0xRRGGBB）
	/// @return エントリインデックス（endSection用）
	std::size_t beginSection(const std::string& name, std::uint32_t color = 0xFFFFFF)
	{
		if (m_entries.empty())
		{
			m_frameStart = Clock::now();
		}

		const auto now = Clock::now();
		const double startMs = std::chrono::duration<double, std::milli>(now - m_frameStart).count();

		const auto index = m_entries.size();
		m_entries.push_back(FrameTimingEntry{
			name,
			startMs,
			0.0,
			m_currentDepth,
			color
		});

		++m_currentDepth;
		m_activeStack.push_back(index);

		return index;
	}

	/// @brief セクションの計測を終了する
	/// @param index beginSectionで返されたインデックス
	void endSection(std::size_t index)
	{
		if (index >= m_entries.size()) return;

		const auto now = Clock::now();
		const double endMs = std::chrono::duration<double, std::milli>(now - m_frameStart).count();
		m_entries[index].durationMs = endMs - m_entries[index].startMs;

		if (m_currentDepth > 0) --m_currentDepth;

		if (!m_activeStack.empty() && m_activeStack.back() == index)
		{
			m_activeStack.pop_back();
		}
	}

	/// @brief 手動で計測エントリを追加する（タイミングを直接指定）
	/// @param entry タイミングエントリ
	void addEntry(const FrameTimingEntry& entry)
	{
		m_entries.push_back(entry);
	}

	/// @brief 現フレームのエントリ一覧を取得する
	/// @return エントリの配列
	[[nodiscard]] const std::vector<FrameTimingEntry>& entries() const noexcept
	{
		return m_entries;
	}

	/// @brief エントリ数を取得する
	/// @return エントリ数
	[[nodiscard]] std::size_t entryCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief フレームデータをクリアする
	void clearFrame()
	{
		m_entries.clear();
		m_activeStack.clear();
		m_currentDepth = 0;
	}

	/// @brief 現在のネスト深度を取得する
	/// @return 深度
	[[nodiscard]] std::size_t currentDepth() const noexcept
	{
		return m_currentDepth;
	}

private:
	std::vector<FrameTimingEntry> m_entries;      ///< タイミングエントリ配列
	std::vector<std::size_t> m_activeStack;       ///< アクティブなセクションのスタック
	TimePoint m_frameStart{};                      ///< フレーム開始時刻
	std::size_t m_currentDepth{0};                 ///< 現在のネスト深度
};

/// @brief RAII式セクション計測ヘルパー
///
/// コンストラクタでbeginSection、デストラクタでendSectionを呼ぶ。
/// スコープを抜けるときに自動的に計測が終了する。
class ScopedSection
{
public:
	/// @brief セクション計測を開始する
	/// @param graph 計測先のFrameGraph
	/// @param name セクション名
	/// @param color 描画色（0xRRGGBB）
	ScopedSection(FrameGraph& graph, const std::string& name, std::uint32_t color = 0xFFFFFF)
		: m_graph(graph)
		, m_index(graph.beginSection(name, color))
	{
	}

	/// @brief デストラクタ（計測終了）
	~ScopedSection()
	{
		m_graph.endSection(m_index);
	}

	/// @brief コピー禁止
	ScopedSection(const ScopedSection&) = delete;
	/// @brief コピー代入禁止
	ScopedSection& operator=(const ScopedSection&) = delete;

	/// @brief ムーブ禁止
	ScopedSection(ScopedSection&&) = delete;
	/// @brief ムーブ代入禁止
	ScopedSection& operator=(ScopedSection&&) = delete;

private:
	FrameGraph& m_graph;        ///< 参照先FrameGraph
	std::size_t m_index;        ///< エントリインデックス
};

} // namespace sgc::debug
