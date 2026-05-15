#pragma once

/// @file RichTextEngine.hpp
/// @brief リッチテキストエンジン
/// @details タグベースの書式指定・アニメーション・ルビ・禁則処理を含む
///          ビジュアルノベル向けテキストレイアウトエンジン。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/vn/RubyText.hpp>
#include <mitiru/vn/TextAnimator.hpp>
#include <mitiru/vn/TrueTypeFont.hpp>

namespace mitiru::vn
{

// ── テキストスタイル ────────────────────────────────────────

/// @brief テキストの色情報
struct TextColor
{
	std::uint8_t r = 255;
	std::uint8_t g = 255;
	std::uint8_t b = 255;
	std::uint8_t a = 255;

	[[nodiscard]] bool operator==(const TextColor& other) const noexcept
	{
		return r == other.r && g == other.g && b == other.b && a == other.a;
	}
};

/// @brief テキストスタイル
struct TextStyle
{
	float fontSize = 24.0f;           ///< フォントサイズ（ピクセル）
	TextColor color{255, 255, 255, 255}; ///< テキスト色
	bool bold = false;                 ///< 太字
	bool italic = false;               ///< 斜体
};

// ── リッチテキストセグメント ─────────────────────────────────

/// @brief リッチテキストセグメントの種類
enum class SegmentType : std::uint8_t
{
	Text,     ///< 通常テキスト
	Ruby,     ///< ルビ付きテキスト
	Wait,     ///< 待機指令
	Speed,    ///< 速度変更指令
};

/// @brief リッチテキストの1セグメント
struct RichTextSegment
{
	SegmentType type = SegmentType::Text;
	std::string text;                  ///< テキスト内容
	TextStyle style;                   ///< スタイル情報
	TextEffect effect = TextEffect::None; ///< アニメーションエフェクト

	// ルビ用
	std::string rubyText;              ///< ルビテキスト（type==Ruby時）

