#pragma once

/// @file GameAssetTemplates.hpp
/// @brief GraphWalker用ゲームオブジェクトのSVGテンプレート集
/// @details サイバーパンク風ネオングロー付きのゲームアセットを
///          プログラム的に生成する静的メソッド群。
///
/// @code
/// auto playerSvg = mitiru::asset::GameAssetTemplates::player(20.0f, "#00ffff");
/// auto svgStr = mitiru::asset::SvgGenerator::toSvg(playerSvg);
/// @endcode

#include "SvgGenerator.hpp"

#include <cmath>
#include <string>

namespace mitiru::asset
{

/// @brief GraphWalkerゲームオブジェクトのSVGテンプレート群
/// @details 各メソッドはネオングロー付きのSvgDocumentを返す。
///          カラーパレットはGraphWalkerのゾーン配色に準拠。
class GameAssetTemplates
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
		body.style.fillColor = darkenColor(neonColor);
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
		body.style.fillColor = darkenColor(color);
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
		body.style.fillColor = darkenColor(color);
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
			spike.style.fillColor = darkenColor(color);
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
			emitter.style.fillColor = darkenColor(color);
			emitter.style.strokeColor = color;
			emitter.style.strokeWidth = 1.5f;
			doc.elements.push_back(emitter);
		}

		return doc;
	}

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
		gem.style.fillColor = darkenColor(color);
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
		head.style.fillColor = darkenColor(bodyColor);
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
		torso.style.fillColor = darkenColor(bodyColor);
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
		body.style.fillColor = darkenColor(color);
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
		barrier.style.fillColor = darkenColor(color);
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
		bg.style.fillColor = darkenColor(color);
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

private:
	/// @brief ネオン色を暗くした背景色を生成する
	/// @param hexColor #RRGGBB形式の色
	/// @return 暗くした色（#RRGGBB形式）
	[[nodiscard]] static std::string darkenColor(const std::string& hexColor)
	{
		if (hexColor.size() < 7 || hexColor[0] != '#')
		{
			return "#111111";
		}

		auto parseHex = [](const std::string& s, size_t pos) -> int
		{
			int val = 0;
			for (size_t i = 0; i < 2; ++i)
			{
				char c = s[pos + i];
				val *= 16;
				if (c >= '0' && c <= '9')
				{
					val += c - '0';
				}
				else if (c >= 'a' && c <= 'f')
				{
					val += c - 'a' + 10;
				}
				else if (c >= 'A' && c <= 'F')
				{
					val += c - 'A' + 10;
				}
			}
			return val;
		};

		const int r = parseHex(hexColor, 1) / 4;
		const int g = parseHex(hexColor, 3) / 4;
		const int b = parseHex(hexColor, 5) / 4;

		char buf[8];
		std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
		return std::string(buf);
	}
};

} // namespace mitiru::asset
