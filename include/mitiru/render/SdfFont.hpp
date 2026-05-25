#pragma once

/// @file SdfFont.hpp
/// @brief Signed Distance Field フォントレンダラー
/// @details stb_truetypeのSDF生成機能を使用し、任意サイズで滑らかなテキスト描画を提供する。
///          アウトライン、シャドウ、グロー等のエフェクトにも対応する。
///          ソフトウェアScreen描画とGPU SpriteBatch描画の両方で動作する。

#include <mitiru/render/sdf/Utf8Utils.hpp>
#include <mitiru/render/sdf/SdfEffects.hpp>
#include <mitiru/render/sdf/SdfFontAtlas.hpp>
#include <mitiru/render/sdf/SdfTextMeasure.hpp>
#include <mitiru/render/sdf/SdfTextRendererGpu.hpp>
#include <mitiru/render/sdf/SdfTextRendererCpu.hpp>

#include <sgc/math/Vec2.hpp>

namespace mitiru::render
{

// ── SDF テキストレンダラー ─────────────────────────────────────

/// @brief SDFフォントアトラスを使用するテキストレンダラー
/// @details SDFの距離情報を利用して、スムーズなエッジのテキストを描画する。
///          GPU SpriteBatch（テクスチャ付きクワッド）とソフトウェアScreen（ピクセル単位）
///          の両方で動作する。
///
/// @code
/// SdfFontAtlas atlas(ttfData, 32.0f);
/// atlas.addAsciiRange();
/// atlas.buildAtlas();
///
/// SdfTextRenderer renderer(atlas);
///
/// // 基本描画
/// renderer.drawText(screen, "Hello", 10, 10, 24.0f, sgc::Colorf::white());
///
/// // アウトライン付き
/// renderer.drawTextWithOutline(screen, "Outlined", 10, 50, 32.0f,
///     sgc::Colorf::white(), sgc::Colorf::black(), 0.12f);
///
/// // エフェクト複合
/// SdfTextEffect fx;
/// fx.outline = {true, {0,0,0,1}, 0.1f};
/// fx.shadow = {true, {0,0,0,0.5f}, 2, 2, 0.15f};
/// renderer.drawTextWithEffect(screen, "Fancy", 10, 100, 48.0f,
///     sgc::Colorf::white(), fx);
/// @endcode
class SdfTextRenderer
{
	const SdfFontAtlas* m_atlas = nullptr; ///< 参照先アトラス（非所有）

public:
	/// @brief デフォルトコンストラクタ
	SdfTextRenderer() = default;

	/// @brief アトラスを指定して構築する
	/// @param atlas SDFフォントアトラス（寿命はレンダラーより長いこと）
	explicit SdfTextRenderer(const SdfFontAtlas& atlas) noexcept
		: m_atlas(&atlas)
	{
	}

	/// @brief 使用するアトラスを変更する
	/// @param atlas SDFフォントアトラス
	void setAtlas(const SdfFontAtlas& atlas) noexcept
	{
		m_atlas = &atlas;
	}

	/// @brief アトラスが設定済みかつ有効か
	[[nodiscard]] bool ready() const noexcept
	{
		return m_atlas != nullptr && m_atlas->valid();
	}

	// ── テキスト計測 ──────────────────────────────────────

	/// @brief テキストの描画サイズを計測する
	/// @param text UTF-8テキスト
	/// @param fontSize 表示フォントサイズ
	/// @return テキストサイズ
	[[nodiscard]] SdfTextSize measureText(std::string_view text, float fontSize) const
	{
		if (!ready())
		{
			return {};
		}

		const float displayScale = fontSize / m_atlas->sdfPixelSize();
		float cursorX = 0.0f;
		float maxHeight = 0.0f;
		std::uint32_t prevCp = 0;

		sdf_detail::Utf8Decoder dec(text);
		while (dec.hasNext())
		{
			const std::uint32_t cp = dec.next();

			if (prevCp != 0)
			{
				cursorX += m_atlas->kerning(prevCp, cp, fontSize);
			}

			const auto* gi = m_atlas->findGlyph(cp);
			if (gi != nullptr)
			{
				cursorX += gi->xadvance * displayScale;
				const float glyphBottom = (-gi->yoff + gi->height()) * displayScale;
				maxHeight = std::max(maxHeight, glyphBottom);
			}
			prevCp = cp;
		}

		const float lineH = m_atlas->metrics().lineHeight * displayScale;
		return {cursorX, std::max(lineH, maxHeight)};
	}

