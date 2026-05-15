#pragma once

/// @file RubyText.hpp
/// @brief ルビ（ふりがな）テキストの配置計算
/// @details 親文字の上にルビテキストを中央揃えで配置する。
///          RichTextEngineの[ruby]タグと連携して動作する。

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/vn/TrueTypeFont.hpp>

namespace mitiru::vn
{

/// @brief ルビセグメントの情報
struct RubySegment
{
	std::string baseText;       ///< 親文字（漢字など）
	std::string rubyText;       ///< ルビ文字（ひらがななど）
	float baseWidth = 0.0f;     ///< 親文字の描画幅
	float rubyWidth = 0.0f;     ///< ルビ文字の描画幅
	float baseStartX = 0.0f;    ///< 親文字の開始X座標
	float rubyStartX = 0.0f;    ///< ルビ文字の開始X座標
};

/// @brief 配置計算済みのルビグリフ情報
struct PositionedRubyGlyph
{
	std::uint32_t codepoint = 0; ///< Unicodeコードポイント
	float x = 0.0f;              ///< X座標
	float y = 0.0f;              ///< Y座標
	float advanceX = 0.0f;       ///< 前進幅
	bool isRuby = false;         ///< ルビ文字か（falseなら親文字）
};

/// @brief ルビ配置の計算結果
struct RubyLayoutResult
{
	std::vector<PositionedRubyGlyph> glyphs; ///< 配置済みグリフ群
	float totalWidth = 0.0f;                  ///< 全体の描画幅
	float baselineY = 0.0f;                   ///< 親文字のベースラインY座標
	float rubyBaselineY = 0.0f;               ///< ルビのベースラインY座標
	float totalHeight = 0.0f;                 ///< 全体の高さ（ルビ含む）
};

/// @brief ルビ配置のパラメータ
struct RubyLayoutParams
{
	float baseFontSize = 24.0f;       ///< 親文字のフォントサイズ
	float rubyFontSize = 12.0f;       ///< ルビのフォントサイズ（通常は親の半分）
	float rubyGap = 1.0f;             ///< ルビと親文字の間隔（ピクセル）
	float rubyOverhangMax = 0.5f;     ///< ルビが親文字からはみ出せる最大比率（0.0〜1.0）
	bool distributeRuby = true;        ///< ルビを親文字の幅に均等分布するか
};

/// @brief ルビテキストの配置エンジン
/// @details 親文字とルビ文字の位置関係を計算する。
///          TrueTypeFontと連携してメトリクスを取得し、
///          中央揃え・均等分布・オーバーハング制御を行う。
///
/// @code
/// mitiru::vn::TrueTypeFont font(ttfData);
/// mitiru::vn::RubyTextLayout layout;
///
/// mitiru::vn::RubySegment seg;
/// seg.baseText = "漢字";
/// seg.rubyText = "かんじ";
///
/// mitiru::vn::RubyLayoutParams params;
/// params.baseFontSize = 24.0f;
/// params.rubyFontSize = 12.0f;
///
/// auto result = layout.calculate(font, {seg}, params, 0.0f, 0.0f);
/// @endcode
class RubyTextLayout
{
public:
	/// @brief デフォルトコンストラクタ
	RubyTextLayout() = default;

	/// @brief ルビセグメントの幅を計測する
	/// @param font フォント
	/// @param segment ルビセグメント（幅が書き込まれる）
	/// @param params レイアウトパラメータ
	static void measureSegment(
		TrueTypeFont& font,
		RubySegment& segment,
		const RubyLayoutParams& params)
	{
		segment.baseWidth = font.measureText(segment.baseText, params.baseFontSize);
		segment.rubyWidth = font.measureText(segment.rubyText, params.rubyFontSize);
	}

	/// @brief ルビセグメント群のレイアウトを計算する
	/// @param font フォント
	/// @param segments ルビセグメント群
	/// @param params レイアウトパラメータ
	/// @param startX 開始X座標
	/// @param startY 開始Y座標
	/// @return 配置結果
	[[nodiscard]] RubyLayoutResult calculate(
		TrueTypeFont& font,
		const std::vector<RubySegment>& segments,
		const RubyLayoutParams& params,
		float startX,
		float startY) const
	{
		RubyLayoutResult result;

		const auto baseMetrics = font.metrics(params.baseFontSize);
		const auto rubyMetrics = font.metrics(params.rubyFontSize);

		// ルビ行 + 間隔 + 親文字行
		const float rubyLineHeight = rubyMetrics.ascent - rubyMetrics.descent;
		result.rubyBaselineY = startY + rubyMetrics.ascent;
		result.baselineY = startY + rubyLineHeight + params.rubyGap + baseMetrics.ascent;
		result.totalHeight = rubyLineHeight + params.rubyGap + baseMetrics.lineHeight;

		float cursorX = startX;

		for (const auto& seg : segments)
		{
			const float baseWidth = font.measureText(seg.baseText, params.baseFontSize);
			const float rubyWidth = font.measureText(seg.rubyText, params.rubyFontSize);

			// ルビが親文字より広い場合の処理
			const float effectiveWidth = calculateEffectiveWidth(
				baseWidth, rubyWidth, params);

			// 親文字の配置
			const float baseOffsetX = (effectiveWidth - baseWidth) * 0.5f;
			layoutGlyphs(
				result.glyphs, font, seg.baseText, params.baseFontSize,
				cursorX + baseOffsetX, result.baselineY, false);

			// ルビの配置
			if (!seg.rubyText.empty())
			{
				if (params.distributeRuby && rubyWidth < effectiveWidth)
				{
					// ルビを親文字の幅に均等分布
					layoutGlyphsDistributed(
						result.glyphs, font, seg.rubyText, params.rubyFontSize,
						cursorX, result.rubyBaselineY, effectiveWidth, true);
				}
				else
				{
					// ルビを中央揃え
					const float rubyOffsetX = (effectiveWidth - rubyWidth) * 0.5f;
					layoutGlyphs(
						result.glyphs, font, seg.rubyText, params.rubyFontSize,
						cursorX + rubyOffsetX, result.rubyBaselineY, true);
				}
			}

			cursorX += effectiveWidth;
		}

		result.totalWidth = cursorX - startX;
		return result;
	}

