#pragma once

/// @file GradientRenderer.hpp
/// @brief 高機能グラデーションレンダラー
/// @details 線形・放射状・円錐グラデーションを複数カラーストップで描画する。
///          Screenの既存drawRect/drawCircleを活用したソフトウェア実装。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::render
{

// ── グラデーション要素 ─────────────────────────────────────

/// @brief グラデーションの色停止点
/// @details position は [0, 1] の範囲で、グラデーション上の位置を示す。
struct GradientStop
{
	float position = 0.0f;     ///< グラデーション上の位置 [0, 1]
	sgc::Colorf color{};       ///< この位置での色

	constexpr GradientStop() noexcept = default;

	constexpr GradientStop(float position, const sgc::Colorf& color) noexcept
		: position(position), color(color)
	{
	}
};

/// @brief 線形グラデーション定義
struct LinearGradient
{
	float angleDegrees = 0.0f;          ///< 角度（度数法、0=上→下、90=左→右）
	std::vector<GradientStop> stops;    ///< カラーストップ（position順にソート推奨）
};

/// @brief 放射状グラデーション定義
struct RadialGradient
{
	float centerX = 0.5f;               ///< 中心X座標（矩形内の正規化座標 [0, 1]）
	float centerY = 0.5f;               ///< 中心Y座標（矩形内の正規化座標 [0, 1]）
	float radius = 0.5f;                ///< 半径（矩形の短辺を1とした正規化値）
	std::vector<GradientStop> stops;    ///< カラーストップ
};

/// @brief 円錐（コニカル）グラデーション定義
struct ConicGradient
{
	float centerX = 0.5f;               ///< 中心X座標（矩形内の正規化座標 [0, 1]）
	float centerY = 0.5f;               ///< 中心Y座標（矩形内の正規化座標 [0, 1]）
	float startAngleDegrees = 0.0f;     ///< 開始角度（度数法、12時方向=0、時計回り）
	std::vector<GradientStop> stops;    ///< カラーストップ
};

// ── グラデーションレンダラー ───────────────────────────────

/// @brief グラデーション描画エンジン
/// @details 任意角度・複数カラーストップの線形・放射状・円錐グラデーションを
///          ScreenのdrawRectに委譲して描画する。
///
///          描画戦略: 矩形をスキャンラインに分割し、各ピクセル行のグラデーション値を
///          計算してバンド幅のdrawRect呼び出しにマップする。
///
/// @code
/// mitiru::render::GradientRenderer grad;
///
/// mitiru::render::LinearGradient lg;
/// lg.angleDegrees = 45.0f;
/// lg.stops = {{0.0f, sgc::Colorf::red()}, {1.0f, sgc::Colorf::blue()}};
/// grad.drawLinearGradient(screen, {10, 10, 200, 100}, lg);
///
/// mitiru::render::RadialGradient rg;
/// rg.stops = {{0.0f, sgc::Colorf::white()}, {1.0f, {0, 0, 0, 0}}};
/// grad.drawRadialGradient(screen, {50, 50, 128, 128}, rg);
/// @endcode
class GradientRenderer
{
public:
	/// @brief 線形グラデーションを描画する
	void drawLinearGradient(Screen& screen,
	                        const sgc::Rectf& rect,
	                        const LinearGradient& gradient) const;

	/// @brief 放射状グラデーションを描画する
	void drawRadialGradient(Screen& screen,
	                        const sgc::Rectf& rect,
	                        const RadialGradient& gradient) const;

	/// @brief 円錐（コニカル）グラデーションを描画する
	void drawConicGradient(Screen& screen,
	                       const sgc::Rectf& rect,
	                       const ConicGradient& gradient) const;

	/// @brief 簡易線形グラデーション（角度指定 + ストップ配列版）
	void drawLinearGradient(Screen& screen,
	                        const sgc::Rectf& rect,
	                        float angleDegrees,
	                        const std::vector<GradientStop>& stops) const;

	/// @brief 簡易放射状グラデーション
	void drawRadialGradient(Screen& screen,
	                        const sgc::Rectf& rect,
	                        float centerX, float centerY, float radius,
	                        const std::vector<GradientStop>& stops) const;

	/// @brief 簡易円錐グラデーション
	void drawConicGradient(Screen& screen,
	                       const sgc::Rectf& rect,
	                       float centerX, float centerY,
	                       float startAngleDegrees,
	                       const std::vector<GradientStop>& stops) const;

private:
	static constexpr float PI = 3.14159265358979323846f;
	static constexpr float DEG_TO_RAD = PI / 180.0f;

	/// @brief グラデーション帯の最大幅（ピクセル）
	static constexpr float BAND_WIDTH = 2.0f;

	/// @brief カラーストップをposition順にソートする
	[[nodiscard]] static std::vector<GradientStop> sortStops(
		const std::vector<GradientStop>& stops)
	{
		auto sorted = stops;
		std::sort(sorted.begin(), sorted.end(),
		          [](const GradientStop& a, const GradientStop& b) {
			return a.position < b.position;
		});
		return sorted;
	}

	/// @brief グラデーションを位置 t でサンプリングする
	[[nodiscard]] static sgc::Colorf sampleGradient(
		const std::vector<GradientStop>& stops, float t)
	{
		if (stops.empty()) return {};
		if (stops.size() == 1) return stops[0].color;

		if (t <= stops.front().position) return stops.front().color;
		if (t >= stops.back().position) return stops.back().color;

		for (std::size_t i = 0; i + 1 < stops.size(); ++i)
		{
			if (t >= stops[i].position && t <= stops[i + 1].position)
			{
				const float range = stops[i + 1].position - stops[i].position;
				if (range < 1e-6f) return stops[i].color;

				const float localT = (t - stops[i].position) / range;
				return lerpColor(stops[i].color, stops[i + 1].color, localT);
			}
		}

		return stops.back().color;
	}

	/// @brief 2色間を線形補間する
	[[nodiscard]] static constexpr sgc::Colorf lerpColor(
		const sgc::Colorf& a, const sgc::Colorf& b, float t) noexcept
	{
		return {
			a.r + (b.r - a.r) * t,
			a.g + (b.g - a.g) * t,
			a.b + (b.b - a.b) * t,
			a.a + (b.a - a.a) * t
		};
	}
};

} // namespace mitiru::render