	// ── ワードラップ ────────────────────────────────────────

	/// @brief テキストを指定幅でワードラップする
	/// @param text UTF-8テキスト
	/// @param fontSize 表示フォントサイズ
	/// @param maxWidth 最大幅（ピクセル）
	/// @return 行ごとの情報
	[[nodiscard]] std::vector<SdfWrappedLine> wrapText(
		std::string_view text, float fontSize, float maxWidth) const
	{
		if (!ready() || text.empty())
		{
			return {};
		}

		std::vector<SdfWrappedLine> lines;
		const float displayScale = fontSize / m_atlas->sdfPixelSize();

		std::size_t lineStart = 0;
		std::size_t lastSpacePos = 0; // バイト位置
		float lineWidth = 0.0f;
		std::uint32_t prevCp = 0;

		// コードポイントとバイト位置を同時に追跡する
		const char* ptr = text.data();
		const char* end = text.data() + text.size();

		while (ptr < end)
		{
			const std::size_t charStart = static_cast<std::size_t>(ptr - text.data());

			// 1文字デコード
			sdf_detail::Utf8Decoder dec(ptr, static_cast<std::size_t>(end - ptr));
			const std::uint32_t cp = dec.next();

			// デコードされたバイト数を計算
			std::size_t charBytes = 1;
			const auto b0 = static_cast<std::uint8_t>(*ptr);
			if (b0 >= 0xF0) charBytes = 4;
			else if (b0 >= 0xE0) charBytes = 3;
			else if (b0 >= 0xC0) charBytes = 2;
			ptr += charBytes;

			const std::size_t charEnd = charStart + charBytes;

			// 改行処理
			if (cp == '\n')
			{
				lines.push_back({text.substr(lineStart, charStart - lineStart), lineWidth});
				lineStart = charEnd;
				lastSpacePos = charEnd;
				lineWidth = 0.0f;
				prevCp = 0;
				continue;
			}

			if (cp == ' ')
			{
				lastSpacePos = charStart;
			}

			float advance = 0.0f;
			if (prevCp != 0)
			{
				advance += m_atlas->kerning(prevCp, cp, fontSize);
			}
			const auto* gi = m_atlas->findGlyph(cp);
			if (gi != nullptr)
			{
				advance += gi->xadvance * displayScale;
			}

			if (lineWidth + advance > maxWidth && lineWidth > 0.0f)
			{
				// 折り返し
				if (lastSpacePos > lineStart)
				{
					const auto lineLen = lastSpacePos - lineStart;
					const auto lineText = text.substr(lineStart, lineLen);
					lines.push_back({lineText, measureText(lineText, fontSize).width});
					lineStart = lastSpacePos + 1; // スペースをスキップ
				}
				else
				{
					const auto lineLen = charStart - lineStart;
					const auto lineText = text.substr(lineStart, lineLen);
					lines.push_back({lineText, lineWidth});
					lineStart = charStart;
				}
				lastSpacePos = lineStart;
				lineWidth = 0.0f;
				prevCp = 0;
			}

			lineWidth += advance;
			prevCp = cp;
		}

		// 最後の行
		if (lineStart < text.size())
		{
			lines.push_back({text.substr(lineStart), lineWidth});
		}

		return lines;
	}

	// ── GPU SpriteBatch 描画 ─────────────────────────────────

	/// @brief SpriteBatchにSDFテキストを描画する
	/// @tparam BatchType SpriteBatch互換型
	/// @param batch スプライトバッチ
	/// @param text UTF-8テキスト
	/// @param x 描画開始X座標
	/// @param y 描画開始Y座標（ベースライン上端）
	/// @param fontSize 表示フォントサイズ
	/// @param color テキスト色
	template <typename BatchType>
	void drawText(BatchType& batch, std::string_view text,
		float x, float y, float fontSize, const sgc::Colorf& color) const
	{
		if (!ready())
		{
			return;
		}
		sdf_gpu_detail::drawText(*m_atlas, batch, text, x, y, fontSize, color);
	}