	// 制御用
	float waitMs = 0.0f;               ///< 待機時間（type==Wait時）
	float speed = 0.0f;                ///< 表示速度（type==Speed時、文字/秒）
};

// ── 配置済み文字 ────────────────────────────────────────────

/// @brief レイアウト済みの1文字
struct LayoutChar
{
	std::uint32_t codepoint = 0;      ///< Unicodeコードポイント
	float x = 0.0f;                    ///< X座標
	float y = 0.0f;                    ///< Y座標（ベースライン）
	float advanceX = 0.0f;            ///< 前進幅
	TextStyle style;                   ///< スタイル
	TextEffect effect = TextEffect::None; ///< エフェクト
	bool isRuby = false;              ///< ルビ文字か
	std::size_t segmentIndex = 0;     ///< 元セグメントのインデックス
	std::size_t charIndex = 0;        ///< セグメント内の文字インデックス
};

/// @brief テキストレイアウト結果
struct TextLayout
{
	std::vector<LayoutChar> chars;     ///< 配置済み文字群
	float totalWidth = 0.0f;           ///< 全体の幅
	float totalHeight = 0.0f;          ///< 全体の高さ
	std::size_t lineCount = 0;         ///< 行数
	float cursorX = 0.0f;             ///< 最終カーソルX座標（待機インジケータ用）
	float cursorY = 0.0f;             ///< 最終カーソルY座標
};

// ── タグパーサー ────────────────────────────────────────────

/// @brief リッチテキストタグパーサー
/// @details [b], [i], [color=RRGGBB], [size=N], [wait=ms], [speed=N],
///          [shake], [wave], [ruby=text]...[/ruby], [/tag] をパースする。
class RichTextParser
{
public:
	/// @brief タグ付きテキストをセグメント列にパースする
	/// @param input タグ付きテキスト（UTF-8）
	/// @param defaultStyle デフォルトスタイル
	/// @return セグメント列
	[[nodiscard]] static std::vector<RichTextSegment> parse(
		std::string_view input,
		const TextStyle& defaultStyle = {})
	{
		std::vector<RichTextSegment> segments;
		TextStyle currentStyle = defaultStyle;
		TextEffect currentEffect = TextEffect::None;

		// スタイルスタック（ネスト対応）
		std::vector<TextStyle> styleStack;
		std::vector<TextEffect> effectStack;

		// ルビ状態
		bool inRuby = false;
		std::string rubyAnnotation;
		std::string rubyBase;

		std::size_t pos = 0;
		std::string currentText;

		while (pos < input.size())
		{
			// タグ開始
			if (input[pos] == '[')
			{
				const auto tagEnd = input.find(']', pos);
				if (tagEnd == std::string_view::npos)
				{
					// 閉じ括弧なし：通常テキストとして扱う
					currentText += input[pos];
					++pos;
					continue;
				}

				const auto tagContent = input.substr(pos + 1, tagEnd - pos - 1);

				// 現在のテキストをフラッシュ
				if (!currentText.empty())
				{
					if (inRuby)
					{
						rubyBase += currentText;
					}
					else
					{
						RichTextSegment seg;
						seg.type = SegmentType::Text;
						seg.text = currentText;
						seg.style = currentStyle;
						seg.effect = currentEffect;
						segments.push_back(std::move(seg));
					}
					currentText.clear();
				}

				// タグを処理
				if (tagContent == "b")
				{
					styleStack.push_back(currentStyle);
					currentStyle.bold = true;
				}
				else if (tagContent == "/b")
				{
					if (!styleStack.empty())
					{
						currentStyle.bold = styleStack.back().bold;
						styleStack.pop_back();
					}
				}
				else if (tagContent == "i")
				{
					styleStack.push_back(currentStyle);
					currentStyle.italic = true;
				}
				else if (tagContent == "/i")
				{
					if (!styleStack.empty())
					{
						currentStyle.italic = styleStack.back().italic;
						styleStack.pop_back();
					}
				}
				else if (tagContent.substr(0, 6) == "color=")
				{
					styleStack.push_back(currentStyle);
					currentStyle.color = parseColor(tagContent.substr(6));
				}
				else if (tagContent == "/color")
				{
					if (!styleStack.empty())
					{
						currentStyle.color = styleStack.back().color;
						styleStack.pop_back();
					}
				}
				else if (tagContent.substr(0, 5) == "size=")
				{
					styleStack.push_back(currentStyle);
					currentStyle.fontSize = parseFloat(tagContent.substr(5));
				}
				else if (tagContent == "/size")
				{
					if (!styleStack.empty())
					{
						currentStyle.fontSize = styleStack.back().fontSize;
						styleStack.pop_back();
					}
				}
				else if (tagContent.substr(0, 5) == "wait=")
				{
					RichTextSegment seg;
					seg.type = SegmentType::Wait;
					seg.style = currentStyle;
					seg.waitMs = parseFloat(tagContent.substr(5));
					segments.push_back(std::move(seg));
				}
				else if (tagContent.substr(0, 6) == "speed=")
				{
					RichTextSegment seg;
					seg.type = SegmentType::Speed;
					seg.style = currentStyle;
					seg.speed = parseFloat(tagContent.substr(6));
					segments.push_back(std::move(seg));
				}
				else if (tagContent == "shake")
				{
					effectStack.push_back(currentEffect);
					currentEffect = TextEffect::Shake;
				}
				else if (tagContent == "/shake")
				{
					if (!effectStack.empty())
					{
						currentEffect = effectStack.back();
						effectStack.pop_back();
					}
					else
					{
						currentEffect = TextEffect::None;
					}
				}
				else if (tagContent == "wave")
				{
					effectStack.push_back(currentEffect);
					currentEffect = TextEffect::Wave;
				}
				else if (tagContent == "/wave")
				{
					if (!effectStack.empty())
					{
						currentEffect = effectStack.back();
						effectStack.pop_back();
					}
					else
					{
						currentEffect = TextEffect::None;
					}
				}
				else if (tagContent.substr(0, 5) == "ruby=")
				{
					inRuby = true;
					rubyAnnotation = std::string(tagContent.substr(5));
					rubyBase.clear();
				}
				else if (tagContent == "/ruby")
				{
					if (inRuby && !rubyBase.empty())
					{
						RichTextSegment seg;
						seg.type = SegmentType::Ruby;
						seg.text = rubyBase;
						seg.rubyText = rubyAnnotation;
						seg.style = currentStyle;
						seg.effect = currentEffect;
						segments.push_back(std::move(seg));
					}
					inRuby = false;
					rubyAnnotation.clear();
					rubyBase.clear();
				}
				else
				{
					// 未知のタグ：テキストとして出力
					currentText += '[';
					currentText += std::string(tagContent);
					currentText += ']';
				}

				pos = tagEnd + 1;
			}
			else
			{
				// UTF-8マルチバイトシーケンスを適切に処理
				const auto byte0 = static_cast<std::uint8_t>(input[pos]);
				std::size_t charLen = 1;
				if ((byte0 & 0xE0) == 0xC0) { charLen = 2; }
				else if ((byte0 & 0xF0) == 0xE0) { charLen = 3; }
				else if ((byte0 & 0xF8) == 0xF0) { charLen = 4; }

				charLen = std::min(charLen, input.size() - pos);
				currentText += std::string(input.substr(pos, charLen));
				pos += charLen;
			}
		}

		// 残りテキストをフラッシュ
		if (!currentText.empty())
		{
			if (inRuby)
			{
				// 閉じ忘れルビ：通常テキストとして出力
				RichTextSegment seg;
				seg.type = SegmentType::Ruby;
				seg.text = rubyBase + currentText;
				seg.rubyText = rubyAnnotation;
				seg.style = currentStyle;
				seg.effect = currentEffect;
				segments.push_back(std::move(seg));
			}
			else
			{
				RichTextSegment seg;
				seg.type = SegmentType::Text;
				seg.text = currentText;
				seg.style = currentStyle;
				seg.effect = currentEffect;
				segments.push_back(std::move(seg));
			}
		}

		return segments;
	}

private:
	/// @brief 16進数カラーコードをパースする
	[[nodiscard]] static TextColor parseColor(std::string_view hex) noexcept
	{
		TextColor color;
		if (hex.size() >= 6)
		{
			color.r = parseHexByte(hex.substr(0, 2));
			color.g = parseHexByte(hex.substr(2, 2));
			color.b = parseHexByte(hex.substr(4, 2));
		}
		if (hex.size() >= 8)
		{
			color.a = parseHexByte(hex.substr(6, 2));
		}
		return color;
	}

