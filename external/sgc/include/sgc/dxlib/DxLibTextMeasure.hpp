#pragma once

/// @file DxLibTextMeasure.hpp
/// @brief ITextMeasure の DxLib 実装
///
/// フォントサイズごとにDxLibフォントハンドルを管理し、
/// ITextMeasureインターフェースでテキスト計測を行う。
/// 未登録サイズは最近接の登録済みサイズにフォールバックする。
///
/// @note このファイルはDxLib.hに依存するため、CI対象外。
///
/// @code
/// sgc::dxlib::DxLibTextMeasure textMeasure;
/// textMeasure.registerFont(24, "ＭＳ ゴシック");
/// textMeasure.registerFont(48);
///
/// sgc::ITextMeasure& m = textMeasure;
/// auto size = m.measure("Hello", 24.0f);
/// float h = m.lineHeight(24.0f);
/// @endcode

#include <DxLib.h>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "sgc/dxlib/TypeConvert.hpp"
#include "sgc/graphics/ITextMeasure.hpp"

namespace sgc::dxlib
{

/// @brief ITextMeasure の DxLib 実装
///
/// フォントサイズ（int）をキーにDxLibフォントハンドルを管理する。
/// 事前にregisterFont()でフォントを登録し、measure/lineHeightで計測する。
/// 未登録サイズが要求された場合、最も近い登録済みサイズのフォントを使う。
class DxLibTextMeasure : public ITextMeasure
{
public:
	/// @brief デストラクタ（全フォントハンドルを解放する）
	~DxLibTextMeasure() override
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

	/// @brief テキストの描画サイズを計測する
	/// @param text テキスト文字列
	/// @param fontSize フォントサイズ
	/// @return テキストの描画サイズ（幅, 高さ）
	[[nodiscard]] Vec2f measure(
		std::string_view text, float fontSize) const override
	{
		const int nearestSize = findNearestSize(static_cast<int>(fontSize));
		const int handle = findNearestHandle(static_cast<int>(fontSize));
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

	/// @brief 指定サイズのフォントハンドルを厳密に取得する
	/// @param fontSize フォントサイズ
	/// @return フォントハンドル
	/// @throw std::out_of_range 未登録のサイズの場合
	[[nodiscard]] int getFont(int fontSize) const
	{
		const auto it = m_fonts.find(fontSize);
		if (it == m_fonts.end())
		{
			throw std::out_of_range(
				"DxLibTextMeasure: font not registered for size");
		}
		return it->second;
	}

	/// @brief 指定サイズまたは最近接サイズのフォントハンドルを取得する
	///
	/// 完全一致があればそれを返す。なければ登録済みサイズの中から
	/// 絶対差が最小のものを選ぶ。フォントが一つも登録されていない場合は
	/// std::out_of_range をスローする。
	///
	/// @param fontSize 要求フォントサイズ
	/// @return フォントハンドル
	/// @throw std::out_of_range フォントが一つも登録されていない場合
	[[nodiscard]] int findNearestHandle(int fontSize) const
	{
		return m_fonts.at(findNearestSize(fontSize));
	}

	/// @brief 指定サイズに最も近い登録済みフォントサイズを取得する
	///
	/// 完全一致があればそれを返す。なければ登録済みサイズの中から
	/// 絶対差が最小のものを選ぶ。フォントが一つも登録されていない場合は
	/// std::out_of_range をスローする。
	///
	/// @param fontSize 要求フォントサイズ
	/// @return 最近接の登録済みフォントサイズ
	/// @throw std::out_of_range フォントが一つも登録されていない場合
	[[nodiscard]] int findNearestSize(int fontSize) const
	{
		// 完全一致
		const auto it = m_fonts.find(fontSize);
		if (it != m_fonts.end())
		{
			return fontSize;
		}

		// 最近接サイズを探索
		if (m_fonts.empty())
		{
			throw std::out_of_range(
				"DxLibTextMeasure: no fonts registered");
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