	/// @brief SpriteBatchにアウトライン付きテキストを描画する
	/// @tparam BatchType SpriteBatch互換型
	template <typename BatchType>
	void drawTextWithOutline(BatchType& batch, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& textColor,
		const sgc::Colorf& outlineColor,
		float outlineWidth = 0.1f) const
	{
		// アウトライン（わずかに大きいクワッドを下地に描画）
		const float expand = outlineWidth * fontSize * 0.5f;
		drawTextExpanded(batch, text, x, y, fontSize, outlineColor, expand);

		// 本体テキスト
		drawText(batch, text, x, y, fontSize, textColor);
	}

	/// @brief SpriteBatchにシャドウ付きテキストを描画する
	/// @tparam BatchType SpriteBatch互換型
	template <typename BatchType>
	void drawTextWithShadow(BatchType& batch, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& textColor,
		const sgc::Colorf& shadowColor,
		const sgc::Vec2f& shadowOffset = {2.0f, 2.0f}) const
	{
		// シャドウ（オフセット位置に描画）
		drawText(batch, text, x + shadowOffset.x, y + shadowOffset.y,
			fontSize, shadowColor);

		// 本体テキスト
		drawText(batch, text, x, y, fontSize, textColor);
	}

	/// @brief SpriteBatchにグロー付きテキストを描画する
	/// @tparam BatchType SpriteBatch互換型
	template <typename BatchType>
	void drawTextWithGlow(BatchType& batch, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& textColor,
		const sgc::Colorf& glowColor,
		float glowRadius = 0.2f) const
	{
		// グロー（拡大したクワッドで下地描画、GPUシェーダーで広い smoothstep）
		const float expand = glowRadius * fontSize;
		drawTextExpanded(batch, text, x, y, fontSize, glowColor, expand);

		// 本体テキスト
		drawText(batch, text, x, y, fontSize, textColor);
	}

	/// @brief SpriteBatchに複合エフェクト付きテキストを描画する
	/// @tparam BatchType SpriteBatch互換型
	template <typename BatchType>
	void drawTextWithEffect(BatchType& batch, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& textColor,
		const SdfTextEffect& effect) const
	{
		// 描画順: グロー → シャドウ → アウトライン → 本体
		if (effect.glow.enabled)
		{
			const float expand = effect.glow.radius * fontSize;
			drawTextExpanded(batch, text, x, y, fontSize, effect.glow.color, expand);
		}

		if (effect.shadow.enabled)
		{
			drawText(batch, text,
				x + effect.shadow.offsetX, y + effect.shadow.offsetY,
				fontSize, effect.shadow.color);
		}

		if (effect.outline.enabled)
		{
			const float expand = effect.outline.width * fontSize * 0.5f;
			drawTextExpanded(batch, text, x, y, fontSize, effect.outline.color, expand);
		}

		drawText(batch, text, x, y, fontSize, textColor);
	}

	// ── ソフトウェア Screen 描画 ─────────────────────────────

	/// @brief ソフトウェアScreenにSDFテキストを描画する（CPU smoothstep）
	/// @tparam ScreenType Screen互換型
	/// @param screen 描画先Screen
	/// @param text UTF-8テキスト
	/// @param x 描画開始X座標
	/// @param y 描画開始Y座標
	/// @param fontSize 表示フォントサイズ
	/// @param color テキスト色
	template <typename ScreenType>
	void drawTextSoftware(ScreenType& screen, std::string_view text,
		float x, float y, float fontSize, const sgc::Colorf& color) const
	{
		if (!ready())
		{
			return;
		}

		const float displayScale = fontSize / m_atlas->sdfPixelSize();
		float cursorX = x;
		std::uint32_t prevCp = 0;

		const auto& atlasPixels = m_atlas->texture().pixels();
		const int atlasW = m_atlas->atlasWidth();

		sdf_detail::Utf8Decoder dec(text);
		while (dec.hasNext())
		{
			const std::uint32_t cp = dec.next();

			if (prevCp != 0)
			{
				cursorX += m_atlas->kerning(prevCp, cp, fontSize);
			}

			const auto* gi = m_atlas->findGlyph(cp);
			if (gi != nullptr && gi->width() > 0 && gi->height() > 0)
			{
				sdf_cpu_detail::renderGlyphSoftware(screen, *gi, atlasPixels, atlasW,
					m_atlas->atlasHeight(), cursorX, y, displayScale, color, 0.5f, 0.0f);
			}

			if (gi != nullptr)
			{
				cursorX += gi->xadvance * displayScale;
			}
			prevCp = cp;
		}
	}

