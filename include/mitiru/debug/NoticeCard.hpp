#pragma once

/// @file NoticeCard.hpp
/// @brief ゲーム画面を暗くして中央にエンジン通知のカードを出す宣言。
/// @details ゲームの一部ではないと一目で分かるよう、帯ではなく画面全体を暗くする。
/// ScreenT は width()、height()、drawRect()、drawTextInRect() を持つこと。テストでは mock でもよい。

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru::debug
{

template <class ScreenT>
void drawNoticeCard(ScreenT& screen, std::string_view title,
                    const std::vector<std::string>& lines, std::string_view hint)
{
	const float W = static_cast<float>(screen.width());
	const float H = static_cast<float>(screen.height());
	screen.drawRect(sgc::Rectf{0.0f, 0.0f, W, H}, sgc::Colorf{0.04f, 0.04f, 0.06f, 0.72f});

	const float padX    = 36.0f;
	const float padY    = 30.0f;
	const float accentH = 6.0f;
	const float titleH  = 40.0f;
	const float lineH   = 28.0f;
	const float hintH   = 26.0f;
	const float gap     = 14.0f;
	const float cardW   = std::min(W - 60.0f, 1080.0f);
	const float bodyH   = lineH * static_cast<float>(lines.size());
	const float cardH   = padY * 2.0f + titleH + gap + bodyH + gap + hintH;
	const float cardX   = (W - cardW) * 0.5f;
	const float cardY   = (H - cardH) * 0.5f;
	const float innerW  = cardW - padX * 2.0f;

	screen.drawRect(sgc::Rectf{cardX, cardY, cardW, cardH}, sgc::Colorf{0.13f, 0.12f, 0.15f, 0.98f});
	screen.drawRect(sgc::Rectf{cardX, cardY, cardW, accentH}, sgc::Colorf{0.86f, 0.20f, 0.24f, 1.0f});

	const float x = cardX + padX;
	float y = cardY + padY;
	screen.drawTextInRect(sgc::Rectf{x, y, innerW, titleH}, std::string(title),
	                      sgc::Colorf{1.0f, 0.55f, 0.55f, 1.0f}, 30.0f);
	y += titleH + gap;
	for (const auto& line : lines)
	{
		screen.drawTextInRect(sgc::Rectf{x, y, innerW, lineH}, line,
		                      sgc::Colorf{0.92f, 0.92f, 0.95f, 1.0f}, 16.0f);
		y += lineH;
	}
	y += gap;
	screen.drawTextInRect(sgc::Rectf{x, y, innerW, hintH}, std::string(hint),
	                      sgc::Colorf{0.66f, 0.68f, 0.74f, 1.0f}, 15.0f);
}

}  // namespace mitiru::debug
