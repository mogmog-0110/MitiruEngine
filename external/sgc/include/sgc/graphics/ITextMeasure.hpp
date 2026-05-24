#pragma once

/// @file ITextMeasure.hpp
/// @brief テキストサイズ計測インターフェース
///
/// UIレイアウト計算でテキストの描画サイズを取得する。
/// 描画は行わない。フレームワーク実装者がSiv3D/DxLib等のフォントAPIで実装する。
///
/// @code
/// class MyTextMeasure : public sgc::ITextMeasure {
/// public:
///     Vec2f measure(std::string_view text, float fontSize) const override {
///         return {text.size() * fontSize * 0.6f, fontSize};
///     }
///     float lineHeight(float fontSize) const override { return fontSize; }
/// };
/// @endcode

#include <string_view>

#include "sgc/math/Vec2.hpp"

namespace sgc
{

/// @brief テキストサイズ計測インターフェース
///
/// UIレイアウト計算で使用する。描画は行わない。
/// フレームワーク実装者がSiv3D/DxLib等のフォントAPIで実装する。
class ITextMeasure
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ITextMeasure() = default;

	/// @brief テキストの描画サイズを計測する
	/// @param text テキスト文字列
	/// @param fontSize フォントサイズ（ピクセル）
	/// @return テキストの幅と高さ
	[[nodiscard]] virtual Vec2f measure(
		std::string_view text, float fontSize) const = 0;

	/// @brief 1行のテキストの高さを取得する
	/// @param fontSize フォントサイズ
	/// @return 行の高さ
	[[nodiscard]] virtual float lineHeight(float fontSize) const = 0;
};

} // namespace sgc