	/// @brief 2桁16進数をバイトにパースする
	[[nodiscard]] static std::uint8_t parseHexByte(std::string_view hex) noexcept
	{
		auto hexDigit = [](char ch) -> int
		{
			if (ch >= '0' && ch <= '9') return ch - '0';
			if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
			if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
			return 0;
		};

		if (hex.size() < 2) return 0;
		return static_cast<std::uint8_t>(hexDigit(hex[0]) * 16 + hexDigit(hex[1]));
	}

	/// @brief 文字列を浮動小数点にパースする
	[[nodiscard]] static float parseFloat(std::string_view sv) noexcept
	{
		float result = 0.0f;
		float fraction = 0.0f;
		float divisor = 1.0f;
		bool afterDot = false;
		bool negative = false;

		std::size_t i = 0;
		if (i < sv.size() && sv[i] == '-')
		{
			negative = true;
			++i;
		}

		for (; i < sv.size(); ++i)
		{
			if (sv[i] == '.')
			{
				afterDot = true;
				continue;
			}
			if (sv[i] < '0' || sv[i] > '9') break;

			if (afterDot)
			{
				divisor *= 10.0f;
				fraction += static_cast<float>(sv[i] - '0') / divisor;
			}
			else
			{
				result = result * 10.0f + static_cast<float>(sv[i] - '0');
			}
		}

		result += fraction;
		return negative ? -result : result;
	}
};

// ── 禁則処理 ────────────────────────────────────────────────

/// @brief 日本語禁則処理ルール
/// @details 行頭禁止文字・行末禁止文字を判定する。
class KinsokuRules
{
public:
	/// @brief 行頭に置けない文字か（閉じ括弧、句読点など）
	/// @param cp Unicodeコードポイント
	/// @return 行頭禁止ならtrue
	[[nodiscard]] static bool isLineStartProhibited(std::uint32_t cp) noexcept
	{
		// 閉じ括弧・句読点・小書きかな等
		switch (cp)
		{
		// 句読点
		case U'\u3001': // 、
		case U'\u3002': // 。
		case U'\uFF0C': // ，
		case U'\uFF0E': // ．
		case U'\uFF01': // ！
		case U'\uFF1F': // ？
		// 閉じ括弧
		case U'\u3009': // 〉
		case U'\u300B': // 》
		case U'\u300D': // 」
		case U'\u300F': // 』
		case U'\u3011': // 】
		case U'\uFF09': // ）
		case U'\uFF3D': // ］
		case U'\uFF5D': // ｝
		case U')':
		case U']':
		case U'}':
		// 中点・長音
		case U'\u30FB': // ・
		case U'\u30FC': // ー
		// 小書きかな
		case U'\u3041': // ぁ
		case U'\u3043': // ぃ
		case U'\u3045': // ぅ
		case U'\u3047': // ぇ
		case U'\u3049': // ぉ
		case U'\u3063': // っ
		case U'\u3083': // ゃ
		case U'\u3085': // ゅ
		case U'\u3087': // ょ
		case U'\u308E': // ゎ
		case U'\u30A1': // ァ
		case U'\u30A3': // ィ
		case U'\u30A5': // ゥ
		case U'\u30A7': // ェ
		case U'\u30A9': // ォ
		case U'\u30C3': // ッ
		case U'\u30E3': // ャ
		case U'\u30E5': // ュ
		case U'\u30E7': // ョ
		case U'\u30EE': // ヮ
		// ASCII句読点
		case U',':
		case U'.':
		case U'!':
		case U'?':
		case U':':
		case U';':
			return true;
		default:
			return false;
		}
	}

