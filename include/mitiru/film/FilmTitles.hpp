#pragma once

/// @file FilmTitles.hpp
/// @brief タイトルカードと下三分の一の字幕を、時刻 t の画面へ重ねる。drawTextInRect だけを使い、ABI には触れない。RmlUi を使えない理由は ADR 0141。

#include <string>

#include <mitiru/core/Screen.hpp>
#include <mitiru/film/FilmEdit.hpp>
#include <mitiru/render/ScreenEnhanced.hpp>

namespace mitiru::film::edit
{

namespace title_detail
{

using Align = Screen::TextAlignH;

/// @brief 左から滑り込むパネルに、題と副題を 2 行で置く。濃さ a は fade_in と fade_out から決まる。
inline void drawLowerThird(Screen& s, const TitleCard& c, float a)
{
	const float w = static_cast<float>(s.width()), h = static_cast<float>(s.height());
	const float size = h * c.size;
	const float ease = 1.0f - (1.0f - a) * (1.0f - a);
	const float slide = -(1.0f - ease) * w * 0.04f;
	const float boxW = w * 0.46f, boxH = c.sub.empty() ? size * 1.9f : size * 3.0f;
	const float x = c.x * w + slide, y = c.y * h;
	if (c.panel > 0.0f) { s.drawRect(sgc::Rectf{x, y, boxW, boxH}, sgc::Colorf{0.0f, 0.0f, 0.0f, c.panel * a}); }
	s.drawRect(sgc::Rectf{x, y, size * 0.12f, boxH}, sgc::Colorf{c.color[0], c.color[1], c.color[2], a});
	const sgc::Colorf col{c.color[0], c.color[1], c.color[2], a};
	s.drawTextInRect(sgc::Rectf{x + size * 0.4f, y + size * 0.15f, boxW - size * 0.6f, size * 1.4f}, c.text, col, size, Align::Left,
	                 Screen::TextAlignV::Middle, 4.0f, 2.0f);
	if (!c.sub.empty())
	{
		s.drawTextInRect(sgc::Rectf{x + size * 0.4f, y + size * 1.55f, boxW - size * 0.6f, size * 1.2f}, c.sub, sgc::Colorf{col.r, col.g, col.b, a * 0.8f},
		                 size * 0.62f, Align::Left, Screen::TextAlignV::Middle, 4.0f, 2.0f);
	}
}

/// @brief 画面の (x, y) を中心に、大きな文字を 1 行置く。副題があれば下に小さく置く。
inline void drawTitleCard(Screen& s, const TitleCard& c, float a)
{
	const float w = static_cast<float>(s.width()), h = static_cast<float>(s.height());
	const float size = h * c.size;
	const float boxW = w * 0.9f;
	const float boxH = size * 1.6f;
	const float cy = c.sub.empty() ? c.y * h : c.y * h - size * 0.4f;
	if (c.panel > 0.0f) { s.drawRect(sgc::Rectf{0.0f, cy - boxH * 0.7f, w, boxH * 1.4f + (c.sub.empty() ? 0.0f : size * 0.9f)}, sgc::Colorf{0.0f, 0.0f, 0.0f, c.panel * a}); }
	const sgc::Colorf col{c.color[0], c.color[1], c.color[2], a};
	s.drawTextInRect(sgc::Rectf{c.x * w - boxW * 0.5f, cy - boxH * 0.5f, boxW, boxH}, c.text, col, size, Align::Center,
	                 Screen::TextAlignV::Middle, 8.0f, 2.0f);
	if (!c.sub.empty())
	{
		s.drawTextInRect(sgc::Rectf{c.x * w - boxW * 0.5f, cy + boxH * 0.45f, boxW, size * 0.9f}, c.sub, sgc::Colorf{col.r, col.g, col.b, a * 0.85f},
		                 size * 0.5f, Align::Center, Screen::TextAlignV::Middle, 8.0f, 2.0f);
	}
}

}  // namespace title_detail

/// @brief 映像の時間 tl に出ているタイトルをすべて描く。暗転の後に重ね、黒から浮かび上がる題も表示する。
inline void drawTitles(Screen& s, const Film& f, float tl)
{
	for (const TitleCard& c : f.edit.titles)
	{
		const float a = titleAlpha(c, tl);
		if (a <= 0.0f) { continue; }
		if (c.kind == TitleKind::LowerThird) { title_detail::drawLowerThird(s, c, a); }
		else { title_detail::drawTitleCard(s, c, a); }
	}
}

}  // namespace mitiru::film::edit
