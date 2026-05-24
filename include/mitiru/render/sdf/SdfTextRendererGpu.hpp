#pragma once

/// @file SdfTextRendererGpu.hpp
/// @brief GPU SpriteBatch用SDFテキスト描画メソッド（SdfTextRendererのインライン実装）

#include <cmath>
#include <cstdint>
#include <string_view>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/sdf/SdfEffects.hpp>
#include <mitiru/render/sdf/SdfFontAtlas.hpp>
#include <mitiru/render/sdf/Utf8Utils.hpp>

namespace mitiru::render
{

namespace sdf_gpu_detail
{

/// @brief SpriteBatchにSDFテキストを描画する（内部実装）
/// @details At small font sizes (e.g. 16px on a 32px SDF atlas → 0.5x display
///          scale) sub-pixel glyph positions blur each character via bilinear
///          texture filtering. We snap destination quad x/y/w/h to integer
///          pixels so glyphs land on whole texels — this removes the
///          "Inspector text looks smudged" artifact at small sizes without
///          affecting layout / advance accounting.
template <typename BatchType>
inline void drawText(const SdfFontAtlas& atlas, BatchType& batch, std::string_view text,
	float x, float y, float fontSize, const sgc::Colorf& color)
{
	const float displayScale = fontSize / atlas.sdfPixelSize();
	const float invAtlasW = 1.0f / static_cast<float>(atlas.atlasWidth());
	const float invAtlasH = 1.0f / static_cast<float>(atlas.atlasHeight());
	float cursorX = x;
	std::uint32_t prevCp = 0;

	sdf_detail::Utf8Decoder dec(text);
	while (dec.hasNext())
	{
		const std::uint32_t cp = dec.next();

		if (prevCp != 0)
		{
			cursorX += atlas.kerning(prevCp, cp, fontSize);
		}

		const auto* gi = atlas.findGlyph(cp);
		if (gi != nullptr && gi->width() > 0 && gi->height() > 0)
		{
			// Compute the exact float dest, then snap to whole pixels for
			// the actual quad. The advance below still uses float so
			// glyph spacing accumulates without rounding error.
			const float gxF = cursorX + gi->xoff * displayScale;
			const float gyF = y + gi->yoff * displayScale;
			const float gwF = static_cast<float>(gi->width())  * displayScale;
			const float ghF = static_cast<float>(gi->height()) * displayScale;

			const float gx = std::floor(gxF + 0.5f);
			const float gy = std::floor(gyF + 0.5f);
			const float gw = std::floor(gwF + 0.5f);
			const float gh = std::floor(ghF + 0.5f);

			const sgc::Rectf destRect{gx, gy, gw, gh};
			const sgc::Rectf srcRect{
				static_cast<float>(gi->x0) * invAtlasW,
				static_cast<float>(gi->y0) * invAtlasH,
				static_cast<float>(gi->width()) * invAtlasW,
				static_cast<float>(gi->height()) * invAtlasH
			};

			batch.drawSprite(0, destRect, srcRect, color);
		}

		if (gi != nullptr)
		{
			cursorX += gi->xadvance * displayScale;
		}
		prevCp = cp;
	}
}

/// @brief 拡大クワッドでテキストを描画する（アウトライン・グロー用、内部実装）
template <typename BatchType>
inline void drawTextExpanded(const SdfFontAtlas& atlas, BatchType& batch, std::string_view text,
	float x, float y, float fontSize,
	const sgc::Colorf& color, float expand)
{
	const float displayScale = fontSize / atlas.sdfPixelSize();
	const float invAtlasW = 1.0f / static_cast<float>(atlas.atlasWidth());
	const float invAtlasH = 1.0f / static_cast<float>(atlas.atlasHeight());
	float cursorX = x;
	std::uint32_t prevCp = 0;

	sdf_detail::Utf8Decoder dec(text);
	while (dec.hasNext())
	{
		const std::uint32_t cp = dec.next();

		if (prevCp != 0)
		{
			cursorX += atlas.kerning(prevCp, cp, fontSize);
		}

		const auto* gi = atlas.findGlyph(cp);
		if (gi != nullptr && gi->width() > 0 && gi->height() > 0)
		{
			const float gx = cursorX + gi->xoff * displayScale - expand;
			const float gy = y + gi->yoff * displayScale - expand;
			const float gw = static_cast<float>(gi->width()) * displayScale + expand * 2.0f;
			const float gh = static_cast<float>(gi->height()) * displayScale + expand * 2.0f;

			const sgc::Rectf destRect{gx, gy, gw, gh};
			const sgc::Rectf srcRect{
				static_cast<float>(gi->x0) * invAtlasW,
				static_cast<float>(gi->y0) * invAtlasH,
				static_cast<float>(gi->width()) * invAtlasW,
				static_cast<float>(gi->height()) * invAtlasH
			};

			batch.drawSprite(0, destRect, srcRect, color);
		}

		if (gi != nullptr)
		{
			cursorX += gi->xadvance * displayScale;
		}
		prevCp = cp;
	}
}

} // namespace sdf_gpu_detail

} // namespace mitiru::render