	/// @brief ソフトウェアScreenにアウトライン付きSDFテキストを描画する
	/// @tparam ScreenType Screen互換型
	template <typename ScreenType>
	void drawTextSoftwareWithOutline(ScreenType& screen, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& textColor,
		const sgc::Colorf& outlineColor,
		float outlineWidth = 0.1f) const
	{
		if (!ready())
		{
			return;
		}

		const float displayScale = fontSize / m_atlas->sdfPixelSize();
		float cursorX = x;
		std::uint32_t prevCp = 0;

		const auto& atlasPixels = m_atlas->texture().pixels();
		const int atlasW = m_atlas->atlasWidth();

		// アウトラインのSDFしきい値（0.5より低い値で広がる）
		const float outlineThreshold = std::max(0.05f, 0.5f - outlineWidth);

		sdf_detail::Utf8Decoder dec(text);
		while (dec.hasNext())
		{
			const std::uint32_t cp = dec.next();

			if (prevCp != 0)
			{
				cursorX += m_atlas->kerning(prevCp, cp, fontSize);
			}

			const auto* gi = m_atlas->findGlyph(cp);
			if (gi != nullptr && gi->width() > 0 && gi->height() > 0)
			{
				// アウトライン（低いしきい値で描画）
				sdf_cpu_detail::renderGlyphSoftware(screen, *gi, atlasPixels, atlasW,
					m_atlas->atlasHeight(), cursorX, y, displayScale, outlineColor, outlineThreshold, 0.0f);

				// 本体（通常しきい値で上書き）
				sdf_cpu_detail::renderGlyphSoftware(screen, *gi, atlasPixels, atlasW,
					m_atlas->atlasHeight(), cursorX, y, displayScale, textColor, 0.5f, 0.0f);
			}

			if (gi != nullptr)
			{
				cursorX += gi->xadvance * displayScale;
			}
			prevCp = cp;
		}
	}

	/// @brief ソフトウェアScreenにシャドウ付きSDFテキストを描画する
	/// @tparam ScreenType Screen互換型
	template <typename ScreenType>
	void drawTextSoftwareWithShadow(ScreenType& screen, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& textColor,
		const sgc::Colorf& shadowColor,
		const sgc::Vec2f& shadowOffset = {2.0f, 2.0f},
		float shadowSoftness = 0.15f) const
	{
		if (!ready())
		{
			return;
		}

		const float displayScale = fontSize / m_atlas->sdfPixelSize();
		const auto& atlasPixels = m_atlas->texture().pixels();
		const int atlasW = m_atlas->atlasWidth();
		std::uint32_t prevCp = 0;

		// パス1: シャドウ
		{
			float cursorX = x + shadowOffset.x;
			const float shadowY = y + shadowOffset.y;
			prevCp = 0;

			sdf_detail::Utf8Decoder dec(text);
			while (dec.hasNext())
			{
				const std::uint32_t cp = dec.next();
				if (prevCp != 0)
				{
					cursorX += m_atlas->kerning(prevCp, cp, fontSize);
				}
				const auto* gi = m_atlas->findGlyph(cp);
				if (gi != nullptr && gi->width() > 0 && gi->height() > 0)
				{
					const float threshold = std::max(0.05f, 0.5f - shadowSoftness);
					sdf_cpu_detail::renderGlyphSoftware(screen, *gi, atlasPixels, atlasW,
						m_atlas->atlasHeight(), cursorX, shadowY, displayScale, shadowColor, threshold, shadowSoftness);
				}
				if (gi != nullptr)
				{
					cursorX += gi->xadvance * displayScale;
				}
				prevCp = cp;
			}
		}

		// パス2: 本体
		{
			float cursorX = x;
			prevCp = 0;

			sdf_detail::Utf8Decoder dec(text);
			while (dec.hasNext())
			{
				const std::uint32_t cp = dec.next();
				if (prevCp != 0)
				{
					cursorX += m_atlas->kerning(prevCp, cp, fontSize);
				}
				const auto* gi = m_atlas->findGlyph(cp);
				if (gi != nullptr && gi->width() > 0 && gi->height() > 0)
				{
					sdf_cpu_detail::renderGlyphSoftware(screen, *gi, atlasPixels, atlasW,
						m_atlas->atlasHeight(), cursorX, y, displayScale, textColor, 0.5f, 0.0f);
				}
				if (gi != nullptr)
				{
					cursorX += gi->xadvance * displayScale;
				}
				prevCp = cp;
			}
		}
	}

