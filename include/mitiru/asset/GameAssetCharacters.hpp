#pragma once

/// @file GameAssetCharacters.hpp
/// @brief キャラクター系ゲームアセットSVGテンプレート（プレイヤー・NPC・敵）

#include "SvgGenerator.hpp"
#include "GameAssetUtil.hpp"

#include <string>

namespace mitiru::asset
{

/// @brief キャラクター系SVGテンプレート
class GameAssetCharacters
{
public:
	/// @brief プレイヤー（サイバースフィア＋ネオントレイルグロー）
	/// @param radius 球体の半径
	/// @param neonColor ネオングロー色（デフォルト: シアン）
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument player(float radius = 20.0f,
		const std::string& neonColor = "#00ffff")
	{
		const float size = radius * 4.0f;
		SvgDocument doc;
		doc.viewBoxW = size;
		doc.viewBoxH = size;
		doc.title = "Player";

		const float cx = size * 0.5f;
		const float cy = size * 0.5f;

		// 外側グロー（大きめの円）
		SvgElement outerGlow;
		outerGlow.shape = SvgShape::Circle;
		outerGlow.x = cx;
		outerGlow.y = cy;
		outerGlow.radius = radius * 1.3f;
		outerGlow.style.fillColor = neonColor;
		outerGlow.style.opacity = 0.3f;
		outerGlow.style.glowColor = neonColor;
		outerGlow.style.glowRadius = radius * 0.5f;
		doc.elements.push_back(outerGlow);

		// 本体球
		SvgElement body;
		body.shape = SvgShape::Circle;
		body.x = cx;
		body.y = cy;
		body.radius = radius;
		body.style.fillColor = GameAssetUtil::darkenColor(neonColor);
		body.style.strokeColor = neonColor;
		body.style.strokeWidth = 2.0f;
		body.style.glowColor = neonColor;
		body.style.glowRadius = 6.0f;
		doc.elements.push_back(body);

		// ハイライト（内側の光沢）
		SvgElement highlight;
		highlight.shape = SvgShape::Circle;
		highlight.x = cx - radius * 0.25f;
		highlight.y = cy - radius * 0.25f;
		highlight.radius = radius * 0.35f;
		highlight.style.fillColor = "white";
		highlight.style.opacity = 0.4f;
		doc.elements.push_back(highlight);

		return doc;
	}

	/// @brief NPC（人型シルエット＋吹き出し）
	/// @param size サイズ
	/// @param bodyColor 体の色
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument npc(float size = 40.0f,
		const std::string& bodyColor = "#8888ff")
	{
		const float padding = 12.0f;
		SvgDocument doc;
		doc.viewBoxW = size * 1.5f + padding * 2;
		doc.viewBoxH = size * 1.8f + padding * 2;
		doc.title = "NPC";

		const float cx = doc.viewBoxW * 0.5f;
		const float y0 = padding;

		// 頭部
		SvgElement head;
		head.shape = SvgShape::Circle;
		head.x = cx;
		head.y = y0 + size * 0.2f;
		head.radius = size * 0.2f;
		head.style.fillColor = GameAssetUtil::darkenColor(bodyColor);
		head.style.strokeColor = bodyColor;
		head.style.strokeWidth = 1.5f;
		head.style.glowColor = bodyColor;
		head.style.glowRadius = 3.0f;
		doc.elements.push_back(head);

		// 胴体
		SvgElement torso;
		torso.shape = SvgShape::Rect;
		torso.x = cx - size * 0.2f;
		torso.y = y0 + size * 0.42f;
		torso.width = size * 0.4f;
		torso.height = size * 0.5f;
		torso.style.fillColor = GameAssetUtil::darkenColor(bodyColor);
		torso.style.strokeColor = bodyColor;
		torso.style.strokeWidth = 1.5f;
		doc.elements.push_back(torso);

		// 吹き出し記号
		SvgElement bubble;
		bubble.shape = SvgShape::Circle;
		bubble.x = cx + size * 0.45f;
		bubble.y = y0 + size * 0.05f;
		bubble.radius = size * 0.08f;
		bubble.style.fillColor = "white";
		bubble.style.opacity = 0.7f;
		doc.elements.push_back(bubble);

		return doc;
	}

	/// @brief 巡回敵（威嚇的な形状＋目）
	/// @param size サイズ
	/// @param color 敵の色
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument enemy(float size = 32.0f,
		const std::string& color = "#ff4444")
	{
		const float padding = 8.0f;
		SvgDocument doc;
		doc.viewBoxW = size + padding * 2;
		doc.viewBoxH = size + padding * 2;
		doc.title = "PatrolEnemy";

		const float cx = doc.viewBoxW * 0.5f;
		const float cy = doc.viewBoxH * 0.5f;
		const float half = size * 0.5f;

		// 本体（角ばった多角形）
		SvgElement body;
		body.shape = SvgShape::Polygon;
		body.points = {
			{cx, cy - half},
			{cx + half * 0.8f, cy - half * 0.3f},
			{cx + half, cy + half * 0.3f},
			{cx + half * 0.6f, cy + half},
			{cx - half * 0.6f, cy + half},
			{cx - half, cy + half * 0.3f},
			{cx - half * 0.8f, cy - half * 0.3f}
		};
		body.style.fillColor = GameAssetUtil::darkenColor(color);
		body.style.strokeColor = color;
		body.style.strokeWidth = 2.0f;
		body.style.glowColor = color;
		body.style.glowRadius = 5.0f;
		doc.elements.push_back(body);

		// 左目
		SvgElement leftEye;
		leftEye.shape = SvgShape::Circle;
		leftEye.x = cx - size * 0.15f;
		leftEye.y = cy - size * 0.05f;
		leftEye.radius = size * 0.08f;
		leftEye.style.fillColor = "white";
		leftEye.style.glowColor = "white";
		leftEye.style.glowRadius = 2.0f;
		doc.elements.push_back(leftEye);

		// 右目
		SvgElement rightEye;
		rightEye.shape = SvgShape::Circle;
		rightEye.x = cx + size * 0.15f;
		rightEye.y = cy - size * 0.05f;
		rightEye.radius = size * 0.08f;
		rightEye.style.fillColor = "white";
		rightEye.style.glowColor = "white";
		rightEye.style.glowRadius = 2.0f;
		doc.elements.push_back(rightEye);

		return doc;
	}
};

} // namespace mitiru::asset
