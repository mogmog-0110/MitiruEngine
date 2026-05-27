#pragma once
/// @file PixelText.hpp
/// @brief 美咲フォント(8x8 ドット日本語)を Screen にピクセルパーフェクトに描く。
/// @details 点灯ピクセルを整数サイズの矩形として drawRect で描く（ADR 0009 以降は
///          バッチ化されるため数十文字でも 1 submit に合流）。SDF と違いアンチエイリアス
///          されない真のドット文字。`drawText` 禁止規約の sanctioned な代替 API。
///
/// @code
/// using namespace mitiru::render::pixel;
/// drawPixelText(screen, 16.0f, 16.0f, u8"スコア 1200", 3, sgc::Colorf{1,1,1,1});
/// drawPixelTextInRect(screen, sgc::Rectf{20,20,300,40}, u8"はなしかける", 2,
///                     sgc::Colorf{1,1,1,1}, PixelAlign::Center);
/// @endcode

#include <string_view>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/pixel/PixelFont.hpp>

namespace mitiru::render::pixel
{

/// @brief 矩形内の水平配置
enum class PixelAlign { Left, Center, Right };

/// @brief ピクセル文字列を (x,y) を左上として描く。
/// @param scale 整数拡大率（1=8px。HUD は 2〜4 が目安）
inline void drawPixelText(Screen& screen, float x, float y,
                          std::string_view text, int scale, const sgc::Colorf& color)
{
	const int ox = static_cast<int>(x);
	const int oy = static_cast<int>(y);
	forEachPixel(text, ox, oy, scale,
		[&](int px, int py, int size)
		{
			screen.drawRect(
				sgc::Rectf{static_cast<float>(px), static_cast<float>(py),
				           static_cast<float>(size), static_cast<float>(size)},
				color);
		});
}

/// @brief 矩形内にピクセル文字列を描く（矩形でクリップ、水平配置指定）。
/// @details 縦は矩形内で中央寄せ。横は align に従う。矩形をはみ出す分はクリップされる。
inline void drawPixelTextInRect(Screen& screen, const sgc::Rectf& rect,
                                std::string_view text, int scale, const sgc::Colorf& color,
                                PixelAlign align = PixelAlign::Left)
{
	if (scale < 1) scale = 1;
	const PixelSize sz = measurePixelText(text, scale);

	float tx = rect.x();
	if (align == PixelAlign::Center) tx = rect.x() + (rect.width() - static_cast<float>(sz.w)) * 0.5f;
	else if (align == PixelAlign::Right) tx = rect.x() + (rect.width() - static_cast<float>(sz.w));
	const float ty = rect.y() + (rect.height() - static_cast<float>(sz.h)) * 0.5f;

	screen.pushClipRect(rect);
	drawPixelText(screen, tx, ty, text, scale, color);
	screen.popClipRect();
}

} // namespace mitiru::render::pixel
