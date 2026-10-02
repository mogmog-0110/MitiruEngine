#pragma once

/// @file ScreenFont.hpp
/// @brief 1 書体を Screen の文字描画 (drawTextInRect / drawTextClipped / drawTextWrapped) につなぐ。
/// @details 字形選択には HarfBuzz を使う。
/// kHintedMaxPixelSize 以下を拡大や回転なしで画素に合わせるときは、FreeType でヒンティングした濃さ
/// (CoverageGlyphCache) を 1:1 で貼る。
/// 距離場では縮小時に線が細くなり、12 から 16 px の和文が読みにくくなるため。
/// それ以外 (大きい・拡大・回転・画面が論理解像度と違う倍率) は MTSDF アトラス (DistanceFieldCache) から描く。
/// テクスチャ描画を持たない描画先には、濃さを直接重ねる。
/// 計測と描画には同じ字形選択の結果を使う。

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/core/Screen.hpp>
#include <mitiru/render/RenderPipeline2D.hpp>
#include <mitiru/text/CoverageGlyphCache.hpp>
#include <mitiru/text/DistanceFieldCache.hpp>
#include <mitiru/text/FontFace.hpp>

namespace mitiru::text
{

class ScreenFont
{
public:
	/// 画素に合わせて塗る上限の高さ。これを超えると距離場でも線の太さを保てる。
	static constexpr float kHintedMaxPixelSize = 16.0f;

	explicit ScreenFont(std::unique_ptr<FontFace> face)
		: m_face(std::move(face))
	{
		m_placed.reserve(256);
	}

	ScreenFont(const ScreenFont&) = delete;
	ScreenFont& operator=(const ScreenFont&) = delete;

	/// @brief screen の既定の文字描画にこの書体を使う。
	/// この書体は screen より長く生存させる。
	void attachTo(Screen& screen) noexcept
	{
		screen.setTrueTypeFont(this, &drawThunk, &measureThunk);
	}

	[[nodiscard]] FontFace& face() noexcept { return *m_face; }

	/// @brief 1 行を描く。
	/// (x, y) は行の左上。
	void draw(Screen& screen, std::string_view text, float x, float y, float pixelSize,
	          const sgc::Colorf& color)
	{
		if (text.empty() || pixelSize <= 0.0f) { return; }
		const float baseline = y + m_face->metrics(pixelSize).ascent;
		const auto glyphs = m_face->shape(text, pixelSize);
		if (screen.softwareTextPath())
		{
			blitCoverage(screen, glyphs, x, std::round(baseline), pixelSize, color);
		}
		else if (pixelSize <= kHintedMaxPixelSize && isPixelExact(screen))
		{
			drawHinted(screen, glyphs, x, baseline, pixelSize, color);
		}
		else
		{
			drawDistanceField(screen, glyphs, x, std::round(baseline), pixelSize, color);
		}
	}

	/// @brief 幅は字形選択後の字送りの合計、高さは行の高さ。
	[[nodiscard]] sgc::Vec2f measure(std::string_view text, float pixelSize)
	{
		return {m_face->measureText(text, pixelSize), m_face->metrics(pixelSize).lineHeight};
	}

	[[nodiscard]] const DistanceFieldCache& distanceFields() const noexcept { return m_fields; }
	[[nodiscard]] const CoverageGlyphCache& coverage() const noexcept { return m_coverage; }

private:
	struct Placed
	{
		sgc::Rectf dst;
		sgc::Rectf src;
	};

	static void drawThunk(void* self, Screen& screen, float x, float y, std::string_view text,
	                      float pixelSize, const sgc::Colorf& color)
	{
		static_cast<ScreenFont*>(self)->draw(screen, text, x, y, pixelSize, color);
	}

	static sgc::Vec2f measureThunk(void* self, std::string_view text, float pixelSize)
	{
		return static_cast<ScreenFont*>(self)->measure(text, pixelSize);
	}

