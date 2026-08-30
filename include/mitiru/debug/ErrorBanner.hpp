#pragma once

/// @file ErrorBanner.hpp
/// @brief ビルドエラー帯。`mitiru watch` のエラーファイルを poll して最前面に表示する
/// @details
/// CLI はビルド失敗時に `<project>/build/.mitiru_build_error.txt` を書き、成功時に
/// 削除する (プロトコルは CLI 側が握る)。engine は host から渡された path を
/// ~0.5 秒毎に poll し、ファイルが存在する間だけ画面上部に帯を描く。
/// 直して保存 → ビルド成功でファイルが消え、帯も消える。
/// poll ロジックと描画 (drawTo) は分離されており、ロジック単体でテストできる。

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

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
	/// @details ここはゲームの一部ではない（エンジンからの通知）ので、ゲーム画面に
	///          薄い帯を重ねるのではなく、画面全体を暗くして中央の読みやすいカードに
	///          まとめる。直して保存すれば消える。ScreenT は width()/height()/
	///          drawRect/drawTextInRect を持つこと (テストは mock 可)。
	template <class ScreenT>
	void drawTo(ScreenT& screen) const
	{
		if (m_lines.empty()) { return; }
		const float W = static_cast<float>(screen.width());
		const float H = static_cast<float>(screen.height());

		// ① 画面全体を暗転 = 「これはゲームではなくエンジンの通知」と一目で分かる。
		screen.drawRect(sgc::Rectf{0.0f, 0.0f, W, H}, sgc::Colorf{0.04f, 0.04f, 0.06f, 0.72f});

		// レイアウト。
		const float padX    = 36.0f;
		const float padY    = 30.0f;
		const float accentH = 6.0f;
		const float titleH  = 40.0f;
		const float lineH   = 28.0f;
		const float hintH   = 26.0f;
		const float gap     = 14.0f;
		const float cardW   = std::min(W - 60.0f, 1080.0f);
		const float bodyH   = lineH * static_cast<float>(m_lines.size());
		const float cardH   = padY * 2.0f + titleH + gap + bodyH + gap + hintH;
		const float cardX   = (W - cardW) * 0.5f;
		const float cardY   = (H - cardH) * 0.5f;
		const float innerW  = cardW - padX * 2.0f;

		// ② 中央カード + 上の赤いアクセント。
		screen.drawRect(sgc::Rectf{cardX, cardY, cardW, cardH}, sgc::Colorf{0.13f, 0.12f, 0.15f, 0.98f});
		screen.drawRect(sgc::Rectf{cardX, cardY, cardW, accentH}, sgc::Colorf{0.86f, 0.20f, 0.24f, 1.0f});

		// ③ 見出し → エラー行 → ヒント。
		const float x = cardX + padX;
		float y = cardY + padY;
		screen.drawTextInRect(sgc::Rectf{x, y, innerW, titleH}, "ビルド失敗",
		                      sgc::Colorf{1.0f, 0.55f, 0.55f, 1.0f}, 30.0f);
		y += titleH + gap;
		for (const auto& line : m_lines)
		{
			screen.drawTextInRect(sgc::Rectf{x, y, innerW, lineH}, line,
			                      sgc::Colorf{0.92f, 0.92f, 0.95f, 1.0f}, 16.0f);
			y += lineH;
		}
		y += gap;
		screen.drawTextInRect(sgc::Rectf{x, y, innerW, hintH},
		                      "直して保存すると、自動で再ビルドして続きから再開します",
		                      sgc::Colorf{0.66f, 0.68f, 0.74f, 1.0f}, 15.0f);
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