	/// @brief 行末に置けない文字か（開き括弧など）
	/// @param cp Unicodeコードポイント
	/// @return 行末禁止ならtrue
	[[nodiscard]] static bool isLineEndProhibited(std::uint32_t cp) noexcept
	{
		switch (cp)
		{
		case U'\u3008': // 〈
		case U'\u300A': // 《
		case U'\u300C': // 「
		case U'\u300E': // 『
		case U'\u3010': // 【
		case U'\uFF08': // （
		case U'\uFF3B': // ［
		case U'\uFF5B': // ｛
		case U'(':
		case U'[':
		case U'{':
			return true;
		default:
			return false;
		}
	}

	/// @brief CJK文字か（日本語・中国語・韓国語の表意文字）
	/// @param cp Unicodeコードポイント
	/// @return CJK文字ならtrue
	[[nodiscard]] static bool isCjk(std::uint32_t cp) noexcept
	{
		return (cp >= 0x3000 && cp <= 0x9FFF)    // CJK統合漢字、ひらがな、カタカナ等
			|| (cp >= 0xF900 && cp <= 0xFAFF)    // CJK互換漢字
			|| (cp >= 0xFF00 && cp <= 0xFFEF)    // 半角・全角形
			|| (cp >= 0x20000 && cp <= 0x2FA1F); // CJK統合漢字拡張
	}

	/// @brief この文字の後で改行可能か
	/// @param cp 現在の文字
	/// @param nextCp 次の文字
	/// @return 改行可能ならtrue
	[[nodiscard]] static bool canBreakAfter(std::uint32_t cp, std::uint32_t nextCp) noexcept
	{
		// 行末禁止文字の後では改行不可
		if (isLineEndProhibited(cp))
		{
			return false;
		}

		// 次が行頭禁止文字なら改行不可
		if (isLineStartProhibited(nextCp))
		{
			return false;
		}

		// CJK文字間は基本的に改行可能
		if (isCjk(cp) || isCjk(nextCp))
		{
			return true;
		}

		// スペースの後は改行可能
		if (cp == U' ' || cp == U'\u3000') // 半角・全角スペース
		{
			return true;
		}

		return false;
	}
};

// ── リッチテキストエンジン ──────────────────────────────────

/// @brief リッチテキストエンジン
/// @details タグパース、テキストレイアウト、文字送りアニメーション、
///          禁則処理、ルビ配置を統合するビジュアルノベル向けテキストエンジン。
///
/// @code
/// mitiru::vn::TrueTypeFont font(ttfData);
/// mitiru::vn::RichTextEngine engine;
///
/// engine.setText("[b]太郎[/b]は[ruby=かんじ]漢字[/ruby]を[wave]読んだ[/wave]。");
/// engine.layout(font, 600.0f);
///
/// // 毎フレーム更新
/// engine.update(dt);
///
/// // 描画情報を取得
/// const auto& layout = engine.layoutResult();
/// const auto transforms = engine.getTransforms();
/// @endcode
class RichTextEngine
{
	std::vector<RichTextSegment> m_segments;
	TextLayout m_layout;
	TextAnimator m_animator;
	RubyTextLayout m_rubyLayout;

