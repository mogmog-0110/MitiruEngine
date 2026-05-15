#pragma once

/// @file VerticalText.hpp
/// @brief 縦書き（vertical text）レイアウトエンジン
/// @details 日本語ビジュアルノベル向けの縦書きテキスト配置を提供する。
///          文字は上から下へ流れ、段（列）は右から左へ進む。
///          全角文字は正位置、半角ラテン文字は90度時計回り回転、
///          句読点・小書きかな・長音符の縦書き位置補正、
///          ルビ（ふりがな）の縦書き配置をサポートする。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/vn/TrueTypeFont.hpp>

namespace mitiru::vn
{

// ── 縦書き設定 ─────────────────────────────────────────────

/// @brief 縦書きレイアウトの設定パラメータ
struct VerticalTextConfig
{
	float columnSpacing = 40.0f;     ///< 段（列）間のスペース（ピクセル）
	float lineHeight = 1.5f;         ///< 文字間の行高さ倍率
	bool startFromRight = true;      ///< 右端から開始（伝統的日本語）
	float rubyOffset = 2.0f;         ///< ルビの親文字からの距離（ピクセル）
	bool rotateHalfwidth = true;     ///< 半角文字を90度回転するか
};

// ── 配置済みグリフ ─────────────────────────────────────────

/// @brief 縦書きレイアウト済みの1グリフ
struct PositionedGlyph
{
	std::uint32_t codepoint = 0;     ///< Unicodeコードポイント
	float x = 0.0f;                  ///< X座標（ピクセル）
	float y = 0.0f;                  ///< Y座標（ピクセル）
	std::size_t columnIndex = 0;     ///< 所属する段（列）のインデックス
	float rotation = 0.0f;           ///< 回転角度（度、0=正位置、90=時計回り90度）
	bool isRuby = false;             ///< ルビ文字か
};

// ── ルビ指定 ────────────────────────────────────────────────

/// @brief 縦書きルビの指定情報
struct VerticalRubySpan
{
	std::size_t startIndex = 0;      ///< 親文字列中の開始インデックス（コードポイント単位）
	std::size_t length = 0;          ///< 親文字のコードポイント数
	std::string rubyText;            ///< ルビ文字列（UTF-8）
};

// ── ヘルパー関数 ────────────────────────────────────────────

/// @brief 全角コードポイントか判定する
/// @details CJK統合漢字、ひらがな、カタカナ、全角記号、全角英数を検出する。
/// @param cp Unicodeコードポイント
/// @return 全角文字ならtrue
[[nodiscard]] inline bool isFullwidthCodepoint(std::uint32_t cp) noexcept
{
	// ひらがな (U+3040..U+309F)
	if (cp >= 0x3040 && cp <= 0x309F) return true;
	// カタカナ (U+30A0..U+30FF)
	if (cp >= 0x30A0 && cp <= 0x30FF) return true;
	// CJK記号・句読点 (U+3000..U+303F)
	if (cp >= 0x3000 && cp <= 0x303F) return true;
	// CJK統合漢字 (U+4E00..U+9FFF)
	if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
	// CJK統合漢字拡張A (U+3400..U+4DBF)
	if (cp >= 0x3400 && cp <= 0x4DBF) return true;
	// CJK互換漢字 (U+F900..U+FAFF)
	if (cp >= 0xF900 && cp <= 0xFAFF) return true;
	// 全角英数・記号 (U+FF01..U+FF60)
	if (cp >= 0xFF01 && cp <= 0xFF60) return true;
	// 全角括弧等 (U+FF5B..U+FF9F の一部)
	if (cp >= 0xFF5B && cp <= 0xFF9F) return true;
	// CJK統合漢字拡張B以降 (U+20000..U+2FA1F)
	if (cp >= 0x20000 && cp <= 0x2FA1F) return true;
	// カタカナ拡張 (U+31F0..U+31FF)
	if (cp >= 0x31F0 && cp <= 0x31FF) return true;
	// 全角スペース
	if (cp == 0x3000) return true;

	return false;
}

/// @brief 縦書きで特別な位置補正が必要な句読点か判定する
/// @param cp Unicodeコードポイント
/// @return 縦書き位置補正が必要ならtrue
[[nodiscard]] inline bool isVerticalPunctuation(std::uint32_t cp) noexcept
{
	switch (cp)
	{
	// 句読点（右上に移動）
	case 0x3001: // 、（読点）
	case 0x3002: // 。（句点）
	case 0xFF0C: // ，（全角コンマ）
	case 0xFF0E: // ．（全角ピリオド）
	// 括弧類（回転が必要）
	case 0x300C: // 「
	case 0x300D: // 」
	case 0x300E: // 『
	case 0x300F: // 』
	case 0x3008: // 〈
	case 0x3009: // 〉
	case 0x300A: // 《
	case 0x300B: // 》
	case 0x3010: // 【
	case 0x3011: // 】
	case 0xFF08: // （
	case 0xFF09: // ）
	case 0xFF3B: // ［
	case 0xFF3D: // ］
	case 0xFF5B: // ｛
	case 0xFF5D: // ｝
	// 長音・リーダー
	case 0x30FC: // ー（長音符）
	case 0x2015: // ―（ダッシュ）
	case 0x2026: // …（三点リーダー）
	case 0xFF5E: // ～（波ダッシュ）
		return true;
	default:
		return false;
	}
}

/// @brief 小書きかなか判定する
/// @param cp Unicodeコードポイント
/// @return 小書きかなならtrue
[[nodiscard]] inline bool isSmallKana(std::uint32_t cp) noexcept
{
	switch (cp)
	{
	// 小書きひらがな
	case 0x3041: // ぁ
	case 0x3043: // ぃ
	case 0x3045: // ぅ
	case 0x3047: // ぇ
	case 0x3049: // ぉ
	case 0x3063: // っ
	case 0x3083: // ゃ
	case 0x3085: // ゅ
	case 0x3087: // ょ
	case 0x308E: // ゎ
	// 小書きカタカナ
	case 0x30A1: // ァ
	case 0x30A3: // ィ
	case 0x30A5: // ゥ
	case 0x30A7: // ェ
	case 0x30A9: // ォ
	case 0x30C3: // ッ
	case 0x30E3: // ャ
	case 0x30E5: // ュ
	case 0x30E7: // ョ
	case 0x30EE: // ヮ
	case 0x30F5: // ヵ
	case 0x30F6: // ヶ
		return true;
	default:
		return false;
	}
}

/// @brief 縦書き時の位置オフセットを取得する
/// @details 句読点は右上へ、長音符・ダッシュは90度回転、小書きかなは右上寄せ。
/// @param cp Unicodeコードポイント
/// @param fontSize フォントサイズ（オフセット計算に使用）
/// @param outDx X方向オフセット（出力）
/// @param outDy Y方向オフセット（出力）
/// @param outRotation 回転角度（出力、度単位）
inline void getVerticalOffset(
	std::uint32_t cp,
	float fontSize,
	float& outDx,
	float& outDy,
	float& outRotation) noexcept
{
	outDx = 0.0f;
	outDy = 0.0f;
	outRotation = 0.0f;

	const float halfSize = fontSize * 0.5f;

	switch (cp)
	{
	// 句読点：右上に移動
	case 0x3001: // 、
	case 0x3002: // 。
	case 0xFF0C: // ，
	case 0xFF0E: // ．
		outDx = halfSize;
		outDy = -halfSize;
		return;

	// 長音符・ダッシュ・リーダー：90度回転
	case 0x30FC: // ー
	case 0x2015: // ―
	case 0xFF5E: // ～
		outRotation = 90.0f;
		return;

	// 三点リーダー：90度回転
	case 0x2026: // …
		outRotation = 90.0f;
		return;

	// 括弧類：90度回転
	case 0x300C: // 「
	case 0x300D: // 」
	case 0x300E: // 『
	case 0x300F: // 』
	case 0x3008: // 〈
	case 0x3009: // 〉
	case 0x300A: // 《
	case 0x300B: // 》
	case 0x3010: // 【
	case 0x3011: // 】
	case 0xFF08: // （
	case 0xFF09: // ）
	case 0xFF3B: // ［
	case 0xFF3D: // ］
	case 0xFF5B: // ｛
	case 0xFF5D: // ｝
		outRotation = 90.0f;
		return;

	default:
		break;
	}

	// 小書きかな：右上に少し寄せる
	if (isSmallKana(cp))
	{
		outDx = fontSize * 0.15f;
		outDy = -fontSize * 0.15f;
	}
}

// ── 縦書きレイアウトエンジン ────────────────────────────────

/// @brief 縦書きテキストレイアウトエンジン
/// @details 日本語の伝統的な縦書き方向（上→下、右→左）にテキストを配置する。
///          全角文字は正位置、半角ラテン文字は90度時計回り回転し、
///          句読点・小書きかなの縦書き位置補正を行う。
///          ルビは親文字の右側に配置される。
///
/// @code
/// mitiru::vn::TrueTypeFont font(ttfData);
/// mitiru::vn::VerticalTextLayout layoutEngine;
///
/// mitiru::vn::VerticalTextConfig config;
/// config.columnSpacing = 48.0f;
/// config.startFromRight = true;
///
/// auto glyphs = layoutEngine.layout(
///     "吾輩は猫である。名前はまだ無い。",
///     font, 800.0f, 600.0f, 24.0f, config);
/// @endcode
class VerticalTextLayout
{
public:
	/// @brief デフォルトコンストラクタ
	VerticalTextLayout() = default;

