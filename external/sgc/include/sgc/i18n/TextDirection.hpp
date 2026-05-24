#pragma once

/// @file TextDirection.hpp
/// @brief テキスト方向サポート
///
/// 左→右（LTR）、右→左（RTL）のテキスト方向判定と
/// 双方向テキストの基本的なセグメント分割を提供する。
///
/// @code
/// sgc::i18n::TextDirectionResolver resolver;
/// auto dir = resolver.resolve("ar");
/// // => TextDirection::RightToLeft
///
/// auto segments = sgc::i18n::splitBiDi("Hello مرحبا World", TextDirection::LeftToRight);
/// @endcode

#include <string>
#include <string_view>
#include <vector>

namespace sgc::i18n
{

/// @brief テキスト方向
enum class TextDirection
{
	LeftToRight,  ///< 左から右（英語、日本語等）
	RightToLeft   ///< 右から左（アラビア語、ヘブライ語等）
};

/// @brief 双方向テキストセグメント
struct BiDiSegment
{
	std::string text;           ///< セグメントのテキスト
	TextDirection direction;    ///< このセグメントの方向
	std::size_t startIndex{0};  ///< 元テキスト中の開始位置
};

/// @brief テキスト方向リゾルバ
///
/// 言語コードからテキスト方向を判定する。
class TextDirectionResolver
{
public:
	/// @brief 言語コードからテキスト方向を判定する
	/// @param languageCode 言語コード（例: "en", "ar", "he"）
	/// @return テキスト方向
	[[nodiscard]] static constexpr TextDirection resolve(
		std::string_view languageCode) noexcept
	{
		// RTL言語: アラビア語、ヘブライ語、ペルシャ語、ウルドゥー語
		if (languageCode == "ar" || languageCode == "he" ||
			languageCode == "fa" || languageCode == "ur" ||
			languageCode == "yi" || languageCode == "arc" ||
			languageCode == "dv" || languageCode == "ku" ||
			languageCode == "ps" || languageCode == "sd")
		{
			return TextDirection::RightToLeft;
		}
		return TextDirection::LeftToRight;
	}

	/// @brief テキスト方向が右から左かどうか
	/// @param languageCode 言語コード
	/// @return RTLならtrue
	[[nodiscard]] static constexpr bool isRtl(std::string_view languageCode) noexcept
	{
		return resolve(languageCode) == TextDirection::RightToLeft;
	}
};

/// @brief Unicodeコードポイントの方向特性を判定する
///
/// 簡易的なUnicode方向判定。主要なアラビア文字・ヘブライ文字範囲をカバーする。
///
/// @param codePoint Unicodeコードポイント
/// @return テキスト方向
[[nodiscard]] inline TextDirection charDirection(char32_t codePoint) noexcept
{
	// アラビア文字範囲
	if (codePoint >= 0x0600 && codePoint <= 0x06FF) return TextDirection::RightToLeft;
	if (codePoint >= 0x0750 && codePoint <= 0x077F) return TextDirection::RightToLeft;
	if (codePoint >= 0x08A0 && codePoint <= 0x08FF) return TextDirection::RightToLeft;
	if (codePoint >= 0xFB50 && codePoint <= 0xFDFF) return TextDirection::RightToLeft;
	if (codePoint >= 0xFE70 && codePoint <= 0xFEFF) return TextDirection::RightToLeft;

	// ヘブライ文字範囲
	if (codePoint >= 0x0590 && codePoint <= 0x05FF) return TextDirection::RightToLeft;
	if (codePoint >= 0xFB1D && codePoint <= 0xFB4F) return TextDirection::RightToLeft;

	// シリア文字
	if (codePoint >= 0x0700 && codePoint <= 0x074F) return TextDirection::RightToLeft;

	// ターナ文字（ディベヒ語）
	if (codePoint >= 0x0780 && codePoint <= 0x07BF) return TextDirection::RightToLeft;

	return TextDirection::LeftToRight;
}

/// @brief UTF-8バイト列から1つのコードポイントをデコードする
/// @param data UTF-8文字列
/// @param pos 現在位置（デコード後に更新される）
/// @return デコードされたコードポイント
[[nodiscard]] inline char32_t decodeUtf8(std::string_view data, std::size_t& pos)
{
	if (pos >= data.size()) return 0;

	const auto byte0 = static_cast<unsigned char>(data[pos]);

	if (byte0 < 0x80)
	{
		pos += 1;
		return static_cast<char32_t>(byte0);
	}

	if ((byte0 & 0xE0) == 0xC0 && pos + 1 < data.size())
	{
		const auto byte1 = static_cast<unsigned char>(data[pos + 1]);
		pos += 2;
		return static_cast<char32_t>(((byte0 & 0x1F) << 6) | (byte1 & 0x3F));
	}

	if ((byte0 & 0xF0) == 0xE0 && pos + 2 < data.size())
	{
		const auto byte1 = static_cast<unsigned char>(data[pos + 1]);
		const auto byte2 = static_cast<unsigned char>(data[pos + 2]);
		pos += 3;
		return static_cast<char32_t>(
			((byte0 & 0x0F) << 12) | ((byte1 & 0x3F) << 6) | (byte2 & 0x3F));
	}

	if ((byte0 & 0xF8) == 0xF0 && pos + 3 < data.size())
	{
		const auto byte1 = static_cast<unsigned char>(data[pos + 1]);
		const auto byte2 = static_cast<unsigned char>(data[pos + 2]);
		const auto byte3 = static_cast<unsigned char>(data[pos + 3]);
		pos += 4;
		return static_cast<char32_t>(
			((byte0 & 0x07) << 18) | ((byte1 & 0x3F) << 12) |
			((byte2 & 0x3F) << 6) | (byte3 & 0x3F));
	}

	// 不正なUTF-8: 1バイト進めて置換文字を返す
	pos += 1;
	return 0xFFFD;
}

/// @brief 双方向テキストをセグメントに分割する
///
/// UTF-8テキストを文字方向ごとのセグメントに分割する。
/// 基本的なUnicode BiDiアルゴリズムの簡易実装。
///
/// @param text UTF-8テキスト
/// @param baseDirection ベース方向
/// @return セグメントのベクタ
[[nodiscard]] inline std::vector<BiDiSegment> splitBiDi(
	std::string_view text,
	TextDirection baseDirection = TextDirection::LeftToRight)
{
	std::vector<BiDiSegment> segments;
	if (text.empty()) return segments;

	TextDirection currentDir = baseDirection;
	std::string currentText;
	std::size_t segmentStart = 0;
	std::size_t pos = 0;

	while (pos < text.size())
	{
		const std::size_t charStart = pos;
		const char32_t cp = decodeUtf8(text, pos);
		const TextDirection dir = charDirection(cp);

		// 空白・数字・記号はニュートラル（現在の方向を維持）
		const bool isNeutral = (cp <= 0x007F &&
			(cp == ' ' || cp == '\t' || (cp >= '0' && cp <= '9') ||
			 cp == '.' || cp == ',' || cp == '!' || cp == '?'));

		const TextDirection effectiveDir = isNeutral ? currentDir : dir;

		if (!currentText.empty() && effectiveDir != currentDir)
		{
			segments.push_back({currentText, currentDir, segmentStart});
			currentText.clear();
			segmentStart = charStart;
			currentDir = effectiveDir;
		}
		else if (currentText.empty())
		{
			currentDir = effectiveDir;
		}

		currentText.append(text.data() + charStart, pos - charStart);
	}

	if (!currentText.empty())
	{
		segments.push_back({currentText, currentDir, segmentStart});
	}

	return segments;
}

} // namespace sgc::i18n
