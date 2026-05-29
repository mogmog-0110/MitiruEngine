#pragma once

/// @file NineSlice.hpp
/// @brief 9-slice 描画ユーティリティ (会話枠 / メニュー枠の伸縮描画)

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/Texture.hpp>

#include <algorithm>
#include <array>

namespace mitiru::render
{

/// @brief 9-slice の 1 領域 (source rect → destination rect の対応)。
struct NineSlicePiece
{
	sgc::Rectf src;
	sgc::Rectf dst;
	bool       visible;  ///< 領域寸法が 0 以下なら描画スキップを示す
};

/// @brief sheet 寸法 + dst + corner 厚みから 9 領域の src/dst rect を算出する純関数。
/// 領域順: TL, T, TR, L, C, R, BL, B, BR。draw 不要な領域は visible=false。
[[nodiscard]] inline std::array<NineSlicePiece, 9> computeNineSliceRects(
	float sw, float sh, const sgc::Rectf& dst, float L, float R, float T, float B) noexcept
{
	const float dx = dst.x(), dy = dst.y(), dw = dst.width(), dh = dst.height();
	const float scx = std::max(0.0f, sw - L - R);
	const float scy = std::max(0.0f, sh - T - B);
	const float dcx = std::max(0.0f, dw - L - R);
	const float dcy = std::max(0.0f, dh - T - B);

	auto piece = [](sgc::Rectf s, sgc::Rectf d, bool ok) {
		return NineSlicePiece{s, d, ok && s.width() > 0.0f && s.height() > 0.0f
		                                && d.width() > 0.0f && d.height() > 0.0f};
	};
	// 寸法 0 (corner 無し / 中央無し) でも visible=false で安全に返す。
	std::array<NineSlicePiece, 9> r{};
	r[0] = piece({0,       0,       L,   T  }, {dx,           dy,           L,   T  }, L>0   && T>0);
	r[1] = piece({L,       0,       scx, T  }, {dx + L,       dy,           dcx, T  }, dcx>0 && T>0);
	r[2] = piece({sw - R,  0,       R,   T  }, {dx + dw - R,  dy,           R,   T  }, R>0   && T>0);
	r[3] = piece({0,       T,       L,   scy}, {dx,           dy + T,       L,   dcy}, L>0   && dcy>0);
	r[4] = piece({L,       T,       scx, scy}, {dx + L,       dy + T,       dcx, dcy}, dcx>0 && dcy>0);
	r[5] = piece({sw - R,  T,       R,   scy}, {dx + dw - R,  dy + T,       R,   dcy}, R>0   && dcy>0);
	r[6] = piece({0,       sh - B,  L,   B  }, {dx,           dy + dh - B,  L,   B  }, L>0   && B>0);
	r[7] = piece({L,       sh - B,  scx, B  }, {dx + L,       dy + dh - B,  dcx, B  }, dcx>0 && B>0);
	r[8] = piece({sw - R,  sh - B,  R,   B  }, {dx + dw - R,  dy + dh - B,  R,   B  }, R>0   && B>0);
	return r;
}

/// @brief 9-slice 描画。sheet を 4 隅 + 4 辺 + 中央に分け、隅は固定・辺/中央は dst の比率で伸縮。
inline void draw9Slice(Screen& screen, const Texture& sheet, const sgc::Rectf& dst,
                       float L, float R, float T, float B,
                       const sgc::Colorf& tint = {1.0f, 1.0f, 1.0f, 1.0f})
{
	const float sw = static_cast<float>(sheet.width());
	const float sh = static_cast<float>(sheet.height());
	if (sw <= 0.0f || sh <= 0.0f) { return; }

	const auto pieces = computeNineSliceRects(sw, sh, dst, L, R, T, B);
	for (const auto& p : pieces)
	{
		if (p.visible) { screen.drawSprite(sheet, p.dst, p.src, tint); }
	}
}

/// @brief 4 辺均一の corner 厚み版 (L=R=T=B=cornerPx)。
inline void draw9Slice(Screen& screen, const Texture& sheet, const sgc::Rectf& dst,
                       float cornerPx,
                       const sgc::Colorf& tint = {1.0f, 1.0f, 1.0f, 1.0f})
{
	draw9Slice(screen, sheet, dst, cornerPx, cornerPx, cornerPx, cornerPx, tint);
}

}  // namespace mitiru::render