	/// @brief 縦書きレイアウトを実行する
	/// @param text UTF-8テキスト
	/// @param font TrueTypeフォント
	/// @param areaWidth レイアウト領域の幅（ピクセル）
	/// @param areaHeight レイアウト領域の高さ（ピクセル）
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param config レイアウト設定
	/// @return 配置済みグリフの配列
	[[nodiscard]] std::vector<PositionedGlyph> layout(
		std::string_view text,
		TrueTypeFont& font,
		float areaWidth,
		float areaHeight,
		float fontSize,
		const VerticalTextConfig& config = {}) const
	{
		const auto codepoints = utf8ToCodepoints(text);
		return layoutCodepoints(codepoints, font, areaWidth, areaHeight, fontSize, config);
	}

	/// @brief ルビ付き縦書きレイアウトを実行する
	/// @param text UTF-8テキスト
	/// @param font TrueTypeフォント
	/// @param areaWidth レイアウト領域の幅
	/// @param areaHeight レイアウト領域の高さ
	/// @param fontSize フォントサイズ
	/// @param rubySpans ルビ指定の配列
	/// @param config レイアウト設定
	/// @return 配置済みグリフの配列（ルビグリフ含む）
	[[nodiscard]] std::vector<PositionedGlyph> layoutWithRuby(
		std::string_view text,
		TrueTypeFont& font,
		float areaWidth,
		float areaHeight,
		float fontSize,
		const std::vector<VerticalRubySpan>& rubySpans,
		const VerticalTextConfig& config = {}) const
	{
		const auto codepoints = utf8ToCodepoints(text);
		auto glyphs = layoutCodepoints(codepoints, font, areaWidth, areaHeight, fontSize, config);

		// ルビを親文字の右側に配置する
		const float rubyFontSize = fontSize * 0.5f;

		for (const auto& ruby : rubySpans)
		{
			const auto rubyCps = utf8ToCodepoints(ruby.rubyText);
			if (rubyCps.empty() || ruby.length == 0)
			{
				continue;
			}

			// 親文字のグリフ群を検索（コードポイントインデックスでマッチ）
			auto parentGlyphs = findParentGlyphs(glyphs, codepoints, ruby.startIndex, ruby.length);
			if (parentGlyphs.empty())
			{
				continue;
			}

			// 親文字の縦方向の範囲を取得
			const float parentTopY = parentGlyphs.front()->y;
			const float parentBottomY = parentGlyphs.back()->y + fontSize;
			const float parentSpanHeight = parentBottomY - parentTopY;

			// 親文字の右側にルビを配置
			const float rubyX = parentGlyphs.front()->x + fontSize + config.rubyOffset;
			const std::size_t parentColumn = parentGlyphs.front()->columnIndex;

			// ルビを親文字の縦範囲に均等配置
			const float rubyCharStep = (rubyCps.size() > 1)
				? parentSpanHeight / static_cast<float>(rubyCps.size())
				: parentSpanHeight;
			const float rubyStartY = parentTopY
				+ (parentSpanHeight - rubyCharStep * static_cast<float>(rubyCps.size())) * 0.5f;

			for (std::size_t i = 0; i < rubyCps.size(); ++i)
			{
				PositionedGlyph rubyGlyph;
				rubyGlyph.codepoint = rubyCps[i];
				rubyGlyph.x = rubyX;
				rubyGlyph.y = rubyStartY + rubyCharStep * static_cast<float>(i);
				rubyGlyph.columnIndex = parentColumn;
				rubyGlyph.rotation = 0.0f;
				rubyGlyph.isRuby = true;

				// ルビ文字にも縦書き補正を適用
				float dx = 0.0f;
				float dy = 0.0f;
				float rot = 0.0f;
				getVerticalOffset(rubyCps[i], rubyFontSize, dx, dy, rot);
				rubyGlyph.x += dx;
				rubyGlyph.y += dy;
				rubyGlyph.rotation = rot;

				glyphs.push_back(rubyGlyph);
			}
		}

		return glyphs;
	}

