#pragma once

/// @file SampleMath.hpp
/// @brief 数学関数ビジュアルデモ
///
/// 左パネル: ベクトル演算（内積・外積）の可視化
/// 右パネル: イージングカーブグラフ + アニメーションドット
/// - マウスでベクトルBの方向を制御
/// - ESC: メニューに戻る

#include <cmath>
#include <numbers>
#include <string>

#include "sgc/core/Hash.hpp"
#include "sgc/math/Easing.hpp"
#include "sgc/math/Interpolation.hpp"
#include "sgc/math/Vec2.hpp"
#include "sgc/scene/App.hpp"

#include "KeyCodes.hpp"
#include "SharedData.hpp"

using namespace sgc::literals;

/// @brief 数学関数可視化デモシーン
class SampleMath : public sgc::AppScene<SharedData>
{
public:
	using AppScene::AppScene;

	/// @brief シーン開始時の初期化
	void onEnter() override
	{
		m_time = 0.0f;
	}

	/// @brief 更新処理
	void update(float dt) override
	{
		const auto* input = getData().inputProvider;

		if (input->isKeyJustPressed(KeyCode::ESCAPE))
		{
			getSceneManager().changeScene("menu"_hash, 0.3f);
			return;
		}

		m_time += dt;
		m_mousePos = input->mousePosition();

		// イージングアニメーション（3秒周期）
		m_easingT += dt / EASING_PERIOD;
		if (m_easingT > 1.0f) m_easingT -= 1.0f;
	}

