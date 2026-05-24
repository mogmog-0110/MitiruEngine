#pragma once

/// @file TextEffects.hpp
/// @brief インラインタグパーサーによるテキストエフェクト
///
/// タグ付きテキストを解析し、エフェクト付きセグメントに分解する。
/// シェイク、ウェーブ、フェード、カラー変更、速度変更、一時停止をサポートする。
///
/// @code
/// using namespace sgc::vn;
/// auto segments = parseTextEffects("{shake}Earthquake!{/shake} Normal text.");
/// // segments[0]: text="Earthquake!", effect=Shake
/// // segments[1]: text=" Normal text.", effect=None
/// @endcode

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sgc/types/Color.hpp"

namespace sgc::vn
{

/// @brief テキストエフェクトの種類
enum class TextEffect : uint8_t
{
	None,    ///< エフェクトなし
	Shake,   ///< 振動エフェクト
	Wave,    ///< 波打ちエフェクト
	Fade     ///< フェードエフェクト
};

/// @brief テキストセグメント
///
/// パース結果として返される1区間分のテキストとそのエフェクト情報。
struct TextSegment
{
	std::string text;                               ///< セグメントのテキスト内容
	Colorf color = Colorf::white();                 ///< テキスト色
	TextEffect effect = TextEffect::None;           ///< エフェクト種別
	float speedMultiplier = 1.0f;                   ///< 表示速度倍率
	float pauseDuration = 0.0f;                     ///< このセグメント前の一時停止時間（秒）
};

namespace detail
{

/// @brief 16進文字を数値に変換する
/// @param ch 16進文字
/// @return 数値（0-15）。無効な場合は0
[[nodiscard]] inline int hexCharToInt(char ch) noexcept
{
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
	return 0;
}

/// @brief 6桁のRRGGBB文字列からColorfに変換する
/// @param hex 6桁の16進文字列
/// @return 変換結果の色
[[nodiscard]] inline Colorf parseHexColor(const std::string& hex) noexcept
{
	if (hex.size() != 6)
	{
		return Colorf::white();
	}
	const auto r = static_cast<uint8_t>(hexCharToInt(hex[0]) * 16 + hexCharToInt(hex[1]));
	const auto g = static_cast<uint8_t>(hexCharToInt(hex[2]) * 16 + hexCharToInt(hex[3]));
	const auto b = static_cast<uint8_t>(hexCharToInt(hex[4]) * 16 + hexCharToInt(hex[5]));
	return Colorf::fromRGBA8(r, g, b);
}

/// @brief 文字列をfloatに変換する（簡易版）
/// @param s 数値文字列
/// @return 変換結果
[[nodiscard]] inline float parseFloat(const std::string& s) noexcept
{
	float result = 0.0f;
	float fraction = 0.0f;
	float divisor = 1.0f;
	bool hasDot = false;

	for (const char ch : s)
	{
		if (ch == '.')
		{
			hasDot = true;
			continue;
		}
		if (ch < '0' || ch > '9')
		{
			continue;
		}
		if (hasDot)
		{
			divisor *= 10.0f;
			fraction += static_cast<float>(ch - '0') / divisor;
		}
		else
		{
			result = result * 10.0f + static_cast<float>(ch - '0');
		}
	}
	return result + fraction;
}

} // namespace detail

/// @brief タグ付きテキストをセグメントに分解する
///
/// サポートするタグ:
/// - {shake}text{/shake} : 振動エフェクト
/// - {wave}text{/wave} : 波打ちエフェクト
/// - {fade}text{/fade} : フェードエフェクト
/// - {color=RRGGBB}text{/color} : テキスト色変更
/// - {speed=N.N}text{/speed} : 表示速度変更
/// - {pause=N.N} : 一時停止（閉じタグなし）
///
/// 不正なタグはプレーンテキストとして扱う。
///
/// @param taggedText タグ付きテキスト
/// @return セグメントの配列
[[nodiscard]] inline std::vector<TextSegment> parseTextEffects(const std::string& taggedText)
{
	std::vector<TextSegment> segments;
	if (taggedText.empty())
	{
		return segments;
	}

	// 現在のエフェクトスタック（簡易実装: 直近のエフェクトのみ）
	Colorf currentColor = Colorf::white();
	TextEffect currentEffect = TextEffect::None;
	float currentSpeed = 1.0f;
	float pendingPause = 0.0f;

	std::string currentText;
	std::size_t pos = 0;
	const std::size_t len = taggedText.size();

	while (pos < len)
	{
		// タグの開始を検出
		if (taggedText[pos] == '{')
		{
			// 閉じ括弧を探す
			const std::size_t closePos = taggedText.find('}', pos + 1);
			if (closePos == std::string::npos)
			{
				// 閉じ括弧がない: プレーンテキストとして扱う
				currentText += taggedText[pos];
				++pos;
				continue;
			}

			const std::string tagContent = taggedText.substr(pos + 1, closePos - pos - 1);

			// 閉じタグの処理
			if (tagContent == "/shake" || tagContent == "/wave" || tagContent == "/fade")
			{
				// 現在のテキストをセグメントとして確定
				if (!currentText.empty())
				{
					TextSegment seg;
					seg.text = currentText;
					seg.color = currentColor;
					seg.effect = currentEffect;
					seg.speedMultiplier = currentSpeed;
					seg.pauseDuration = pendingPause;
					segments.push_back(std::move(seg));
					currentText.clear();
					pendingPause = 0.0f;
				}
				currentEffect = TextEffect::None;
				pos = closePos + 1;
				continue;
			}

			if (tagContent == "/color")
			{
				if (!currentText.empty())
				{
					TextSegment seg;
					seg.text = currentText;
					seg.color = currentColor;
					seg.effect = currentEffect;
					seg.speedMultiplier = currentSpeed;
					seg.pauseDuration = pendingPause;
					segments.push_back(std::move(seg));
					currentText.clear();
					pendingPause = 0.0f;
				}
				currentColor = Colorf::white();
				pos = closePos + 1;
				continue;
			}

			if (tagContent == "/speed")
			{
				if (!currentText.empty())
				{
					TextSegment seg;
					seg.text = currentText;
					seg.color = currentColor;
					seg.effect = currentEffect;
					seg.speedMultiplier = currentSpeed;
					seg.pauseDuration = pendingPause;
					segments.push_back(std::move(seg));
					currentText.clear();
					pendingPause = 0.0f;
				}
				currentSpeed = 1.0f;
				pos = closePos + 1;
				continue;
			}

			// 開きタグの処理
			if (tagContent == "shake" || tagContent == "wave" || tagContent == "fade")
			{
				// 現在のテキストをセグメントとして確定
				if (!currentText.empty())
				{
					TextSegment seg;
					seg.text = currentText;
					seg.color = currentColor;
					seg.effect = currentEffect;
					seg.speedMultiplier = currentSpeed;
					seg.pauseDuration = pendingPause;
					segments.push_back(std::move(seg));
					currentText.clear();
					pendingPause = 0.0f;
				}

				if (tagContent == "shake")
				{
					currentEffect = TextEffect::Shake;
				}
				else if (tagContent == "wave")
				{
					currentEffect = TextEffect::Wave;
				}
				else
				{
					currentEffect = TextEffect::Fade;
				}
				pos = closePos + 1;
				continue;
			}

			// color=RRGGBB タグ
			if (tagContent.size() == 12 && tagContent.substr(0, 6) == "color=")
			{
				if (!currentText.empty())
				{
					TextSegment seg;
					seg.text = currentText;
					seg.color = currentColor;
					seg.effect = currentEffect;
					seg.speedMultiplier = currentSpeed;
					seg.pauseDuration = pendingPause;
					segments.push_back(std::move(seg));
					currentText.clear();
					pendingPause = 0.0f;
				}
				currentColor = detail::parseHexColor(tagContent.substr(6));
				pos = closePos + 1;
				continue;
			}

			// speed=N.N タグ
			if (tagContent.size() > 6 && tagContent.substr(0, 6) == "speed=")
			{
				if (!currentText.empty())
				{
					TextSegment seg;
					seg.text = currentText;
					seg.color = currentColor;
					seg.effect = currentEffect;
					seg.speedMultiplier = currentSpeed;
					seg.pauseDuration = pendingPause;
					segments.push_back(std::move(seg));
					currentText.clear();
					pendingPause = 0.0f;
				}
				currentSpeed = detail::parseFloat(tagContent.substr(6));
				pos = closePos + 1;
				continue;
			}

			// pause=N.N タグ（閉じタグなし）
			if (tagContent.size() > 6 && tagContent.substr(0, 6) == "pause=")
			{
				// 現在のテキストを確定
				if (!currentText.empty())
				{
					TextSegment seg;
					seg.text = currentText;
					seg.color = currentColor;
					seg.effect = currentEffect;
					seg.speedMultiplier = currentSpeed;
					seg.pauseDuration = pendingPause;
					segments.push_back(std::move(seg));
					currentText.clear();
					pendingPause = 0.0f;
				}
				pendingPause = detail::parseFloat(tagContent.substr(6));
				pos = closePos + 1;
				continue;
			}

			// 認識できないタグ: プレーンテキストとして扱う
			currentText += taggedText[pos];
			++pos;
			continue;
		}

		// 通常文字
		currentText += taggedText[pos];
		++pos;
	}

	// 残りのテキストをセグメントとして確定
	if (!currentText.empty())
	{
		TextSegment seg;
		seg.text = currentText;
		seg.color = currentColor;
		seg.effect = currentEffect;
		seg.speedMultiplier = currentSpeed;
		seg.pauseDuration = pendingPause;
		segments.push_back(std::move(seg));
	}

	return segments;
}

} // namespace sgc::vn
