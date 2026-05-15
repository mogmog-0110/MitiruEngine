#pragma once

/// @file SdfTextRendererCpu.hpp
/// @brief ソフトウェアScreen用SDFテキスト描画メソッド（SdfTextRendererのインライン実装）

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/sdf/SdfEffects.hpp>
#include <mitiru/render/sdf/SdfFontAtlas.hpp>
#include <mitiru/render/sdf/Utf8Utils.hpp>

namespace mitiru::render
{

namespace sdf_cpu_detail
{

/// @brief アトラスからバイリニア補間でSDF値をサンプリングする
/// @param pixels アトラスピクセルデータ（RGBA8）
/// @param w アトラス幅
/// @param h アトラス高さ
/// @param x サンプル位置X（浮動小数）
/// @param y サンプル位置Y（浮動小数）
/// @return SDF距離値（0-255）
[[nodiscard]] inline float sampleAtlasBilinear(
	const std::vector<std::uint8_t>& pixels,
	int w, int h, float x, float y) noexcept
{
	const float fx = x - 0.5f;
	const float fy = y - 0.5f;

	const int ix = static_cast<int>(std::floor(fx));
	const int iy = static_cast<int>(std::floor(fy));

	const float fracX = fx - static_cast<float>(ix);
	const float fracY = fy - static_cast<float>(iy);

	const auto sample = [&](int sx, int sy) -> float
	{
		const int cx = std::clamp(sx, 0, w - 1);
		const int cy = std::clamp(sy, 0, h - 1);
		const auto idx = static_cast<std::size_t>((cy * w + cx) * 4 + 3); // alpha channel
		if (idx < pixels.size())
		{
			return static_cast<float>(pixels[idx]);
		}
		return 0.0f;
	};

	const float s00 = sample(ix, iy);
	const float s10 = sample(ix + 1, iy);
	const float s01 = sample(ix, iy + 1);
	const float s11 = sample(ix + 1, iy + 1);

	const float top = s00 + (s10 - s00) * fracX;
	const float bottom = s01 + (s11 - s01) * fracX;

	return top + (bottom - top) * fracY;
}

/// @brief ソフトウェアレンダリングで1グリフを描画する
template <typename ScreenType>
inline void renderGlyphSoftware(ScreenType& screen,
	const SdfGlyphInfo& gi,
	const std::vector<std::uint8_t>& atlasPixels,
	int atlasW, int atlasH,
	float cursorX, float baseY, float displayScale,
	const sgc::Colorf& color,
	float threshold, float extraSmoothing)
{
	const float gx = cursorX + gi.xoff * displayScale;
	const float gy = baseY + gi.yoff * displayScale;
	const float gw = static_cast<float>(gi.width()) * displayScale;
	const float gh = static_cast<float>(gi.height()) * displayScale;

	// 表示上の各ピクセルについてSDF値をサンプリング
	const int startX = static_cast<int>(gx);
	const int startY = static_cast<int>(gy);
	const int endX = static_cast<int>(std::ceil(gx + gw));
	const int endY = static_cast<int>(std::ceil(gy + gh));

	const float invDisplayScale = 1.0f / displayScale;

	// 適応的スムージング幅（表示スケールに基づく）
	const float baseSmoothing = 0.5f / static_cast<float>(gi.width());
	const float smoothingWidth = std::max(baseSmoothing, extraSmoothing);

	for (int py = startY; py < endY; ++py)
	{
		for (int px = startX; px < endX; ++px)
		{
			// 表示ピクセル → アトラス座標にマッピング
			const float localX = (static_cast<float>(px) + 0.5f - gx) * invDisplayScale;
			const float localY = (static_cast<float>(py) + 0.5f - gy) * invDisplayScale;

			// アトラス上の対応座標
			const float atlasX = static_cast<float>(gi.x0) + localX;
			const float atlasY = static_cast<float>(gi.y0) + localY;

			// バイリニアサンプリング
			const float dist = sampleAtlasBilinear(atlasPixels, atlasW,
				atlasH, atlasX, atlasY);

			// SDF距離を[0,1]に正規化
			const float normalizedDist = dist / 255.0f;

			// smoothstep でアルファを計算
			const float alpha = sdf_detail::smoothstep(
				threshold - smoothingWidth,
				threshold + smoothingWidth,
				normalizedDist);

			if (alpha > 0.01f)
			{
				const sgc::Colorf blendedColor{
					color.r, color.g, color.b,
					color.a * alpha
				};
				screen.drawRect(
					sgc::Rectf{
						static_cast<float>(px),
						static_cast<float>(py),
						1.0f, 1.0f},
					blendedColor);
			}
		}
	}
}

} // namespace sdf_cpu_detail

} // namespace mitiru::render
