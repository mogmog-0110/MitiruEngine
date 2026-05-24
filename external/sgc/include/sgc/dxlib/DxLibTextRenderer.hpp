#pragma once

/// @file DxLibTextRenderer.hpp
/// @brief ITextRenderer の DxLib 実装
///
/// フォントサイズごとにDxLibフォントハンドルを管理し、
/// ITextRendererインターフェースでテキスト描画を行う。
///
/// @note このファイルはDxLib.hに依存するため、CI対象外。
///
/// @code
/// sgc::dxlib::DxLibTextRenderer textRenderer;
/// textRenderer.registerFont(60, "ＭＳ ゴシック");
/// textRenderer.registerFont(24);
///
/// sgc::ITextRenderer& r = textRenderer;
/// r.drawText("Score: 100", 24.0f, {10, 10}, sgc::Colorf::white());
/// r.drawTextCentered("TITLE", 60.0f, {400, 300}, sgc::Colorf::white());
/// @endcode

#include <DxLib.h>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "sgc/dxlib/TypeConvert.hpp"
#include "sgc/graphics/ITextMeasure.hpp"
#include "sgc/graphics/ITextRenderer.hpp"

namespace sgc::dxlib
{

/// @brief ITextRenderer の DxLib 実装
///
/// フォントサイズ（int）をキーにDxLibフォントハンドルを管理する。
/// 事前にregisterFont()でフォントを登録し、drawText/drawTextCenteredで描画する。
class DxLibTextRenderer : public ITextRenderer, public ITextMeasure
{
public:
	/// @brief デストラクタ（全フォントハンドルを解放する）
	~DxLibTextRenderer() override
	{
		for (const auto& [size, handle] : m_fonts)
		{
			DeleteFontToHandle(handle);
		}
	}

	/// @brief フォントを登録する
	/// @param fontSize フォントサイズ（ピクセル）
	/// @param fontName フォント名（nullptrの場合はデフォルトフォント）
	/// @param fontType フォントタイプ（デフォルト: DX_FONTTYPE_ANTIALIASING_EDGE_4X4）
	void registerFont(int fontSize, const char* fontName = nullptr,
		int fontType = DX_FONTTYPE_ANTIALIASING_EDGE_4X4)
	{
		// 既存のハンドルがあれば先に解放する
		const auto it = m_fonts.find(fontSize);
		if (it != m_fonts.end())
		{
			DeleteFontToHandle(it->second);
		}

		const int handle = CreateFontToHandle(
			fontName, fontSize, -1, fontType);
		m_fonts.insert_or_assign(fontSize, handle);
	}

	/// @brief テキストを左上基準で描画する
	void drawText(
		std::string_view text, float fontSize,
		const Vec2f& pos, const Colorf& color) override
	{
		const int handle = getFont(static_cast<int>(fontSize));
		const std::string str(text);
		DrawStringToHandle(
			static_cast<int>(pos.x),
			static_cast<int>(pos.y),
			str.c_str(),
			toDxColor(color),
			handle);
	}

	/// @brief テキストを中央揃えで描画する
	void drawTextCentered(
		std::string_view text, float fontSize,
		const Vec2f& center, const Colorf& color) override
	{
		const int handle = getFont(static_cast<int>(fontSize));
		const std::string str(text);
		const int textWidth = GetDrawStringWidthToHandle(
			str.c_str(), static_cast<int>(str.size()), handle);
		const int x = static_cast<int>(center.x) - textWidth / 2;
		const int y = static_cast<int>(center.y) - static_cast<int>(fontSize) / 2;
		DrawStringToHandle(x, y, str.c_str(), toDxColor(color), handle);
	}

	// ── ITextMeasure 実装 ──────────────────────────────────

	/// @brief テキストの描画サイズを計測する
	/// @param text テキスト文字列
	/// @param fontSize フォントサイズ
	/// @return テキストの描画サイズ（幅, 高さ）
	[[nodiscard]] Vec2f measure(
		std::string_view text, float fontSize) const override
	{
		const int nearestSize = findNearestSize(static_cast<int>(fontSize));
		const int handle = m_fonts.at(nearestSize);
		const std::string str(text);
		const int width = GetDrawStringWidthToHandle(
			str.c_str(), static_cast<int>(str.size()), handle);
		return Vec2f{static_cast<float>(width), static_cast<float>(nearestSize)};
	}

	/// @brief 1行のテキストの高さを取得する
	/// @param fontSize フォントサイズ
	/// @return 行の高さ
	[[nodiscard]] float lineHeight(float fontSize) const override
	{
		return static_cast<float>(findNearestSize(static_cast<int>(fontSize)));
	}

private:
	std::unordered_map<int, int> m_fonts;  ///< フォントサイズ→フォントハンドル

	/// @brief 指定サイズのフォントハンドルを取得する（描画用、厳密一致）
	/// @param fontSize フォントサイズ
	/// @return フォントハンドル
	/// @throw std::out_of_range 未登録のサイズの場合
	[[nodiscard]] int getFont(int fontSize) const
	{
		const auto it = m_fonts.find(fontSize);
		if (it == m_fonts.end())
		{
			throw std::out_of_range("DxLibTextRenderer: font not registered for size");
		}
		return it->second;
	}

	/// @brief 指定サイズに最も近い登録済みフォントサイズを取得する（計測用）
	///
	/// 完全一致があればそれを返す。なければ登録済みサイズの中から
	/// 絶対差が最小のものを選ぶ。
	///
	/// @param fontSize 要求フォントサイズ
	/// @return 最近接の登録済みフォントサイズ
	/// @throw std::out_of_range フォントが一つも登録されていない場合
	[[nodiscard]] int findNearestSize(int fontSize) const
	{
		const auto it = m_fonts.find(fontSize);
		if (it != m_fonts.end())
		{
			return fontSize;
		}

		if (m_fonts.empty())
		{
			throw std::out_of_range(
				"DxLibTextRenderer: no fonts registered");
		}

		int bestSize = m_fonts.begin()->first;
		int bestDiff = std::abs(fontSize - bestSize);
		for (const auto& [size, handle] : m_fonts)
		{
			const int diff = std::abs(fontSize - size);
			if (diff < bestDiff)
			{
				bestDiff = diff;
				bestSize = size;
			}
		}
		return bestSize;
	}
};

} // namespace sgc::dxlib
