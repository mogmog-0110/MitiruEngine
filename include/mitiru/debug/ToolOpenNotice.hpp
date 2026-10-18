#pragma once

/// @file ToolOpenNotice.hpp
/// @brief ツール窓を開けなかったとき、画面の下に数秒だけ出す 1 行。
/// @details コンソールは遊ぶ人に見えないので、F8 のメニューから選んでも何も起きない理由を画面に出す。
///          ScreenT は width()、height()、drawRect()、drawTextInRect() を持つこと。

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::debug
{

class ToolOpenNotice
{
public:
	static constexpr double kShowSec = 4.0;

	/// @param key 開けなかった要求の印。表示中は同じ要求を試し直さない (毎フレーム頼むゲームで失敗を繰り返さないため)
	void show(std::string text, std::string key, double nowSec)
	{
		m_text     = std::move(text);
		m_key      = std::move(key);
		m_untilSec = nowSec + kShowSec;
	}

	[[nodiscard]] bool suppresses(const std::string& key, double nowSec) const noexcept
	{
		return active(nowSec) && key == m_key;
	}

	[[nodiscard]] bool active(double nowSec) const noexcept { return !m_text.empty() && nowSec < m_untilSec; }

	[[nodiscard]] const std::string& text() const noexcept { return m_text; }

	template <class ScreenT>
	void drawTo(ScreenT& screen, double nowSec) const
	{
		if (!active(nowSec)) { return; }
		const float W    = static_cast<float>(screen.width());
		const float H    = static_cast<float>(screen.height());
		const float barH = 36.0f;
		const float barW = std::min(W - 40.0f, 720.0f);
		const float x    = (W - barW) * 0.5f;
		const float y    = H - barH - 24.0f;
		screen.drawRect(sgc::Rectf{x, y, barW, barH}, sgc::Colorf{0.13f, 0.12f, 0.15f, 0.94f});
		screen.drawRect(sgc::Rectf{x, y, 5.0f, barH}, sgc::Colorf{0.86f, 0.20f, 0.24f, 1.0f});
		screen.drawTextInRect(sgc::Rectf{x + 16.0f, y + 6.0f, barW - 28.0f, barH - 10.0f}, m_text,
		                      sgc::Colorf{0.92f, 0.92f, 0.95f, 1.0f}, 16.0f);
	}

private:
	std::string m_text;
	std::string m_key;
	double      m_untilSec = 0.0;
};

}  // namespace mitiru::debug
