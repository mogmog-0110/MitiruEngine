#pragma once

/// @file GameAssetEffects.hpp
/// @brief エフェクト・ハザード系ゲームアセットSVGテンプレート（スパイク・レーザー）

#include "SvgGenerator.hpp"
#include "GameAssetUtil.hpp"

#include <string>

namespace mitiru::asset
{

/// @brief エフェクト・ハザード系SVGテンプレート
class GameAssetEffects
{
public:
	/// @brief スパイク障害物（三角形のトゲ列）
	/// @param w 全体幅
	/// @param h トゲの高さ
	/// @param count トゲの数
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument spikeHazard(float w = 100.0f, float h = 20.0f, int count = 5)
	{
		const std::string color = "#ff4444";
		const float padding = 4.0f;

		SvgDocument doc;
		doc.viewBoxW = w + padding * 2;
		doc.viewBoxH = h + padding * 2;
		doc.title = "SpikeHazard";

		const float spikeW = w / static_cast<float>(count);
		for (int i = 0; i < count; ++i)
		{
			SvgElement spike;
			spike.shape = SvgShape::Triangle;
			const float sx = padding + spikeW * static_cast<float>(i);
			spike.points = {
				{sx + spikeW * 0.5f, padding},
				{sx, padding + h},
				{sx + spikeW, padding + h}
			};
			spike.style.fillColor = GameAssetUtil::darkenColor(color);
			spike.style.strokeColor = color;
			spike.style.strokeWidth = 1.0f;
			spike.style.glowColor = color;
			spike.style.glowRadius = 3.0f;
			doc.elements.push_back(spike);
		}

		return doc;
	}

	/// @brief レーザーバリア（水平ビーム＋グロー）
	/// @param length ビームの長さ
	/// @param color ネオン色
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument laserBarrier(float length = 150.0f,
		const std::string& color = "#ff00ff")
	{
		const float beamH = 4.0f;
		const float padding = 16.0f;

		SvgDocument doc;
		doc.viewBoxW = length + padding * 2;
		doc.viewBoxH = beamH * 6 + padding * 2;
		doc.title = "LaserBarrier";

		const float cy = doc.viewBoxH * 0.5f;

		// 外側グロー
		SvgElement glow;
		glow.shape = SvgShape::Rect;
		glow.x = padding;
		glow.y = cy - beamH * 2;
		glow.width = length;
		glow.height = beamH * 4;
		glow.style.fillColor = color;
		glow.style.opacity = 0.2f;
		glow.style.glowColor = color;
		glow.style.glowRadius = 8.0f;
		doc.elements.push_back(glow);

		// コアビーム
		SvgElement beam;
		beam.shape = SvgShape::Rect;
		beam.x = padding;
		beam.y = cy - beamH * 0.5f;
		beam.width = length;
		beam.height = beamH;
		beam.style.fillColor = "white";
		beam.style.strokeColor = color;
		beam.style.strokeWidth = 1.0f;
		beam.style.glowColor = color;
		beam.style.glowRadius = 4.0f;
		doc.elements.push_back(beam);

		// エミッター（両端の円）
		for (float ex : {padding, padding + length})
		{
			SvgElement emitter;
			emitter.shape = SvgShape::Circle;
			emitter.x = ex;
			emitter.y = cy;
			emitter.radius = beamH * 1.5f;
			emitter.style.fillColor = GameAssetUtil::darkenColor(color);
			emitter.style.strokeColor = color;
			emitter.style.strokeWidth = 1.5f;
			doc.elements.push_back(emitter);
		}

		return doc;
	}
};

} // namespace mitiru::asset