	/// @brief テキストに必要な段（列）数を計算する
	/// @param text UTF-8テキスト
	/// @param areaHeight レイアウト領域の高さ
	/// @param fontSize フォントサイズ
	/// @param config レイアウト設定
	/// @return 必要な段数
	[[nodiscard]] std::size_t estimateColumnCount(
		std::string_view text,
		float areaHeight,
		float fontSize,
		const VerticalTextConfig& config = {}) const noexcept
	{
		const auto codepoints = utf8ToCodepoints(text);
		if (codepoints.empty() || areaHeight <= 0.0f || fontSize <= 0.0f)
		{
			return 0;
		}

		const float charStep = fontSize * config.lineHeight;
		const std::size_t charsPerColumn = std::max(
			static_cast<std::size_t>(1),
			static_cast<std::size_t>(areaHeight / charStep));

		return (codepoints.size() + charsPerColumn - 1) / charsPerColumn;
	}

private:
	/// @brief コードポイント列から縦書きレイアウトを実行する
	[[nodiscard]] std::vector<PositionedGlyph> layoutCodepoints(
		const std::vector<std::uint32_t>& codepoints,
		TrueTypeFont& font,
		float areaWidth,
		float areaHeight,
		float fontSize,
		const VerticalTextConfig& config) const
	{
		std::vector<PositionedGlyph> result;
		result.reserve(codepoints.size());

		if (codepoints.empty() || areaHeight <= 0.0f || fontSize <= 0.0f)
		{
			return result;
		}

		const float charStep = fontSize * config.lineHeight;
		const float columnWidth = fontSize + config.columnSpacing;

		// 段数の最大値
		const std::size_t maxColumns = (areaWidth > 0.0f)
			? std::max(static_cast<std::size_t>(1),
				static_cast<std::size_t>(areaWidth / columnWidth))
			: 1;

		std::size_t currentColumn = 0;
		float cursorY = 0.0f;

		for (std::size_t i = 0; i < codepoints.size(); ++i)
		{
			const std::uint32_t cp = codepoints[i];

			// 改行文字
			if (cp == U'\n')
			{
				// 新しい段へ
				if (currentColumn + 1 < maxColumns)
				{
					++currentColumn;
					cursorY = 0.0f;
				}
				continue;
			}

			// 段の末尾到達チェック
			if (cursorY + fontSize > areaHeight)
			{
				if (currentColumn + 1 < maxColumns)
				{
					++currentColumn;
					cursorY = 0.0f;
				}
				else
				{
					// 領域外：これ以上配置できない
					break;
				}
			}

			// X座標の計算
			const float columnX = calculateColumnX(
				currentColumn, columnWidth, areaWidth, config.startFromRight);

			PositionedGlyph glyph;
			glyph.codepoint = cp;
			glyph.x = columnX;
			glyph.y = cursorY;
			glyph.columnIndex = currentColumn;
			glyph.isRuby = false;

			// 回転・位置補正を適用
			applyVerticalTransform(glyph, cp, fontSize, config);

			result.push_back(glyph);
			cursorY += charStep;
		}

		return result;
	}