	/// @brief ソフトウェアScreenにグロー付きSDFテキストを描画する
	/// @tparam ScreenType Screen互換型
	template <typename ScreenType>
	void drawTextSoftwareWithGlow(ScreenType& screen, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& textColor,
		const sgc::Colorf& glowColor,
		float glowRadius = 0.2f) const
	{
		if (!ready())
		{
			return;
		}

		const float displayScale = fontSize / m_atlas->sdfPixelSize();
		const auto& atlasPixels = m_atlas->texture().pixels();
		const int atlasW = m_atlas->atlasWidth();

		const float glowThreshold = std::max(0.01f, 0.5f - glowRadius);

		// パス1: グロー
		{
			float cursorX = x;
			std::uint32_t prevCp = 0;

			sdf_detail::Utf8Decoder dec(text);
			while (dec.hasNext())
			{
				const std::uint32_t cp = dec.next();
				if (prevCp != 0)
				{
					cursorX += m_atlas->kerning(prevCp, cp, fontSize);
				}
				const auto* gi = m_atlas->findGlyph(cp);
				if (gi != nullptr && gi->width() > 0 && gi->height() > 0)
				{
					sdf_cpu_detail::renderGlyphSoftware(screen, *gi, atlasPixels, atlasW,
						m_atlas->atlasHeight(), cursorX, y, displayScale, glowColor, glowThreshold, glowRadius);
				}
				if (gi != nullptr)
				{
					cursorX += gi->xadvance * displayScale;
				}
				prevCp = cp;
			}
		}

		// パス2: 本体
		{
			float cursorX = x;
			std::uint32_t prevCp = 0;

			sdf_detail::Utf8Decoder dec(text);
			while (dec.hasNext())
			{
				const std::uint32_t cp = dec.next();
				if (prevCp != 0)
				{
					cursorX += m_atlas->kerning(prevCp, cp, fontSize);
				}
				const auto* gi = m_atlas->findGlyph(cp);
				if (gi != nullptr && gi->width() > 0 && gi->height() > 0)
				{
					sdf_cpu_detail::renderGlyphSoftware(screen, *gi, atlasPixels, atlasW,
						m_atlas->atlasHeight(), cursorX, y, displayScale, textColor, 0.5f, 0.0f);
				}
				if (gi != nullptr)
				{
					cursorX += gi->xadvance * displayScale;
				}
				prevCp = cp;
			}
		}
	}