	/// @brief 論理座標の 1 画素がバックバッファの 1 画素に重なるかを返す。
	/// 平行移動だけを許す。
	[[nodiscard]] static bool isPixelExact(const Screen& screen)
	{
		const render::RenderPipeline2D* pipeline = screen.pipeline();
		if (pipeline == nullptr) { return false; }
		const auto near1 = [](float v) { return std::abs(v - 1.0f) < 1e-3f; };
		const auto near0 = [](float v) { return std::abs(v) < 1e-3f; };
		if (!near1(pipeline->viewportWidth() / static_cast<float>(screen.width())) ||
		    !near1(pipeline->viewportHeight() / static_cast<float>(screen.height())))
		{
			return false;
		}
		const sgc::Vec2f o = screen.applyTransform(sgc::Vec2f{0.0f, 0.0f});
		const sgc::Vec2f ex = screen.applyTransform(sgc::Vec2f{1.0f, 0.0f});
		const sgc::Vec2f ey = screen.applyTransform(sgc::Vec2f{0.0f, 1.0f});
		return near1(ex.x - o.x) && near0(ex.y - o.y) && near0(ey.x - o.x) && near1(ey.y - o.y);
	}

	void drawDistanceField(Screen& screen, std::span<const ShapedGlyph> glyphs, float x,
	                       float baseline, float pixelSize, const sgc::Colorf& color)
	{
		render::RenderPipeline2D* pipeline = screen.pipeline();
		if (pipeline == nullptr) { return; }

		// 全字形を揃えてから GPU へ送り、同じ行の字形どうしで場所を取り合わないようにする。
		m_fields.tick();
		m_fields.prefetch(*m_face, glyphs);
		m_placed.clear();
		float pen = x;
		for (const ShapedGlyph& g : glyphs)
		{
			if (const DistanceFieldEntry* e = m_fields.get(*m_face, g.glyphId); e != nullptr && e->slot >= 0)
			{
				const float gx = pen + g.offsetX;
				const float gy = baseline + g.offsetY;
				m_placed.push_back(Placed{
					sgc::Rectf{gx + e->left * pixelSize, gy + e->top * pixelSize,
					           (e->right - e->left) * pixelSize, (e->bottom - e->top) * pixelSize},
					sgc::Rectf{static_cast<float>(e->x), static_cast<float>(e->y),
					           static_cast<float>(e->width), static_cast<float>(e->height)}});
			}
			pen += g.advanceX;
		}
		upload(screen, *pipeline, m_fields.texture(), m_fields.dirty(), DistanceFieldCache::kPixelRange);
		emit(screen, m_fields.texture(), color);
	}

	/// @brief ヒンティング済みの濃さを画素の格子に揃えて貼る。
	/// 縮小も補間もしない。
	void drawHinted(Screen& screen, std::span<const ShapedGlyph> glyphs, float x, float baseline,
	                float pixelSize, const sgc::Colorf& color)
	{
		render::RenderPipeline2D* pipeline = screen.pipeline();
		const int sizeKey = std::max(1, static_cast<int>(std::lround(pixelSize)));
		// 平行移動の端数を打ち消し、バックバッファ上の整数位置に置く。
		const sgc::Vec2f o = screen.applyTransform(sgc::Vec2f{0.0f, 0.0f});
		const float snappedBaseline = std::round(baseline + o.y) - o.y;

		// 行の途中でアトラスが破棄されたら、先に置いた字形も新しい位置から引き直す。
		// 1 行の字形がアトラスに収まらないほど多いときは引き直しても揃わないので、回数を限る。
		std::uint64_t resets = m_coverage.resets();
		placeHinted(glyphs, x, snappedBaseline, sizeKey, o.x);
		for (int retry = 0; retry < 2 && m_coverage.resets() != resets; ++retry)
		{
			resets = m_coverage.resets();
			placeHinted(glyphs, x, snappedBaseline, sizeKey, o.x);
		}
		upload(screen, *pipeline, m_coverage.texture(), m_coverage.dirty(), 0.0f);
		emit(screen, m_coverage.texture(), color);
	}