	TextStyle m_defaultStyle;
	float m_maxWidth = 0.0f;
	bool m_layoutDirty = true;

	// 文字インデックスマッピング（ルビ非カウント文字を除外）
	std::vector<std::size_t> m_visibleCharIndices;

public:
	/// @brief デフォルトコンストラクタ
	RichTextEngine() = default;

	/// @brief デフォルトスタイルを設定する
	/// @param style デフォルトスタイル
	void setDefaultStyle(const TextStyle& style) noexcept
	{
		m_defaultStyle = style;
		m_layoutDirty = true;
	}

	/// @brief タグ付きテキストを設定する
	/// @param input タグ付きUTF-8テキスト
	void setText(std::string_view input)
	{
		m_segments = RichTextParser::parse(input, m_defaultStyle);
		m_layoutDirty = true;
	}

	/// @brief セグメント列を直接設定する
	/// @param segments セグメント列
	void setSegments(std::vector<RichTextSegment> segments)
	{
		m_segments = std::move(segments);
		m_layoutDirty = true;
	}

	/// @brief テキストをレイアウトする
	/// @param font フォント
	/// @param maxWidth 最大行幅（ピクセル、0で無制限）
	void layout(TrueTypeFont& font, float maxWidth = 0.0f)
	{
		m_maxWidth = maxWidth;
		m_layout = TextLayout{};
		m_visibleCharIndices.clear();

		const auto baseMetrics = font.metrics(m_defaultStyle.fontSize);
		float cursorX = 0.0f;
		float cursorY = baseMetrics.ascent;
		float lineHeight = baseMetrics.lineHeight;
		float maxLineWidth = 0.0f;
		std::size_t lineCount = 1;

		// 現在の表示速度
		float currentSpeed = 20.0f;
		std::size_t mainCharIndex = 0;

		// 文字ごとの待機時間
		std::vector<float> charWaitTimes;
		std::vector<TextEffect> charEffects;

		for (std::size_t segIdx = 0; segIdx < m_segments.size(); ++segIdx)
		{
			const auto& seg = m_segments[segIdx];

			switch (seg.type)
			{
			case SegmentType::Wait:
			{
				// 直前の文字に待機時間を設定
				if (!charWaitTimes.empty())
				{
					charWaitTimes.back() = seg.waitMs / 1000.0f;
				}
				break;
			}

			case SegmentType::Speed:
			{
				currentSpeed = seg.speed;
				break;
			}

			case SegmentType::Ruby:
			{
				// ルビ付きテキストの処理
				RubySegment rubySeg;
				rubySeg.baseText = seg.text;
				rubySeg.rubyText = seg.rubyText;

				RubyLayoutParams rubyParams;
				rubyParams.baseFontSize = seg.style.fontSize;
				rubyParams.rubyFontSize = seg.style.fontSize * 0.5f;

				const float rubyHeight = m_rubyLayout.measureHeight(font, rubyParams);
				lineHeight = std::max(lineHeight, rubyHeight);

				auto rubyResult = m_rubyLayout.calculate(
					font, {rubySeg}, rubyParams, cursorX, cursorY - baseMetrics.ascent);

				// 行幅チェック
				if (maxWidth > 0.0f && cursorX + rubyResult.totalWidth > maxWidth && cursorX > 0.0f)
				{
					cursorX = 0.0f;
					cursorY += lineHeight;
					lineCount++;

					rubyResult = m_rubyLayout.calculate(
						font, {rubySeg}, rubyParams, cursorX, cursorY - baseMetrics.ascent);
				}

				for (const auto& glyph : rubyResult.glyphs)
				{
					LayoutChar lc;
					lc.codepoint = glyph.codepoint;
					lc.x = glyph.x;
					lc.y = glyph.y;
					lc.advanceX = glyph.advanceX;
					lc.style = seg.style;
					lc.effect = seg.effect;
					lc.isRuby = glyph.isRuby;
					lc.segmentIndex = segIdx;
					m_layout.chars.push_back(lc);

					if (!glyph.isRuby)
					{
						m_visibleCharIndices.push_back(m_layout.chars.size() - 1);
						charWaitTimes.push_back(0.0f);
						charEffects.push_back(seg.effect);
						++mainCharIndex;
					}
				}

				cursorX += rubyResult.totalWidth;
				maxLineWidth = std::max(maxLineWidth, cursorX);
				break;
			}

			case SegmentType::Text:
			{
				const auto codepoints = utf8ToCodepoints(seg.text);

				for (std::size_t ci = 0; ci < codepoints.size(); ++ci)
				{
					const std::uint32_t cp = codepoints[ci];

					// 改行文字
					if (cp == U'\n')
					{
						maxLineWidth = std::max(maxLineWidth, cursorX);
						cursorX = 0.0f;
						cursorY += lineHeight;
						lineCount++;

						// 改行はカウントするが描画しない
						LayoutChar lc;
						lc.codepoint = cp;
						lc.x = cursorX;
						lc.y = cursorY;
						lc.style = seg.style;
						lc.segmentIndex = segIdx;
						lc.charIndex = ci;
						m_layout.chars.push_back(lc);
						m_visibleCharIndices.push_back(m_layout.chars.size() - 1);
						charWaitTimes.push_back(0.0f);
						charEffects.push_back(seg.effect);
						++mainCharIndex;
						continue;
					}

					const float advance = font.advanceWidth(cp, seg.style.fontSize);

					// カーニング
					float kern = 0.0f;
					if (ci + 1 < codepoints.size())
					{
						kern = font.kerning(cp, codepoints[ci + 1], seg.style.fontSize);
					}

					// 行幅チェック（禁則処理込み）
					if (maxWidth > 0.0f && cursorX + advance > maxWidth && cursorX > 0.0f)
					{
						// 禁則チェック
						const std::uint32_t nextCp = (ci + 1 < codepoints.size())
							? codepoints[ci + 1] : 0;

						if (!KinsokuRules::isLineStartProhibited(cp))
						{
							maxLineWidth = std::max(maxLineWidth, cursorX);
							cursorX = 0.0f;
							cursorY += lineHeight;
							lineCount++;
						}
					}

					LayoutChar lc;
					lc.codepoint = cp;
					lc.x = cursorX;
					lc.y = cursorY;
					lc.advanceX = advance;
					lc.style = seg.style;
					lc.effect = seg.effect;
					lc.isRuby = false;
					lc.segmentIndex = segIdx;
					lc.charIndex = ci;
					m_layout.chars.push_back(lc);

					m_visibleCharIndices.push_back(m_layout.chars.size() - 1);
					charWaitTimes.push_back(0.0f);
					charEffects.push_back(seg.effect);
					++mainCharIndex;

					cursorX += advance + kern;
				}
				break;
			}
			}
		}

		maxLineWidth = std::max(maxLineWidth, cursorX);
		m_layout.totalWidth = maxLineWidth;
		m_layout.totalHeight = cursorY + (lineHeight - baseMetrics.ascent);
		m_layout.lineCount = lineCount;
		m_layout.cursorX = cursorX;
		m_layout.cursorY = cursorY;

		// アニメーターをセットアップ
		m_animator.reset(mainCharIndex);
		m_animator.setRevealSpeed(currentSpeed);

		for (std::size_t i = 0; i < charWaitTimes.size(); ++i)
		{
			if (charWaitTimes[i] > 0.0f)
			{
				m_animator.setCharWaitTime(i, charWaitTimes[i]);
			}
			if (i < charEffects.size() && charEffects[i] != TextEffect::None)
			{
				m_animator.setCharEffect(i, charEffects[i]);
			}
		}

		m_layoutDirty = false;
	}