// ════════════════════════════════════════════════════════════
// Screen依存メソッドのインライン実装
// ════════════════════════════════════════════════════════════
#include <mitiru/core/Screen.hpp>

inline void mitiru::render::GradientRenderer::drawLinearGradient(
	Screen& screen,
	const sgc::Rectf& rect,
	const LinearGradient& gradient) const
{
	if (gradient.stops.size() < 2) return;
	if (rect.width() <= 0.0f || rect.height() <= 0.0f) return;

	const auto sortedStops = sortStops(gradient.stops);
	const float angleRad = gradient.angleDegrees * DEG_TO_RAD;

	const float dirX = std::sin(angleRad);
	const float dirY = std::cos(angleRad);

	/// 4隅の投影値を求めて最小・最大を得る
	const float proj00 = 0.0f;
	const float proj10 = rect.width() * dirX;
	const float proj01 = rect.height() * dirY;
	const float proj11 = rect.width() * dirX + rect.height() * dirY;

	const float projMin = std::min({proj00, proj10, proj01, proj11});
	const float projMax = std::max({proj00, proj10, proj01, proj11});
	const float projRange = projMax - projMin;

	if (projRange < 1e-6f) return;

	const int steps = std::max(1, static_cast<int>(rect.height()));
	const float stepH = rect.height() / static_cast<float>(steps);

	for (int row = 0; row < steps; ++row)
	{
		const float y = static_cast<float>(row) * stepH;

		const int hSteps = std::max(1, static_cast<int>(rect.width() / BAND_WIDTH));
		const float hStepW = rect.width() / static_cast<float>(hSteps);

		for (int col = 0; col < hSteps; ++col)
		{
			const float x = static_cast<float>(col) * hStepW;

			const float proj = x * dirX + y * dirY;
			const float t = std::clamp((proj - projMin) / projRange, 0.0f, 1.0f);

			const auto color = sampleGradient(sortedStops, t);
			screen.drawRect(
				sgc::Rectf{rect.x() + x, rect.y() + y, hStepW, stepH},
				color);
		}
	}
}

