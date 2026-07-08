/// @file chapter_hud.hpp
/// @brief 全章共通の HUD 部品と配色。章ラベル (左上) と操作帯 (下端) の書式をそろえる
/// 使い方: draw() 内で chapterTitle(s, "3D シーン"); chapterControls(s, "矢印: うごかす");
/// 規約: docs/EXAMPLE_STANDARD.md「画面フォーマット」「テーマ」節

#pragma once

#include <mitiru.hpp>

// 全章共通の白系 (Apple-light) パレット。背景は紙白、文字は濃色、アクセントは彩度高め。
// glow / 星空のような発光表現だけは kCard のダークカード内で見せる (白地では光らない)。
namespace theme
{
	constexpr mitiru::Color kPaper  = mitiru::hex(0xF5F7FA);   // ページ背景 (紙白)
	constexpr mitiru::Color kInk    = mitiru::hex(0x1D1D1F);   // 本文の濃色
	constexpr mitiru::Color kSubtle = mitiru::hex(0x6B7280);   // 補助ラベル
	constexpr mitiru::Color kFrame  = mitiru::hex(0xD8DEE9);   // 枠線・レール
	constexpr mitiru::Color kBlue   = mitiru::hex(0x0A84FF);
	constexpr mitiru::Color kPink   = mitiru::hex(0xE8338A);
	constexpr mitiru::Color kOrange = mitiru::hex(0xFF9500);
	constexpr mitiru::Color kGreen  = mitiru::hex(0x1FA654);
	constexpr mitiru::Color kRed    = mitiru::hex(0xE5484D);
	constexpr mitiru::Color kAmber  = mitiru::hex(0xDB9E00);   // 白地で読める黄の代替
	constexpr mitiru::Color kCard   = mitiru::hex(0x141A2A);   // ダークカード (glow / 星空用)
	constexpr mitiru::Color kCardInk = mitiru::hex(0x9AA4B8);  // ダークカード内のラベル
}

// 左上の章ラベル (日本語名のみ)。薄グレー半透明の下地バーで絵から浮かせる。
inline void chapterTitle(mitiru::Screen& s, const char* nameJp)
{
	const float w = s.measureText(nameJp, 20.0f).x;
	const mitiru::Rect bar{16.0f, 14.0f, w + 28.0f, 36.0f};
	s.drawRect(bar, mitiru::rgba(226, 231, 240, 215));
	s.drawTextInRect(bar, nameJp, theme::kInk, 20.0f,
	                 mitiru::Screen::TextAlignH::Center, mitiru::Screen::TextAlignV::Middle);
}

// 下端の全幅操作帯。日本語 18px 中央揃え (項目の区切りは全角スペース)。
inline void chapterControls(mitiru::Screen& s, const char* textJp)
{
	const mitiru::Rect band{0.0f, 720.0f - 34.0f, 1280.0f, 34.0f};
	s.drawRect(band, mitiru::rgba(226, 231, 240, 225));
	s.drawTextInRect(band, textJp, mitiru::hex(0x3A4048), 18.0f,
	                 mitiru::Screen::TextAlignH::Center, mitiru::Screen::TextAlignV::Middle);
}