	/// @brief アニメーションを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		m_animator.update(dt);
	}

	/// @brief レイアウト結果を取得する
	[[nodiscard]] const TextLayout& layoutResult() const noexcept
	{
		return m_layout;
	}

	/// @brief 全文字のトランスフォームを取得する
	/// @return レイアウト文字数分のトランスフォーム（ルビ文字含む）
	[[nodiscard]] std::vector<CharTransform> getTransforms() const
	{
		std::vector<CharTransform> result;
		result.resize(m_layout.chars.size());

		const auto animTransforms = m_animator.getAllTransforms();

		// メイン文字のトランスフォームをマッピング
		for (std::size_t mainIdx = 0; mainIdx < m_visibleCharIndices.size(); ++mainIdx)
		{
			const auto layoutIdx = m_visibleCharIndices[mainIdx];
			if (layoutIdx < result.size() && mainIdx < animTransforms.size())
			{
				result[layoutIdx] = animTransforms[mainIdx];
			}
		}

		// ルビ文字は対応する親文字のvisibility/alphaに連動
		for (std::size_t i = 0; i < m_layout.chars.size(); ++i)
		{
			if (m_layout.chars[i].isRuby)
			{
				// 同セグメント内の最初の非ルビ文字を探す
				const auto segIdx = m_layout.chars[i].segmentIndex;
				bool parentVisible = false;
				for (std::size_t mainIdx = 0; mainIdx < m_visibleCharIndices.size(); ++mainIdx)
				{
					const auto li = m_visibleCharIndices[mainIdx];
					if (li < m_layout.chars.size()
						&& m_layout.chars[li].segmentIndex == segIdx
						&& !m_layout.chars[li].isRuby)
					{
						if (mainIdx < animTransforms.size())
						{
							parentVisible = animTransforms[mainIdx].visible;
							result[i].alpha = animTransforms[mainIdx].alpha;
						}
						break;
					}
				}
				result[i].visible = parentVisible;
			}
		}

		return result;
	}

