#pragma once

/// @file GameAssetUI.hpp
/// @brief UI系ゲームアセットSVGテンプレート（収集アイテム・ボタン）

#include "SvgGenerator.hpp"
#include "GameAssetUtil.hpp"

#include <string>

namespace mitiru::asset
{

/// @brief UI系SVGテンプレート
class GameAssetUI
{
public:
	/// @brief 収集アイテム（光るジェム/クリスタル）
	/// @param size ジェムのサイズ
	/// @param color ネオン色
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument collectible(float size = 24.0f,
		const std::string& color = "#00ffff")
	{
		const float padding = 8.0f;
		SvgDocument doc;
		doc.viewBoxW = size + padding * 2;
		doc.viewBoxH = size * 1.2f + padding * 2;
		doc.title = "Collectible";

		const float cx = doc.viewBoxW * 0.5f;
		const float cy = doc.viewBoxH * 0.5f;
		const float half = size * 0.5f;

		// ダイヤモンド形状（回転した正方形）
		SvgElement gem;
		gem.shape = SvgShape::Polygon;
		gem.points = {
			{cx, cy - half * 1.2f},         // 上
			{cx + half, cy},                  // 右
			{cx, cy + half * 1.2f},          // 下
			{cx - half, cy}                   // 左
		};
		gem.style.fillColor = GameAssetUtil::darkenColor(color);
		gem.style.strokeColor = color;
		gem.style.strokeWidth = 2.0f;
		gem.style.glowColor = color;
		gem.style.glowRadius = 6.0f;
		doc.elements.push_back(gem);

		// 中央のハイライト
		SvgElement shine;
		shine.shape = SvgShape::Polygon;
		shine.points = {
			{cx, cy - half * 0.5f},
			{cx + half * 0.4f, cy},
			{cx, cy + half * 0.5f},
			{cx - half * 0.4f, cy}
		};
		shine.style.fillColor = "white";
		shine.style.opacity = 0.5f;
		doc.elements.push_back(shine);

		return doc;
	}

	/// @brief 数式ボタン（電卓スタイルボタン）
	/// @param label ボタンラベル（"sin", "cos", "+", "-" 等）
	/// @param size ボタンサイズ
	/// @param color ネオン色
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument formulaButton(const std::string& label = "f(x)",
		float size = 48.0f, const std::string& color = "#00ffff")
	{
		const float padding = 4.0f;
		SvgDocument doc;
		doc.viewBoxW = size + padding * 2;
		doc.viewBoxH = size + padding * 2;
		doc.title = "FormulaButton: " + label;

		// ボタン背景
		SvgElement bg;
		bg.shape = SvgShape::Rect;
		bg.x = padding;
		bg.y = padding;
		bg.width = size;
		bg.height = size;
		bg.style.fillColor = GameAssetUtil::darkenColor(color);
		bg.style.strokeColor = color;
		bg.style.strokeWidth = 1.5f;
		bg.style.glowColor = color;
		bg.style.glowRadius = 3.0f;
		doc.elements.push_back(bg);

		// ボタンラベル
		SvgElement text;
		text.shape = SvgShape::Text;
		text.x = padding + size * 0.5f;
		text.y = padding + size * 0.55f;
		text.text = label;
		text.fontSize = size * 0.35f;
		text.style.fillColor = color;
		doc.elements.push_back(text);

		return doc;
	}
};

} // namespace mitiru::asset