	void placeHinted(std::span<const ShapedGlyph> glyphs, float x, float baseline, int sizeKey, float shiftX)
	{
		m_placed.clear();
		float pen = x;
		for (const ShapedGlyph& g : glyphs)
		{
			// ペン位置の端数は 1/3 画素刻みで字形側へずらし、字間を字送りどおりに保つ。
			constexpr int kSteps = CoverageGlyphCache::kSubpixelSteps;
			const float penX = pen + g.offsetX + shiftX;
			float whole = std::floor(penX);
			int step = static_cast<int>(std::lround((penX - whole) * kSteps));
			if (step == kSteps) { whole += 1.0f; step = 0; }
			const CoverageEntry e = m_coverage.get(*m_face, g.glyphId, sizeKey, step);
			if (e.width > 0)
			{
				const float gx = whole - shiftX + static_cast<float>(e.left);
				const float gy = std::round(baseline + g.offsetY) + static_cast<float>(e.top);
				m_placed.push_back(Placed{
					sgc::Rectf{gx, gy, static_cast<float>(e.width), static_cast<float>(e.height)},
					sgc::Rectf{static_cast<float>(e.x), static_cast<float>(e.y),
					           static_cast<float>(e.width), static_cast<float>(e.height)}});
			}
			pen += g.advanceX;
		}
	}

	/// @brief アトラスの書き換えた行だけを GPU へ送る。
	/// 先に積んだ文字は、送る前の内容で描く。
	/// @param distanceRange 0 なら通常のテクスチャ、0 以外なら MTSDF として描く。
	static void upload(Screen& screen, render::RenderPipeline2D& pipeline, const render::Texture& atlas,
	                   DirtyRows& dirty, float distanceRange)
	{
		if (distanceRange > 0.0f) { pipeline.setDistanceFieldTexture(&atlas, distanceRange); }
		static_cast<void>(pipeline.ensureSpriteTexture(&atlas, atlas.width(), atlas.height(),
		                                               atlas.pixels().data()));
		int firstRow = 0;
		int rowCount = 0;
		if (!dirty.get(firstRow, rowCount)) { return; }
		screen.flushSpriteBatch();
		pipeline.updateSpriteTextureRows(&atlas, firstRow, rowCount, atlas.pixels().data());
		dirty.clear();
	}

	void emit(Screen& screen, const render::Texture& atlas, const sgc::Colorf& color) const
	{
		for (const Placed& p : m_placed) { screen.drawSprite(atlas, p.dst, p.src, color); }
	}

	void blitCoverage(Screen& screen, std::span<const ShapedGlyph> glyphs, float x,
	                  float baseline, float pixelSize, const sgc::Colorf& color)
	{
		const int sizeKey = std::max(1, static_cast<int>(std::lround(pixelSize)));
		const render::Texture& atlas = m_coverage.texture();
		float pen = x;
		for (const ShapedGlyph& g : glyphs)
		{
			const CoverageEntry e = m_coverage.get(*m_face, g.glyphId, sizeKey);
			if (e.width > 0)
			{
				const float gx = std::round(pen + g.offsetX) + static_cast<float>(e.left);
				const float gy = std::round(baseline + g.offsetY) + static_cast<float>(e.top);
				screen.blitAtlasGlyph(atlas,
					sgc::Rectf{gx, gy, static_cast<float>(e.width), static_cast<float>(e.height)},
					sgc::Rectf{static_cast<float>(e.x), static_cast<float>(e.y),
					           static_cast<float>(e.width), static_cast<float>(e.height)},
					color);
			}
			pen += g.advanceX;
		}
	}

	std::unique_ptr<FontFace> m_face;
	DistanceFieldCache m_fields;
	CoverageGlyphCache m_coverage;
	std::vector<Placed> m_placed;
};

} // namespace mitiru::text