	/// @brief 縦書きの回転・位置補正を適用する
	static void applyVerticalTransform(
		PositionedGlyph& glyph,
		std::uint32_t cp,
		float fontSize,
		const VerticalTextConfig& config) noexcept
	{
		// 縦書き特殊文字の補正
		if (isVerticalPunctuation(cp) || isSmallKana(cp))
		{
			float dx = 0.0f;
			float dy = 0.0f;
			float rot = 0.0f;
			getVerticalOffset(cp, fontSize, dx, dy, rot);
			glyph.x += dx;
			glyph.y += dy;
			glyph.rotation = rot;
			return;
		}

		// 半角文字の回転
		if (config.rotateHalfwidth && !isFullwidthCodepoint(cp))
		{
			// 半角ラテン文字・数字等は90度時計回り回転
			if (cp >= 0x0020 && cp <= 0x007E)
			{
				glyph.rotation = 90.0f;
				// 回転中心を文字の中心に合わせるための補正
				glyph.x += fontSize * 0.25f;
				glyph.y += fontSize * 0.25f;
			}
		}
	}

	/// @brief 段のX座標を計算する
	[[nodiscard]] static float calculateColumnX(
		std::size_t columnIndex,
		float columnWidth,
		float areaWidth,
		bool startFromRight) noexcept
	{
		if (startFromRight)
		{
			// 右端から左へ（伝統的日本語の段組み方向）
			return areaWidth - columnWidth * (static_cast<float>(columnIndex) + 1.0f);
		}
		// 左端から右へ（非伝統的）
		return columnWidth * static_cast<float>(columnIndex);
	}

	/// @brief 親文字に対応するグリフへのポインタ配列を取得する
	[[nodiscard]] static std::vector<PositionedGlyph*> findParentGlyphs(
		std::vector<PositionedGlyph>& glyphs,
		const std::vector<std::uint32_t>& codepoints,
		std::size_t startIndex,
		std::size_t length)
	{
		std::vector<PositionedGlyph*> parentGlyphs;

		// コードポイントインデックスからグリフを逆引きする
		// glyphsは改行を除くコードポイントに1:1対応している
		std::size_t glyphIdx = 0;
		std::size_t cpIdx = 0;

		for (std::size_t i = 0; i < codepoints.size() && glyphIdx < glyphs.size(); ++i)
		{
			if (codepoints[i] == U'\n')
			{
				++cpIdx;
				continue;
			}

			if (cpIdx >= startIndex && cpIdx < startIndex + length)
			{
				if (glyphIdx < glyphs.size() && !glyphs[glyphIdx].isRuby)
				{
					parentGlyphs.push_back(&glyphs[glyphIdx]);
				}
			}

			++glyphIdx;
			++cpIdx;
		}

		return parentGlyphs;
	}
};

} // namespace mitiru::vn