	/// @brief 描画処理
	void draw() const override
	{
		auto* r = getData().renderer;
		auto* tr = getData().textRenderer;

		r->clearBackground(sgc::Colorf{0.06f, 0.04f, 0.1f, 1.0f});

		// 区切り線
		r->drawLine({400.0f, 40.0f}, {400.0f, 560.0f}, 1.0f,
			sgc::Colorf{0.3f, 0.3f, 0.4f, 0.5f});

		drawVectorPanel(r, tr);
		drawEasingPanel(r, tr);

		// タイトル
		tr->drawTextCentered("Math Visualization", 24.0f,
			{400.0f, 20.0f}, sgc::Colorf{0.8f, 0.7f, 1.0f, 1.0f});

		tr->drawText("ESC: Back to Menu", 14.0f,
			{660.0f, 575.0f}, sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}

private:
	static constexpr float EASING_PERIOD = 3.0f;  ///< イージングアニメーション周期（秒）
	static constexpr float VEC_SCALE = 100.0f;    ///< ベクトル描画スケール

	float m_time{0.0f};
	float m_easingT{0.0f};
	sgc::Vec2f m_mousePos{400.0f, 300.0f};

	/// @brief ベクトル演算パネルを描画する（左半分）
	void drawVectorPanel(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		const sgc::Vec2f origin{180.0f, 320.0f};

		// パネルラベル
		tr->drawTextCentered("Vector Operations", 20.0f,
			{200.0f, 55.0f}, sgc::Colorf{1.0f, 0.8f, 0.5f, 1.0f});

		// 座標軸（薄い線）
		r->drawLine({origin.x - 140.0f, origin.y}, {origin.x + 140.0f, origin.y}, 1.0f,
			sgc::Colorf{0.3f, 0.3f, 0.3f, 0.5f});
		r->drawLine({origin.x, origin.y - 140.0f}, {origin.x, origin.y + 140.0f}, 1.0f,
			sgc::Colorf{0.3f, 0.3f, 0.3f, 0.5f});

		// ベクトルA（固定: 右上方向）
		const sgc::Vec2f vecA{0.8f, -0.5f};
		const sgc::Vec2f vecAEnd = origin + vecA * VEC_SCALE;

		// ベクトルB（マウス方向）
		sgc::Vec2f rawDir = m_mousePos - origin;
		const float rawLen = rawDir.length();
		const sgc::Vec2f vecB = (rawLen > 0.01f) ? rawDir / rawLen : sgc::Vec2f{1.0f, 0.0f};
		const sgc::Vec2f vecBEnd = origin + vecB * VEC_SCALE;

		// ベクトルA描画（赤）
		drawArrow(r, origin, vecAEnd, sgc::Colorf{1.0f, 0.3f, 0.3f, 1.0f});
		tr->drawText("A", 16.0f, {vecAEnd.x + 5.0f, vecAEnd.y - 10.0f},
			sgc::Colorf{1.0f, 0.3f, 0.3f, 1.0f});

		// ベクトルB描画（青）
		drawArrow(r, origin, vecBEnd, sgc::Colorf{0.3f, 0.5f, 1.0f, 1.0f});
		tr->drawText("B", 16.0f, {vecBEnd.x + 5.0f, vecBEnd.y - 10.0f},
			sgc::Colorf{0.3f, 0.5f, 1.0f, 1.0f});

		// 投影ベクトル描画（緑の点線風）
		const sgc::Vec2f projected = vecB.projected(vecA);
		const sgc::Vec2f projEnd = origin + projected * VEC_SCALE;
		r->drawLine(origin, projEnd, 2.0f, sgc::Colorf{0.3f, 1.0f, 0.5f, 0.6f});
		// 垂線
		r->drawLine(vecBEnd, projEnd, 1.0f, sgc::Colorf{0.5f, 0.5f, 0.5f, 0.4f});

		// 内積・外積の計算
		const float dotVal = vecA.dot(vecB);
		const float crossVal = vecA.cross(vecB);
		const float angleRad = std::atan2(crossVal, dotVal);
		const float angleDeg = angleRad * 180.0f / std::numbers::pi_v<float>;

		// 角度の弧
		const int arcSegments = 20;
		const float arcRadius = 40.0f;
		const float startAngle = std::atan2(vecA.y, vecA.x);
		for (int i = 0; i < arcSegments; ++i)
		{
			const float t0 = static_cast<float>(i) / static_cast<float>(arcSegments);
			const float t1 = static_cast<float>(i + 1) / static_cast<float>(arcSegments);
			const float a0 = startAngle + angleRad * t0;
			const float a1 = startAngle + angleRad * t1;
			r->drawLine(
				origin + sgc::Vec2f{std::cos(a0), std::sin(a0)} * arcRadius,
				origin + sgc::Vec2f{std::cos(a1), std::sin(a1)} * arcRadius,
				1.5f, sgc::Colorf{1.0f, 1.0f, 0.3f, 0.6f});
		}

		// 数値表示
		const float infoX = 20.0f;
		const float infoY = 460.0f;

		auto floatStr = [](float v) -> std::string
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.3f", v);
			return buf;
		};

		tr->drawText("Dot(A,B)   = " + floatStr(dotVal), 16.0f,
			{infoX, infoY}, sgc::Colorf{1.0f, 0.9f, 0.5f, 1.0f});
		tr->drawText("Cross(A,B) = " + floatStr(crossVal), 16.0f,
			{infoX, infoY + 22.0f}, sgc::Colorf{0.5f, 1.0f, 0.8f, 1.0f});
		tr->drawText("Angle      = " + floatStr(angleDeg) + " deg", 16.0f,
			{infoX, infoY + 44.0f}, sgc::Colorf{1.0f, 1.0f, 0.3f, 1.0f});
		tr->drawText("|A| = " + floatStr(vecA.length()) + "  |B| = " + floatStr(vecB.length()), 14.0f,
			{infoX, infoY + 68.0f}, sgc::Colorf{0.7f, 0.7f, 0.7f, 1.0f});

		tr->drawText("Mouse controls vector B direction", 14.0f,
			{infoX, infoY + 90.0f}, sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
	}

	/// @brief イージングカーブパネルを描画する（右半分）
	void drawEasingPanel(sgc::IRenderer* r, sgc::ITextRenderer* tr) const
	{
		tr->drawTextCentered("Easing Curves", 20.0f,
			{600.0f, 55.0f}, sgc::Colorf{0.5f, 0.8f, 1.0f, 1.0f});

		// グラフ領域
		const float graphX = 440.0f;
		const float graphY = 90.0f;
		const float graphW = 320.0f;
		const float graphH = 300.0f;

		// グラフ枠
		r->drawRectFrame(
			sgc::AABB2f{{graphX, graphY}, {graphX + graphW, graphY + graphH}},
			1.0f, sgc::Colorf{0.4f, 0.4f, 0.4f, 0.5f});

		// 0/1基準線
		r->drawLine({graphX, graphY + graphH}, {graphX + graphW, graphY + graphH}, 1.0f,
			sgc::Colorf{0.3f, 0.3f, 0.3f, 0.5f}); // y=0
		r->drawLine({graphX, graphY}, {graphX + graphW, graphY}, 1.0f,
			sgc::Colorf{0.3f, 0.3f, 0.3f, 0.5f}); // y=1

		tr->drawText("1.0", 14.0f, {graphX - 30.0f, graphY - 4.0f}, sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});
		tr->drawText("0.0", 14.0f, {graphX - 30.0f, graphY + graphH - 4.0f}, sgc::Colorf{0.5f, 0.5f, 0.5f, 1.0f});

		// イージング関数配列
		struct EasingEntry
		{
			const char* name;
			float (*func)(float);
			sgc::Colorf color;
		};

		const EasingEntry easings[] =
		{
			{"Linear",     sgc::easing::linear<float>,    sgc::Colorf{1.0f, 1.0f, 1.0f, 0.9f}},
			{"InOutQuad",  sgc::easing::inOutQuad<float>, sgc::Colorf{1.0f, 0.4f, 0.4f, 0.9f}},
			{"OutCubic",   sgc::easing::outCubic<float>,  sgc::Colorf{0.4f, 1.0f, 0.4f, 0.9f}},
			{"InOutSine",  sgc::easing::inOutSine<float>, sgc::Colorf{0.4f, 0.6f, 1.0f, 0.9f}},
			{"OutBounce",  sgc::easing::outBounce<float>, sgc::Colorf{1.0f, 0.8f, 0.2f, 0.9f}},
		};

		constexpr int SEGMENTS = 60;
		constexpr int EASING_COUNT = 5;

		for (int e = 0; e < EASING_COUNT; ++e)
		{
			const auto& entry = easings[e];

			// カーブ描画
			for (int i = 0; i < SEGMENTS; ++i)
			{
				const float t0 = static_cast<float>(i) / static_cast<float>(SEGMENTS);
				const float t1 = static_cast<float>(i + 1) / static_cast<float>(SEGMENTS);
				const float v0 = entry.func(t0);
				const float v1 = entry.func(t1);

				const float x0 = graphX + t0 * graphW;
				const float x1 = graphX + t1 * graphW;
				const float y0 = graphY + graphH - v0 * graphH;
				const float y1 = graphY + graphH - v1 * graphH;

				r->drawLine({x0, y0}, {x1, y1}, 2.0f, entry.color);
			}

			// アニメーションドット
			const float dotVal = entry.func(m_easingT);
			const float dotX = graphX + m_easingT * graphW;
			const float dotY = graphY + graphH - dotVal * graphH;
			r->drawCircle({dotX, dotY}, 5.0f, entry.color);
			r->drawCircleFrame({dotX, dotY}, 5.0f, 1.5f, sgc::Colorf::white());
		}

		// 進行バー（下部）
		const float barY = graphY + graphH + 10.0f;
		r->drawRectFrame(
			sgc::AABB2f{{graphX, barY}, {graphX + graphW, barY + 6.0f}},
			1.0f, sgc::Colorf{0.4f, 0.4f, 0.4f, 0.5f});
		r->drawRect(
			sgc::AABB2f{{graphX, barY}, {graphX + m_easingT * graphW, barY + 6.0f}},
			sgc::Colorf{1.0f, 1.0f, 1.0f, 0.6f});

		// 凡例
		const float legendX = 440.0f;
		float legendY = graphY + graphH + 30.0f;

		for (int e = 0; e < EASING_COUNT; ++e)
		{
			const auto& entry = easings[e];
			const float val = entry.func(m_easingT);

			r->drawRect(
				sgc::AABB2f{{legendX, legendY + 2.0f}, {legendX + 14.0f, legendY + 14.0f}},
				entry.color);

			char valBuf[32];
			std::snprintf(valBuf, sizeof(valBuf), "%.2f", val);
			tr->drawText(std::string(entry.name) + "  = " + valBuf, 14.0f,
				{legendX + 20.0f, legendY}, sgc::Colorf::white());

			legendY += 20.0f;
		}
	}

	/// @brief 矢印を描画する
	/// @param r レンダラー
	/// @param from 始点
	/// @param to 終点
	/// @param color 色
	void drawArrow(sgc::IRenderer* r, const sgc::Vec2f& from, const sgc::Vec2f& to, const sgc::Colorf& color) const
	{
		r->drawLine(from, to, 2.5f, color);

		// 矢じり
		const sgc::Vec2f dir = (to - from).normalized();
		const sgc::Vec2f perp = dir.perpendicular();
		const float arrowSize = 10.0f;
		const sgc::Vec2f tip = to;
		const sgc::Vec2f left = tip - dir * arrowSize + perp * (arrowSize * 0.4f);
		const sgc::Vec2f right = tip - dir * arrowSize - perp * (arrowSize * 0.4f);
		r->drawTriangle(tip, left, right, color);
	}
};
