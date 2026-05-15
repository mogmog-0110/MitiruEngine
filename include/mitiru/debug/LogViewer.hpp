#pragma once

/// @file LogViewer.hpp
/// @brief インタラクティブログビューワー
/// @details スレッドセーフなリングバッファでログエントリを蓄積し、
///          レベル・カテゴリ・テキスト検索でフィルタリングしながら
///          オーバーレイ表示する。F11キーで折りたたみ切り替え。
///
/// @code
/// mitiru::debug::LogViewer viewer;
/// viewer.addEntry({mitiru::debug::LogLevel::Info, "Engine started", 0.0, "Core", "System"});
/// viewer.drawOverlay(screen);
/// @endcode

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/types/Color.hpp>
#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>

#include <mitiru/debug/ILogger.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::debug
{

/// @brief ログエントリ
struct LogEntry
{
	LogLevel level = LogLevel::Info;   ///< ログレベル
	std::string message;               ///< メッセージ本文
	double timestamp = 0.0;            ///< タイムスタンプ（秒）
	std::string source;                ///< 発生元
	std::string category;              ///< カテゴリ
};

/// @brief インタラクティブログビューワー
/// @details ログエントリのリングバッファ管理・フィルタリング・描画を行う。
///          addEntry()はスレッドセーフ。
class LogViewer
{
public:
	/// @brief コンストラクタ
	/// @param maxEntries 最大保持エントリ数
	explicit LogViewer(std::size_t maxEntries = 1000)
		: m_maxEntries(maxEntries)
	{
		m_entries.reserve(maxEntries);
	}

	// ── エントリ管理 ──

	/// @brief ログエントリを追加する（スレッドセーフ）
	/// @param entry 追加するエントリ
	void addEntry(const LogEntry& entry)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_entries.size() >= m_maxEntries)
		{
			m_entries.erase(m_entries.begin());
		}
		m_entries.push_back(entry);

		// 自動スクロールが有効なら末尾に追従する
		if (m_autoScroll)
		{
			m_scrollOffset = scrollMax();
		}
	}

	/// @brief ログエントリを追加する（ILoggerからの簡易連携）
	/// @param level ログレベル
	/// @param category カテゴリ
	/// @param message メッセージ
	/// @param timestamp タイムスタンプ（秒）
	void addEntry(LogLevel level, std::string_view category,
	              std::string_view message, double timestamp = 0.0)
	{
		addEntry(LogEntry{level, std::string(message), timestamp,
		                  std::string(category), std::string(category)});
	}

	/// @brief 全エントリをクリアする
	void clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_entries.clear();
		m_scrollOffset = 0;
	}

	/// @brief 保持エントリ数を取得する
	[[nodiscard]] std::size_t entryCount() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_entries.size();
	}

	// ── フィルタリング ──

	/// @brief 最小表示レベルを設定する
	void setMinLevel(LogLevel level) noexcept { m_filterLevel = level; }

	/// @brief カテゴリフィルタを設定する（空で全カテゴリ表示）
	void setCategoryFilter(std::string_view category) { m_filterCategory = std::string(category); }

	/// @brief テキスト検索フィルタを設定する（空でフィルタなし）
	void setTextFilter(std::string_view text) { m_filterText = std::string(text); }

	// ── 表示制御 ──

	/// @brief 表示/非表示を切り替える（F11トグル）
	void toggleVisible() noexcept { m_collapsed = !m_collapsed; }

	/// @brief 折りたたみ状態を設定する
	void setCollapsed(bool collapsed) noexcept { m_collapsed = collapsed; }

	/// @brief 折りたたみ状態を取得する
	[[nodiscard]] bool isCollapsed() const noexcept { return m_collapsed; }

	/// @brief 表示/非表示を設定する
	void setVisible(bool visible) noexcept { m_visible = visible; }

	/// @brief 表示状態を取得する
	[[nodiscard]] bool isVisible() const noexcept { return m_visible; }

	/// @brief 自動スクロールを設定する
	void setAutoScroll(bool autoScroll) noexcept { m_autoScroll = autoScroll; }

	/// @brief 上方向スクロール（自動スクロールを一時停止）
	void scrollUp(int lines = 1)
	{
		m_scrollOffset = std::max(0, m_scrollOffset - lines);
		m_autoScroll = false;
	}

	/// @brief 下方向スクロール
	void scrollDown(int lines = 1)
	{
		m_scrollOffset = std::min(scrollMax(), m_scrollOffset + lines);
		if (m_scrollOffset >= scrollMax())
		{
			m_autoScroll = true;
		}
	}

	/// @brief 最大保持エントリ数を設定する
	void setMaxEntries(std::size_t max)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_maxEntries = max;
		while (m_entries.size() > m_maxEntries)
		{
			m_entries.erase(m_entries.begin());
		}
	}

	// ── クリップボード出力 ──

	/// @brief フィルタ適用済みのログをテキスト形式で取得する
	/// @return テキストフォーマットのログ文字列
	[[nodiscard]] std::string copyToClipboard() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::string result;
		result.reserve(m_entries.size() * 80);

		for (const auto& entry : m_entries)
		{
			if (!passesFilter(entry)) continue;
			result += "[" + levelTag(entry.level) + "] ";
			result += "[" + entry.category + "] ";
			result += entry.message + "\n";
		}
		return result;
	}

	// ── 描画 ──

	/// @brief オーバーレイ描画
	/// @param screen 描画先スクリーン
	void drawOverlay(Screen& screen) const
	{
		if (!m_visible) return;

		const float panelW = static_cast<float>(screen.width()) * 0.6f;
		const float panelX = static_cast<float>(screen.width()) - panelW - 8.0f;

		if (m_collapsed)
		{
			// 折りたたみ時はタイトルバーのみ描画する
			screen.drawRect(
				sgc::Rectf{panelX, 8.0f, panelW, 20.0f},
				{0.0f, 0.0f, 0.0f, 0.7f});

			const std::string title = "Log Viewer [F11] (" +
				std::to_string(entryCount()) + " entries)";
			screen.drawText({panelX + 4.0f, 10.0f}, title, {0.8f, 0.8f, 0.8f, 1.0f}, 8.0f);
			return;
		}

		const float panelH = static_cast<float>(screen.height()) * 0.4f;
		const float panelY = static_cast<float>(screen.height()) - panelH - 8.0f;

		// 背景パネル
		screen.drawRect(
			sgc::Rectf{panelX, panelY, panelW, panelH},
			{0.02f, 0.02f, 0.05f, 0.85f});

		// タイトルバー
		screen.drawRect(
			sgc::Rectf{panelX, panelY, panelW, 18.0f},
			{0.1f, 0.1f, 0.15f, 0.9f});

		const std::string title = "Log Viewer [F11]  Entries: " +
			std::to_string(entryCount()) +
			(m_autoScroll ? "  [AUTO]" : "  [PAUSED]");
		screen.drawText({panelX + 4.0f, panelY + 3.0f}, title, {0.9f, 0.9f, 0.9f, 1.0f}, 8.0f);

		// フィルタ情報バー
		if (!m_filterCategory.empty() || !m_filterText.empty())
		{
			std::string filterInfo = "Filter:";
			if (!m_filterCategory.empty()) filterInfo += " cat=" + m_filterCategory;
			if (!m_filterText.empty()) filterInfo += " text=" + m_filterText;
			screen.drawText(
				{panelX + 4.0f, panelY + 20.0f},
				filterInfo, {0.5f, 0.8f, 1.0f, 0.8f}, 8.0f);
		}

		// ログ行の描画
		const float lineH = 12.0f;
		const float contentY = panelY + 34.0f;
		const float contentH = panelH - 38.0f;
		const int visibleLines = std::max(1, static_cast<int>(contentH / lineH));

		std::lock_guard<std::mutex> lock(m_mutex);

		// フィルタ済みエントリを収集する
		std::vector<const LogEntry*> filtered;
		filtered.reserve(m_entries.size());
		for (const auto& entry : m_entries)
		{
			if (passesFilter(entry))
			{
				filtered.push_back(&entry);
			}
		}

		// スクロールオフセットを適用する
		const int maxScroll = std::max(0, static_cast<int>(filtered.size()) - visibleLines);
		const int offset = m_autoScroll ? maxScroll : std::min(m_scrollOffset, maxScroll);

		float curY = contentY;
		for (int i = offset; i < static_cast<int>(filtered.size()) && i < offset + visibleLines; ++i)
		{
			const auto& entry = *filtered[static_cast<std::size_t>(i)];
			const sgc::Colorf color = levelToColor(entry.level);

			const std::string line =
				"[" + levelTag(entry.level) + "] " +
				"[" + entry.category + "] " +
				entry.message;

			screen.drawText({panelX + 4.0f, curY}, line, color, 8.0f);
			curY += lineH;
		}

		// スクロールバー
		if (static_cast<int>(filtered.size()) > visibleLines)
		{
			const float sbX = panelX + panelW - 6.0f;
			const float sbH = contentH;
			const float thumbH = sbH * (static_cast<float>(visibleLines) / static_cast<float>(filtered.size()));
			const float thumbY = contentY + (sbH - thumbH) *
				(static_cast<float>(offset) / static_cast<float>(maxScroll));

			screen.drawRect(sgc::Rectf{sbX, contentY, 4.0f, sbH}, {0.2f, 0.2f, 0.2f, 0.5f});
			screen.drawRect(sgc::Rectf{sbX, thumbY, 4.0f, thumbH}, {0.6f, 0.6f, 0.6f, 0.7f});
		}
	}

