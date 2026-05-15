#pragma once

/// @file GameAssetEnvironment.hpp
/// @brief 環境系ゲームアセットSVGテンプレート（プラットフォーム・チェックポイント・ゲート・ゴール）

#include "SvgGenerator.hpp"
#include "GameAssetUtil.hpp"

#include <cmath>
#include <sstream>
#include <string>

namespace mitiru::asset
{

/// @brief 環境系SVGテンプレート
class GameAssetEnvironment
{
public:
	/// @brief プラットフォーム（ネオンエッジグロー付き矩形）
	/// @param w 幅
	/// @param h 高さ
	/// @param color ネオン色
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument platform(float w = 120.0f, float h = 20.0f,
		const std::string& color = "#00ff88")
	{
		const float padding = 8.0f;
		SvgDocument doc;
		doc.viewBoxW = w + padding * 2;
		doc.viewBoxH = h + padding * 2;
		doc.title = "Platform";

		// 本体
		SvgElement body;
		body.shape = SvgShape::Rect;
		body.x = padding;
		body.y = padding;
		body.width = w;
		body.height = h;
		body.style.fillColor = GameAssetUtil::darkenColor(color);
		body.style.strokeColor = color;
		body.style.strokeWidth = 2.0f;
		body.style.glowColor = color;
		body.style.glowRadius = 4.0f;
		doc.elements.push_back(body);

		// 上面ハイライトライン
		SvgElement topLine;
		topLine.shape = SvgShape::Rect;
		topLine.x = padding + 2.0f;
		topLine.y = padding;
		topLine.width = w - 4.0f;
		topLine.height = 2.0f;
		topLine.style.fillColor = color;
		topLine.style.opacity = 0.8f;
		doc.elements.push_back(topLine);

		return doc;
	}

	/// @brief 移動プラットフォーム（方向矢印付き）
	/// @param w 幅
	/// @param h 高さ
	/// @param color ネオン色
	/// @param arrowDir 矢印方向（"left", "right", "up", "down"）
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument movingPlatform(float w = 120.0f, float h = 20.0f,
		const std::string& color = "#00ff88", const std::string& arrowDir = "right")
	{
		auto doc = platform(w, h, color);
		doc.title = "MovingPlatform";

		const float padding = 8.0f;
		const float cx = padding + w * 0.5f;
		const float cy = padding + h * 0.5f;
		const float arrowSize = h * 0.4f;

		// 方向矢印
		SvgElement arrow;
		arrow.shape = SvgShape::Triangle;

		if (arrowDir == "right")
		{
			arrow.points = {
				{cx + arrowSize, cy},
				{cx - arrowSize, cy - arrowSize},
				{cx - arrowSize, cy + arrowSize}
			};
		}
		else if (arrowDir == "left")
		{
			arrow.points = {
				{cx - arrowSize, cy},
				{cx + arrowSize, cy - arrowSize},
				{cx + arrowSize, cy + arrowSize}
			};
		}
		else if (arrowDir == "up")
		{
			arrow.points = {
				{cx, cy - arrowSize},
				{cx - arrowSize, cy + arrowSize},
				{cx + arrowSize, cy + arrowSize}
			};
		}
		else
		{
			arrow.points = {
				{cx, cy + arrowSize},
				{cx - arrowSize, cy - arrowSize},
				{cx + arrowSize, cy - arrowSize}
			};
		}
		arrow.style.fillColor = "white";
		arrow.style.opacity = 0.6f;
		doc.elements.push_back(arrow);

		return doc;
	}

	/// @brief 崩れるプラットフォーム（ひび割れ模様）
	/// @param w 幅
	/// @param h 高さ
	/// @param color ネオン色
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument crumblingPlatform(float w = 120.0f, float h = 20.0f,
		const std::string& color = "#ff8800")
	{
		auto doc = platform(w, h, color);
		doc.title = "CrumblingPlatform";

		const float padding = 8.0f;

		// ひび割れライン群
		const float x0 = padding;
		const float y0 = padding;
		for (int i = 0; i < 3; ++i)
		{
			SvgElement crack;
			crack.shape = SvgShape::Path;
			const float startX = x0 + w * (0.2f + 0.3f * static_cast<float>(i));
			std::ostringstream path;
			path << "M " << startX << " " << y0
				 << " L " << (startX + w * 0.05f) << " " << (y0 + h * 0.4f)
				 << " L " << (startX - w * 0.03f) << " " << (y0 + h * 0.7f)
				 << " L " << (startX + w * 0.02f) << " " << (y0 + h);
			crack.pathData = path.str();
			crack.style.fillColor = "none";
			crack.style.strokeColor = color;
			crack.style.strokeWidth = 1.0f;
			crack.style.opacity = 0.7f;
			doc.elements.push_back(crack);
		}

		return doc;
	}

