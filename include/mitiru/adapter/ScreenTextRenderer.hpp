#pragma once

/// @file ScreenTextRenderer.hpp
/// @brief mitiru::Screen → sgc::ITextRenderer + sgc::ITextMeasure アダプター
/// @details ScreenのdrawText APIとBitmapFontのメトリクスを使って
///          sgcのテキスト描画・計測インターフェースを実装する。

#include <algorithm>
#include <string_view>

#include <sgc/graphics/ITextRenderer.hpp>
#include <sgc/graphics/ITextMeasure.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>
#include <mitiru/core/Screen.hpp>
#include <mitiru/render/BitmapFont.hpp>

namespace mitiru::adapter
{

/// @brief mitiru::ScreenをsgcのITextRenderer + ITextMeasureとして使用するアダプター
/// @details BitmapFontの8x8グリフサイズを基準にスケーリングし、
///          テキストの描画と計測の両方を提供する。
class ScreenTextRenderer : public sgc::ITextRenderer, public sgc::ITextMeasure
{
public:
	/// @brief コンストラクタ
	/// @param screen 描画先のScreen（非所有）
	explicit ScreenTextRenderer(Screen& screen) noexcept
		: m_screen(screen)
	{
	}

	// ── ITextRenderer ──

	/// @brief テキストを左上基準で描画する
	/// @param text テキスト文字列
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param pos 描画位置（左上）
	/// @param color テキスト色
	void drawText(
		std::string_view text, float fontSize,
		const sgc::Vec2f& pos, const sgc::Colorf& color) override
	{
		m_screen.drawText(pos, text, color, fontSize);
	}

	/// @brief テキストを中央揃えで描画する
	/// @param text テキスト文字列
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param center 中央座標
	/// @param color テキスト色
	void drawTextCentered(
		std::string_view text, float fontSize,
		const sgc::Vec2f& center, const sgc::Colorf& color) override
	{
		const auto size = measure(text, fontSize);
		const sgc::Vec2f pos{center.x - size.x * 0.5f, center.y - size.y * 0.5f};
		m_screen.drawText(pos, text, color, fontSize);
	}

	// ── ITextMeasure ──

	/// @brief テキストの描画サイズを計測する
	/// @param text テキスト文字列
	/// @param fontSize フォントサイズ（ピクセル）
	/// @return テキストの幅と高さ
	[[nodiscard]] sgc::Vec2f measure(
		std::string_view text, float fontSize) const override
	{
		const int scale = std::max(1, static_cast<int>(fontSize) / render::BitmapFont::GLYPH_HEIGHT);
		const float charW = static_cast<float>(render::BitmapFont::GLYPH_WIDTH * scale);
		const float charH = static_cast<float>(render::BitmapFont::GLYPH_HEIGHT * scale);
		return {charW * static_cast<float>(text.size()), charH};
	}

	/// @brief 1行のテキストの高さを取得する
	/// @param fontSize フォントサイズ（ピクセル）
	/// @return 行の高さ
	[[nodiscard]] float lineHeight(float fontSize) const override
	{
		const int scale = std::max(1, static_cast<int>(fontSize) / render::BitmapFont::GLYPH_HEIGHT);
		return static_cast<float>(render::BitmapFont::GLYPH_HEIGHT * scale);
	}

private:
	Screen& m_screen;  ///< 描画先Screen（非所有）
};

} // namespace mitiru::adapter