private:
	mutable std::mutex m_mutex;
	std::vector<LogEntry> m_entries;
	std::size_t m_maxEntries;

	// フィルタ設定
	LogLevel m_filterLevel = LogLevel::Debug;
	std::string m_filterCategory;
	std::string m_filterText;

	// 表示状態
	bool m_visible = true;
	bool m_collapsed = false;
	bool m_autoScroll = true;
	int m_scrollOffset = 0;

	/// @brief エントリがフィルタを通過するか判定する
	[[nodiscard]] bool passesFilter(const LogEntry& entry) const
	{
		// レベルフィルタ
		if (static_cast<std::uint8_t>(entry.level) < static_cast<std::uint8_t>(m_filterLevel))
		{
			return false;
		}

		// カテゴリフィルタ
		if (!m_filterCategory.empty() && entry.category != m_filterCategory)
		{
			return false;
		}

		// テキスト検索フィルタ
		if (!m_filterText.empty() &&
		    entry.message.find(m_filterText) == std::string::npos)
		{
			return false;
		}

		return true;
	}

	/// @brief 最大スクロールオフセットを計算する
	[[nodiscard]] int scrollMax() const
	{
		return std::max(0, static_cast<int>(m_entries.size()) - 20);
	}

	/// @brief ログレベルに応じた描画色を返す
	[[nodiscard]] static sgc::Colorf levelToColor(LogLevel level) noexcept
	{
		switch (level)
		{
		case LogLevel::Trace: return {0.5f, 0.5f, 0.5f, 0.7f};  // 灰
		case LogLevel::Debug: return {0.6f, 0.6f, 0.6f, 0.9f};  // 灰
		case LogLevel::Info:  return {0.9f, 0.9f, 0.9f, 1.0f};  // 白
		case LogLevel::Warn:  return {1.0f, 0.9f, 0.2f, 1.0f};  // 黄
		case LogLevel::Error: return {1.0f, 0.3f, 0.3f, 1.0f};  // 赤
		default:              return {0.8f, 0.8f, 0.8f, 1.0f};
		}
	}

	/// @brief ログレベルの短縮タグを返す
	[[nodiscard]] static std::string levelTag(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Trace: return "TRACE";
		case LogLevel::Debug: return "DEBUG";
		case LogLevel::Info:  return "INFO ";
		case LogLevel::Warn:  return "WARN ";
		case LogLevel::Error: return "ERROR";
		default:              return "?????";
		}
	}
};

} // namespace mitiru::debug