	/// @brief 表示済み文字数を取得する
	[[nodiscard]] std::size_t revealedCount() const noexcept
	{
		return m_animator.revealedCount();
	}

	/// @brief 全文字が表示済みか
	[[nodiscard]] bool isComplete() const noexcept
	{
		return m_animator.isComplete();
	}

	/// @brief 全文字を即座に表示する
	void skipToEnd() noexcept
	{
		m_animator.skipToEnd();
	}

	/// @brief アニメーターへの参照を取得する
	[[nodiscard]] TextAnimator& animator() noexcept { return m_animator; }

	/// @brief アニメーターへのconst参照を取得する
	[[nodiscard]] const TextAnimator& animator() const noexcept { return m_animator; }

	/// @brief セグメント列を取得する
	[[nodiscard]] const std::vector<RichTextSegment>& segments() const noexcept
	{
		return m_segments;
	}

	/// @brief カーソル位置を取得する（待機インジケータ用）
	/// @param outX X座標（出力）
	/// @param outY Y座標（出力）
	void cursorPosition(float& outX, float& outY) const noexcept
	{
		if (m_layout.chars.empty())
		{
			outX = 0.0f;
			outY = 0.0f;
			return;
		}

		// 最後に表示された文字の右端
		const auto revealed = m_animator.revealedCount();
		if (revealed > 0 && revealed <= m_visibleCharIndices.size())
		{
			const auto lastIdx = m_visibleCharIndices[revealed - 1];
			if (lastIdx < m_layout.chars.size())
			{
				const auto& lc = m_layout.chars[lastIdx];
				outX = lc.x + lc.advanceX;
				outY = lc.y;
				return;
			}
		}

		outX = m_layout.cursorX;
		outY = m_layout.cursorY;
	}
};

} // namespace mitiru::vn
