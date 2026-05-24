#pragma once

/// @file Siv3DTextRenderer.hpp
/// @brief ITextRenderer の Siv3D 実装
///
/// フォントサイズごとにs3d::Fontを管理し、
/// ITextRendererインターフェースでテキスト描画を行う。
///
/// @note このファイルはSiv3Dに依存するため、CI対象外。
///
/// @code
/// sgc::siv3d::Siv3DTextRenderer textRenderer;
/// textRenderer.registerFont(60, s3d::Typeface::Heavy);
/// textRenderer.registerFont(24, s3d::Typeface::Regular);
///
/// sgc::ITextRenderer& r = textRenderer;
/// r.drawText("Score: 100", 24.0f, {10, 10}, sgc::Colorf::white());
/// r.drawTextCentered("TITLE", 60.0f, {400, 300}, sgc::Colorf::white());
/// @endcode

#include <Siv3D.hpp>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "sgc/graphics/ITextMeasure.hpp"
#include "sgc/graphics/ITextRenderer.hpp"
#include "sgc/siv3d/TypeConvert.hpp"

namespace sgc::siv3d
{

/// @brief ITextRenderer の Siv3D 実装
///
/// フォントサイズ（int）をキーにフォントを管理する。
/// 事前にregisterFont()でフォントを登録し、drawText/drawTextCenteredで描画する。
class Siv3DTextRenderer : public ITextRenderer, public ITextMeasure
{
public:
	/// @brief フォントをタイプフェース指定で登録する
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param typeface Siv3Dタイプフェース
	void registerFont(int fontSize, s3d::Typeface typeface = s3d::Typeface::Regular)
	{
		m_fonts.insert_or_assign(fontSize, s3d::Font{fontSize, typeface});
	}

	/// @brief 既存のフォントを登録する
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param font Siv3Dフォント
	void registerFont(int fontSize, const s3d::Font& font)
	{
		m_fonts.insert_or_assign(fontSize, font);
	}

	/// @brief テキストを左上基準で描画する
	void drawText(
		std::string_view text, float fontSize,
		const Vec2f& pos, const Colorf& color) override
	{
		const auto& font = getFont(static_cast<int>(fontSize));
		font(s3d::Unicode::FromUTF8(text)).draw(toSiv(pos), toSivColorF(color));
	}

	/// @brief テキストを中央揃えで描画する
	void drawTextCentered(
		std::string_view text, float fontSize,
		const Vec2f& center, const Colorf& color) override
	{
		const auto& font = getFont(static_cast<int>(fontSize));
		font(s3d::Unicode::FromUTF8(text)).drawAt(toSiv(center), toSivColorF(color));
	}

	// ── ITextMeasure 実装 ──────────────────────────────────

	/// @brief テキストの描画サイズを計測する
	/// @param text テキスト文字列
	/// @param fontSize フォントサイズ
	/// @return テキストの描画サイズ
	[[nodiscard]] Vec2f measure(
		std::string_view text, float fontSize) const override
	{
		const auto& font = findNearestFont(static_cast<int>(fontSize));
		const auto region = font(s3d::Unicode::FromUTF8(text)).region();
		return fromSiv(region.size);
	}

	/// @brief 1行のテキストの高さを取得する
	/// @param fontSize フォントサイズ
	/// @return 行の高さ
	[[nodiscard]] float lineHeight(float fontSize) const override
	{
		const auto& font = findNearestFont(static_cast<int>(fontSize));
		return static_cast<float>(font.height());
	}

private:
	std::unordered_map<int, s3d::Font> m_fonts;  ///< フォントサイズ→Font マップ

	/// @brief 指定サイズのフォントを取得する（描画用、厳密一致）
	/// @param fontSize フォントサイズ
	/// @return フォントへの参照
	/// @throw std::out_of_range 未登録のサイズの場合
	[[nodiscard]] const s3d::Font& getFont(int fontSize) const
	{
		const auto it = m_fonts.find(fontSize);
		if (it == m_fonts.end())
		{
			throw std::out_of_range("Siv3DTextRenderer: font not registered for size");
		}
		return it->second;
	}

	/// @brief 指定サイズまたは最近接サイズのフォントを取得する（計測用）
	/// @param fontSize 要求フォントサイズ
	/// @return フォントへの参照
	[[nodiscard]] const s3d::Font& findNearestFont(int fontSize) const
	{
		const auto it = m_fonts.find(fontSize);
		if (it != m_fonts.end())
		{
			return it->second;
		}

		if (m_fonts.empty())
		{
			static const s3d::Font fallback{16, s3d::Typeface::Regular};
			return fallback;
		}

		int bestSize = m_fonts.begin()->first;
		int bestDiff = std::abs(fontSize - bestSize);
		for (const auto& [size, font] : m_fonts)
		{
			const int diff = std::abs(fontSize - size);
			if (diff < bestDiff)
			{
				bestDiff = diff;
				bestSize = size;
			}
		}
		return m_fonts.at(bestSize);
	}
};

} // namespace sgc::siv3d