inline void mitiru::render::GradientRenderer::drawRadialGradient(
	Screen& screen,
	const sgc::Rectf& rect,
	const RadialGradient& gradient) const
{
	if (gradient.stops.size() < 2) return;
	if (rect.width() <= 0.0f || rect.height() <= 0.0f) return;

	const auto sortedStops = sortStops(gradient.stops);

	const float pixelCx = rect.x() + gradient.centerX * rect.width();
	const float pixelCy = rect.y() + gradient.centerY * rect.height();
	const float minDim = std::min(rect.width(), rect.height());
	const float pixelRadius = gradient.radius * minDim;

	if (pixelRadius < 1e-6f) return;

	const int rows = std::max(1, static_cast<int>(rect.height()));
	const float rowH = rect.height() / static_cast<float>(rows);

	for (int row = 0; row < rows; ++row)
	{
		const float y = rect.y() + static_cast<float>(row) * rowH + rowH * 0.5f;
		const float dy = y - pixelCy;

		const int cols = std::max(1, static_cast<int>(rect.width() / BAND_WIDTH));
		const float colW = rect.width() / static_cast<float>(cols);

		for (int col = 0; col < cols; ++col)
		{
			const float x = rect.x() + static_cast<float>(col) * colW + colW * 0.5f;
			const float dx = x - pixelCx;

			const float dist = std::sqrt(dx * dx + dy * dy);
			const float t = std::clamp(dist / pixelRadius, 0.0f, 1.0f);

			const auto color = sampleGradient(sortedStops, t);
			screen.drawRect(
				sgc::Rectf{
					rect.x() + static_cast<float>(col) * colW,
					rect.y() + static_cast<float>(row) * rowH,
					colW, rowH
				},
				color);
		}
	}
}

inline void mitiru::render::GradientRenderer::drawConicGradient(
	Screen& screen,
	const sgc::Rectf& rect,
	const ConicGradient& gradient) const
{
	if (gradient.stops.size() < 2) return;
	if (rect.width() <= 0.0f || rect.height() <= 0.0f) return;

	const auto sortedStops = sortStops(gradient.stops);

	const float pixelCx = rect.x() + gradient.centerX * rect.width();
	const float pixelCy = rect.y() + gradient.centerY * rect.height();
	const float startRad = gradient.startAngleDegrees * DEG_TO_RAD;

	const int rows = std::max(1, static_cast<int>(rect.height()));
	const float rowH = rect.height() / static_cast<float>(rows);

	for (int row = 0; row < rows; ++row)
	{
		const float y = rect.y() + static_cast<float>(row) * rowH + rowH * 0.5f;
		const float dy = y - pixelCy;

		const int cols = std::max(1, static_cast<int>(rect.width() / BAND_WIDTH));
		const float colW = rect.width() / static_cast<float>(cols);

		for (int col = 0; col < cols; ++col)
		{
			const float x = rect.x() + static_cast<float>(col) * colW + colW * 0.5f;
			const float dx = x - pixelCx;

			float angle = std::atan2(dx, -dy) - startRad;

			while (angle < 0.0f) angle += 2.0f * PI;
			while (angle >= 2.0f * PI) angle -= 2.0f * PI;

			const float t = angle / (2.0f * PI);

			const auto color = sampleGradient(sortedStops, t);
			screen.drawRect(
				sgc::Rectf{
					rect.x() + static_cast<float>(col) * colW,
					rect.y() + static_cast<float>(row) * rowH,
					colW, rowH
				},
				color);
		}
	}
}

inline void mitiru::render::GradientRenderer::drawLinearGradient(
	Screen& screen,
	const sgc::Rectf& rect,
	float angleDegrees,
	const std::vector<GradientStop>& stops) const
{
	LinearGradient g;
	g.angleDegrees = angleDegrees;
	g.stops = stops;
	drawLinearGradient(screen, rect, g);
}

inline void mitiru::render::GradientRenderer::drawRadialGradient(
	Screen& screen,
	const sgc::Rectf& rect,
	float centerX, float centerY, float radius,
	const std::vector<GradientStop>& stops) const
{
	RadialGradient g;
	g.centerX = centerX;
	g.centerY = centerY;
	g.radius = radius;
	g.stops = stops;
	drawRadialGradient(screen, rect, g);
}

inline void mitiru::render::GradientRenderer::drawConicGradient(
	Screen& screen,
	const sgc::Rectf& rect,
	float centerX, float centerY,
	float startAngleDegrees,
	const std::vector<GradientStop>& stops) const
{
	ConicGradient g;
	g.centerX = centerX;
	g.centerY = centerY;
	g.startAngleDegrees = startAngleDegrees;
	g.stops = stops;
	drawConicGradient(screen, rect, g);
}