	/// @brief ルビ付きテキストの全体幅を計算する
	/// @param font フォント
	/// @param segments ルビセグメント群
	/// @param params レイアウトパラメータ
	/// @return 全体の描画幅
	[[nodiscard]] float measureWidth(
		TrueTypeFont& font,
		const std::vector<RubySegment>& segments,
		const RubyLayoutParams& params) const
	{
		float totalWidth = 0.0f;
		for (const auto& seg : segments)
		{
			const float baseWidth = font.measureText(seg.baseText, params.baseFontSize);
			const float rubyWidth = font.measureText(seg.rubyText, params.rubyFontSize);
			totalWidth += calculateEffectiveWidth(baseWidth, rubyWidth, params);
		}
		return totalWidth;
	}

	/// @brief ルビ付きテキストの行高さを計算する
	/// @param font フォント
	/// @param params レイアウトパラメータ
	/// @return ルビ含む行高さ
	[[nodiscard]] float measureHeight(
		TrueTypeFont& font,
		const RubyLayoutParams& params) const noexcept
	{
		const auto baseMetrics = font.metrics(params.baseFontSize);
		const auto rubyMetrics = font.metrics(params.rubyFontSize);
		const float rubyLineHeight = rubyMetrics.ascent - rubyMetrics.descent;
		return rubyLineHeight + params.rubyGap + baseMetrics.lineHeight;
	}

private:
	/// @brief 親文字とルビの実効幅を計算する
	[[nodiscard]] static float calculateEffectiveWidth(
		float baseWidth, float rubyWidth, const RubyLayoutParams& params) noexcept
	{
		if (rubyWidth <= baseWidth)
		{
			return baseWidth;
		}

		// ルビが親文字より広い場合、オーバーハングを考慮
		const float overhang = (rubyWidth - baseWidth) * 0.5f;
		const float maxOverhang = baseWidth * params.rubyOverhangMax;

		if (overhang <= maxOverhang)
		{
			// オーバーハング範囲内なら親文字幅を維持
			return baseWidth;
		}

		// オーバーハング超過分だけ幅を拡張
		return baseWidth + (overhang - maxOverhang) * 2.0f;
	}

	/// @brief グリフを配置する（通常モード）
	static void layoutGlyphs(
		std::vector<PositionedRubyGlyph>& out,
		TrueTypeFont& font,
		std::string_view text,
		float fontSize,
		float x, float y,
		bool isRuby)
	{
		float cursorX = x;
		std::uint32_t prevCp = 0;

		Utf8Iterator it(text);
		while (it.hasNext())
		{
			const std::uint32_t cp = it.next();

			if (prevCp != 0)
			{
				cursorX += font.kerning(prevCp, cp, fontSize);
			}

			const float advance = font.advanceWidth(cp, fontSize);

			PositionedRubyGlyph glyph;
			glyph.codepoint = cp;
			glyph.x = cursorX;
			glyph.y = y;
			glyph.advanceX = advance;
			glyph.isRuby = isRuby;
			out.push_back(glyph);

			cursorX += advance;
			prevCp = cp;
		}
	}

	/// @brief グリフを均等分布で配置する（ルビ用）
	static void layoutGlyphsDistributed(
		std::vector<PositionedRubyGlyph>& out,
		TrueTypeFont& font,
		std::string_view text,
		float fontSize,
		float x, float y,
		float targetWidth,
		bool isRuby)
	{
		const auto codepoints = utf8ToCodepoints(text);
		if (codepoints.empty())
		{
			return;
		}

		// 各グリフの自然幅を計算
		float naturalWidth = 0.0f;
		std::vector<float> advances;
		advances.reserve(codepoints.size());
		for (const auto cp : codepoints)
		{
			const float adv = font.advanceWidth(cp, fontSize);
			advances.push_back(adv);
			naturalWidth += adv;
		}

		// 余白を均等に分配
		const float extraSpace = targetWidth - naturalWidth;
		const float gapCount = static_cast<float>(codepoints.size());
		const float extraPerChar = (gapCount > 0) ? extraSpace / gapCount : 0.0f;

		float cursorX = x + extraPerChar * 0.5f; // 先頭に半分のスペース

		for (std::size_t i = 0; i < codepoints.size(); ++i)
		{
			PositionedRubyGlyph glyph;
			glyph.codepoint = codepoints[i];
			glyph.x = cursorX;
			glyph.y = y;
			glyph.advanceX = advances[i];
			glyph.isRuby = isRuby;
			out.push_back(glyph);

			cursorX += advances[i] + extraPerChar;
		}
	}
};

} // namespace mitiru::vn
