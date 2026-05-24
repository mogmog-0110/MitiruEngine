#pragma once

/// @file ITextRenderer.hpp
/// @brief テキスト描画インターフェース
///
/// フレームワーク非依存のテキスト描画APIを定義する。
/// Siv3D、DxLib等の具体的な実装はこのインターフェースを実装する。
///
/// @note ITextMeasure（計測のみ）とは独立。描画を行う。
///
/// @code
/// class MyTextRenderer : public sgc::ITextRenderer {
/// public:
///     void drawText("Hello", 24.0f, {10, 10}, Colorf::white()) override { /* ... */ }
///     void drawTextCentered("Title", 60.0f, {400, 300}, Colorf::white()) override { /* ... */ }
/// };
/// @endcode

#include <string_view>

#include "sgc/math/Vec2.hpp"
#include "sgc/types/Color.hpp"

namespace sgc
{

/// @brief テキスト描画インターフェース
///
/// テキストの描画を抽象化する。
/// フォント管理は各フレームワーク実装に委ねる。
class ITextRenderer
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ITextRenderer() = default;

	/// @brief テキストを左上基準で描画する
	/// @param text テキスト文字列（UTF-8）
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param pos 描画位置（左上）
	/// @param color テキスト色
	virtual void drawText(
		std::string_view text, float fontSize,
		const Vec2f& pos, const Colorf& color) = 0;

	/// @brief テキストを中央揃えで描画する
	/// @param text テキスト文字列（UTF-8）
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param center 中央座標
	/// @param color テキスト色
	virtual void drawTextCentered(
		std::string_view text, float fontSize,
		const Vec2f& center, const Colorf& color) = 0;
};

} // namespace sgc