	/// @brief バネプラットフォーム（コイルバネ付き）
	/// @param w 幅
	/// @param h 高さ
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument springPlatform(float w = 80.0f, float h = 20.0f)
	{
		const std::string color = "#ffff00";
		const float springH = 24.0f;
		const float padding = 8.0f;

		SvgDocument doc;
		doc.viewBoxW = w + padding * 2;
		doc.viewBoxH = h + springH + padding * 2;
		doc.title = "SpringPlatform";

		// プラットフォーム本体
		SvgElement body;
		body.shape = SvgShape::Rect;
		body.x = padding;
		body.y = padding;
		body.width = w;
		body.height = h;
		body.style.fillColor = GameAssetUtil::darkenColor(color);
		body.style.strokeColor = color;
		body.style.strokeWidth = 2.0f;
		body.style.glowColor = color;
		body.style.glowRadius = 3.0f;
		doc.elements.push_back(body);

		// コイルバネ（ジグザグパス）
		SvgElement spring;
		spring.shape = SvgShape::Path;
		const float sx = padding + w * 0.5f;
		const float sy = padding + h;
		const float coilW = w * 0.25f;
		const int coils = 4;
		std::ostringstream path;
		path << "M " << sx << " " << sy;
		for (int i = 0; i < coils; ++i)
		{
			const float dy = springH / static_cast<float>(coils);
			const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
			path << " L " << (sx + sign * coilW) << " " << (sy + dy * (static_cast<float>(i) + 0.5f));
		}
		path << " L " << sx << " " << (sy + springH);
		spring.pathData = path.str();
		spring.style.fillColor = "none";
		spring.style.strokeColor = color;
		spring.style.strokeWidth = 2.5f;
		spring.style.glowColor = color;
		spring.style.glowRadius = 3.0f;
		doc.elements.push_back(spring);

		return doc;
	}

	/// @brief チェックポイント（フラグ/ビーコン）
	/// @param size サイズ
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument checkpoint(float size = 40.0f)
	{
		const std::string color = "#00ff88";
		const float padding = 8.0f;

		SvgDocument doc;
		doc.viewBoxW = size + padding * 2;
		doc.viewBoxH = size * 1.5f + padding * 2;
		doc.title = "Checkpoint";

		const float x0 = padding;
		const float y0 = padding;

		// ポール
		SvgElement pole;
		pole.shape = SvgShape::Rect;
		pole.x = x0 + size * 0.45f;
		pole.y = y0;
		pole.width = size * 0.1f;
		pole.height = size * 1.5f;
		pole.style.fillColor = "#444444";
		pole.style.strokeColor = color;
		pole.style.strokeWidth = 1.0f;
		doc.elements.push_back(pole);

		// フラグ
		SvgElement flag;
		flag.shape = SvgShape::Triangle;
		flag.points = {
			{x0 + size * 0.5f, y0 + size * 0.1f},
			{x0 + size, y0 + size * 0.3f},
			{x0 + size * 0.5f, y0 + size * 0.5f}
		};
		flag.style.fillColor = color;
		flag.style.strokeColor = "white";
		flag.style.strokeWidth = 1.0f;
		flag.style.glowColor = color;
		flag.style.glowRadius = 4.0f;
		doc.elements.push_back(flag);

		// ベースの光
		SvgElement base;
		base.shape = SvgShape::Circle;
		base.x = x0 + size * 0.5f;
		base.y = y0 + size * 1.5f;
		base.radius = size * 0.15f;
		base.style.fillColor = color;
		base.style.opacity = 0.6f;
		base.style.glowColor = color;
		base.style.glowRadius = 4.0f;
		doc.elements.push_back(base);

		return doc;
	}

