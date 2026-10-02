#pragma once

/// @file ErrorBanner.hpp
/// @brief ビルドエラー帯。`mitiru watch` のエラーファイルを poll して最前面に表示する
/// @details
/// CLI はビルド失敗時に `<project>/build/.mitiru_build_error.txt` を書き、成功時に
/// 削除する (プロトコルは CLI 側が決める)。engine は host から渡された path を
/// ~0.5 秒毎に poll し、ファイルが存在する間だけ画面上部に帯を描く。
/// 直して保存 → ビルド成功でファイルが消え、帯も消える。
/// poll ロジックと描画 (drawTo) は分離されており、ロジック単体でテストできる。

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <mitiru/debug/NoticeCard.hpp>

namespace mitiru::debug
{

/// @brief エラーファイルの poll + 帯描画。Engine が 1 個所有し毎フレーム poll(now) を呼ぶ。
class ErrorBanner
{
public:
	static constexpr double kPollIntervalSec = 0.5;  ///< mtime poll の間隔
	static constexpr int    kMaxDisplayLines = 6;    ///< 帯に出す最大行数 (画面を奪いすぎない)

	/// @brief 監視するエラーファイルの path を設定する (空=機能 OFF)
	void setFile(std::string path)
	{
		m_path = std::move(path);
		m_nextPollSec = 0.0;  // 次の poll で即評価
	}

	/// @brief 機能が有効か (path が設定済みか)
	[[nodiscard]] bool enabled() const noexcept { return !m_path.empty(); }

	/// @brief 帯を表示すべきか (エラーファイルが存在し中身を保持中か)
	[[nodiscard]] bool active() const noexcept { return !m_lines.empty(); }

	/// @brief 現在保持しているエラー行 (先頭 kMaxDisplayLines 行)
	[[nodiscard]] const std::vector<std::string>& lines() const noexcept { return m_lines; }

	/// @brief エラーファイルを poll する。nowSec は単調増加の秒 (throttle 判定用)。
	/// @details kPollIntervalSec 毎にしか filesystem を見ない。ファイル消滅で即クリア、
	///          mtime 変化時のみ再読込 (同一 mtime なら disk read をスキップ)。
	void poll(double nowSec)
	{
		if (m_path.empty() || nowSec < m_nextPollSec) { return; }
		m_nextPollSec = nowSec + kPollIntervalSec;

		std::error_code ec;
		const auto mtime = std::filesystem::last_write_time(m_path, ec);
		if (ec)
		{
			// ファイルが無い = ビルド成功 (CLI が削除した) → 帯を消す
			m_lines.clear();
			m_hasMtime = false;
			return;
		}
		if (m_hasMtime && mtime == m_lastMtime) { return; }
		m_lastMtime = mtime;
		m_hasMtime  = true;
		reload();
	}

	/// @brief 画面を暗転させ、中央に「ビルド失敗」カードを出す (active でなければ no-op)。
	template <class ScreenT>
	void drawTo(ScreenT& screen) const
	{
		if (m_lines.empty()) { return; }
		drawNoticeCard(screen, "ビルド失敗", m_lines,
		               "直して保存すると、自動で再ビルドして続きから再開します");
	}

private:
	/// @brief ファイルを読み直し、空でない先頭 kMaxDisplayLines 行を保持する
	void reload()
	{
		m_lines.clear();
		std::ifstream in(std::filesystem::path{m_path});
		if (!in) { return; }
		std::string line;
		while (static_cast<int>(m_lines.size()) < kMaxDisplayLines
		       && std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') { line.pop_back(); }
			if (line.empty()) { continue; }
			m_lines.push_back(line);
		}
	}

	std::string                     m_path;
	std::vector<std::string>        m_lines;
	std::filesystem::file_time_type m_lastMtime{};
	bool                            m_hasMtime    = false;
	double                          m_nextPollSec = 0.0;
};

}  // namespace mitiru::debug