	/// @brief ソフトウェアScreenに複合エフェクト付きSDFテキストを描画する
	/// @tparam ScreenType Screen互換型
	template <typename ScreenType>
	void drawTextSoftwareWithEffect(ScreenType& screen, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& textColor,
		const SdfTextEffect& effect) const
	{
		if (effect.glow.enabled)
		{
			drawTextSoftwareWithGlow(screen, text, x, y, fontSize,
				textColor, effect.glow.color, effect.glow.radius);
			// 本体はglowメソッド内で描画済みなので、グローのみの場合はreturn可能
			// ただし他のエフェクトもあり得るので、常に本体を最後に描画
		}

		if (effect.shadow.enabled)
		{
			drawTextSoftwareWithShadow(screen, text, x, y, fontSize,
				textColor, effect.shadow.color,
				{effect.shadow.offsetX, effect.shadow.offsetY},
				effect.shadow.softness);
			return; // shadowメソッド内で本体も描画済み
		}

		if (effect.outline.enabled)
		{
			drawTextSoftwareWithOutline(screen, text, x, y, fontSize,
				textColor, effect.outline.color, effect.outline.width);
			return; // outlineメソッド内で本体も描画済み
		}

		// エフェクトなしの場合
		drawTextSoftware(screen, text, x, y, fontSize, textColor);
	}

private:
	/// @brief 拡大クワッドでテキストを描画する（アウトライン・グロー用）
	/// @tparam BatchType SpriteBatch互換型
	template <typename BatchType>
	void drawTextExpanded(BatchType& batch, std::string_view text,
		float x, float y, float fontSize,
		const sgc::Colorf& color, float expand) const
	{
		if (!ready())
		{
			return;
		}
		sdf_gpu_detail::drawTextExpanded(*m_atlas, batch, text, x, y, fontSize, color, expand);
	}
};

// ── SDF ピクセルシェーダーロジック（参考実装） ─────────────────

/// @brief SDFピクセルシェーダーのCPU参考実装
/// @details 実際のGPUシェーダー（HLSL/GLSL/SPIR-V）で使用するロジックの
///          C++参考実装。GpuSpriteBatchのフラグメントシェーダーに移植する際の
///          リファレンスとして使用する。
///
/// GPU HLSL の疑似コード:
/// @code
/// float dist = tex.Sample(sampler, input.uv).a;
/// float smoothing = fwidth(dist) * 0.5;
///
/// // Main text
/// float alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, dist);
///
/// // Outline (lower threshold)
/// float outlineAlpha = smoothstep(outlineThreshold - smoothing,
///                                  outlineThreshold + smoothing, dist);
/// float4 outlineResult = lerp(outlineColor, textColor, alpha);
/// outlineResult.a *= outlineAlpha;
///
/// // Shadow (sample at offset UV)
/// float shadowDist = tex.Sample(sampler, input.uv - shadowOffset).a;
/// float shadowAlpha = smoothstep(0.5 - smoothing - shadowSoftness,
///                                 0.5 + smoothing, shadowDist);
///
/// // Glow (wider smoothstep range)
/// float glowAlpha = smoothstep(glowThreshold - smoothing,
///                               0.5 + smoothing, dist);
/// @endcode
struct SdfShaderLogic
{
	/// @brief SDF距離値からテキストアルファを計算する
	/// @param dist SDF距離値（0.0〜1.0に正規化済み）
	/// @param smoothing スムージング幅（通常 fwidth(dist)*0.5）
	/// @return アルファ値（0.0〜1.0）
	[[nodiscard]] static float textAlpha(float dist, float smoothing) noexcept
	{
		return sdf_detail::smoothstep(0.5f - smoothing, 0.5f + smoothing, dist);
	}

	/// @brief アウトラインアルファを計算する
	/// @param dist SDF距離値
	/// @param smoothing スムージング幅
	/// @param outlineWidth アウトライン幅（SDF空間、0.0〜0.5）
	/// @return アウトラインアルファ値
	[[nodiscard]] static float outlineAlpha(float dist, float smoothing,
		float outlineWidth) noexcept
	{
		const float threshold = 0.5f - outlineWidth;
		return sdf_detail::smoothstep(threshold - smoothing, threshold + smoothing, dist);
	}

	/// @brief グローアルファを計算する
	/// @param dist SDF距離値
	/// @param smoothing スムージング幅
	/// @param glowRadius グロー半径（SDF空間）
	/// @return グローアルファ値
	[[nodiscard]] static float glowAlpha(float dist, float smoothing,
		float glowRadius) noexcept
	{
		const float threshold = 0.5f - glowRadius;
		return sdf_detail::smoothstep(threshold - smoothing, 0.5f + smoothing, dist);
	}

	/// @brief テキスト色とアウトライン色を合成する
	/// @param textColor テキスト色
	/// @param outlineColor アウトライン色
	/// @param textA テキストアルファ
	/// @param outlineA アウトラインアルファ
	/// @return 合成色
	[[nodiscard]] static sgc::Colorf compositeOutline(
		const sgc::Colorf& textColor,
		const sgc::Colorf& outlineColor,
		float textA, float outlineA) noexcept
	{
		// テキスト色をアウトライン色の上にブレンド
		const float r = textColor.r * textA + outlineColor.r * (1.0f - textA);
		const float g = textColor.g * textA + outlineColor.g * (1.0f - textA);
		const float b = textColor.b * textA + outlineColor.b * (1.0f - textA);
		return {r, g, b, outlineA};
	}
};

} // namespace mitiru::render