	/// @brief ゲート（バリア＋ロックアイコン）
	/// @param w 幅
	/// @param h 高さ
	/// @param locked ロック状態か
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument gate(float w = 20.0f, float h = 80.0f, bool locked = true)
	{
		const std::string color = locked ? "#ff8800" : "#00ff88";
		const float padding = 8.0f;

		SvgDocument doc;
		doc.viewBoxW = w + padding * 2;
		doc.viewBoxH = h + padding * 2;
		doc.title = locked ? "Gate (Locked)" : "Gate (Unlocked)";

		// バリア本体
		SvgElement barrier;
		barrier.shape = SvgShape::Rect;
		barrier.x = padding;
		barrier.y = padding;
		barrier.width = w;
		barrier.height = h;
		barrier.style.fillColor = GameAssetUtil::darkenColor(color);
		barrier.style.strokeColor = color;
		barrier.style.strokeWidth = 2.0f;
		barrier.style.glowColor = color;
		barrier.style.glowRadius = 4.0f;
		barrier.style.opacity = locked ? 0.9f : 0.5f;
		doc.elements.push_back(barrier);

		// 横線パターン
		const int lines = 5;
		const float spacing = h / static_cast<float>(lines + 1);
		for (int i = 1; i <= lines; ++i)
		{
			SvgElement line;
			line.shape = SvgShape::Rect;
			line.x = padding;
			line.y = padding + spacing * static_cast<float>(i) - 0.5f;
			line.width = w;
			line.height = 1.0f;
			line.style.fillColor = color;
			line.style.opacity = 0.5f;
			doc.elements.push_back(line);
		}

		// ロックアイコン（中央の円）
		if (locked)
		{
			SvgElement lockIcon;
			lockIcon.shape = SvgShape::Circle;
			lockIcon.x = padding + w * 0.5f;
			lockIcon.y = padding + h * 0.5f;
			lockIcon.radius = w * 0.3f;
			lockIcon.style.fillColor = color;
			lockIcon.style.strokeColor = "white";
			lockIcon.style.strokeWidth = 1.5f;
			doc.elements.push_back(lockIcon);
		}

		return doc;
	}

	/// @brief ゴール（スター/ポータル）
	/// @param size サイズ
	/// @return SvgDocument
	[[nodiscard]] static SvgDocument goal(float size = 48.0f)
	{
		const std::string color = "#ffff00";
		const float padding = 12.0f;

		SvgDocument doc;
		doc.viewBoxW = size + padding * 2;
		doc.viewBoxH = size + padding * 2;
		doc.title = "Goal";

		const float cx = doc.viewBoxW * 0.5f;
		const float cy = doc.viewBoxH * 0.5f;

		// 外側回転リング
		SvgElement ring;
		ring.shape = SvgShape::Circle;
		ring.x = cx;
		ring.y = cy;
		ring.radius = size * 0.45f;
		ring.style.fillColor = "none";
		ring.style.strokeColor = color;
		ring.style.strokeWidth = 2.5f;
		ring.style.glowColor = color;
		ring.style.glowRadius = 6.0f;
		doc.elements.push_back(ring);

		// 内側リング
		SvgElement innerRing;
		innerRing.shape = SvgShape::Circle;
		innerRing.x = cx;
		innerRing.y = cy;
		innerRing.radius = size * 0.3f;
		innerRing.style.fillColor = "none";
		innerRing.style.strokeColor = color;
		innerRing.style.strokeWidth = 1.5f;
		innerRing.style.opacity = 0.7f;
		doc.elements.push_back(innerRing);

		// 中央のスター（五芒星パス）
		SvgElement star;
		star.shape = SvgShape::Polygon;
		const float outerR = size * 0.2f;
		const float innerR = size * 0.08f;
		for (int i = 0; i < 10; ++i)
		{
			const float angle = static_cast<float>(i) * 3.14159265f / 5.0f - 3.14159265f / 2.0f;
			const float r = (i % 2 == 0) ? outerR : innerR;
			star.points.push_back({
				cx + r * std::cos(angle),
				cy + r * std::sin(angle)
			});
		}
		star.style.fillColor = color;
		star.style.glowColor = color;
		star.style.glowRadius = 4.0f;
		doc.elements.push_back(star);

		return doc;
	}
};

} // namespace mitiru::asset
