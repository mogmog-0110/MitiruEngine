#pragma once

/// @file AntiAlias.hpp
/// @brief アンチエイリアス付きソフトウェア描画プリミティブ
/// @details SDF（Signed Distance Field）ベースのアンチエイリアス描画と
///          Xiaolin Wuの線分アルゴリズムを用いた滑らかなプリミティブ描画を提供する。
///          全てCPU上で動作し、Screen::drawRect()を通じてピクセルを出力する。

#include <algorithm>
#include <cmath>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/AlphaBlend.hpp>

/// @brief 前方宣言（循環include回避）
namespace mitiru { class Screen; }

namespace mitiru::render
{

/// @brief アンチエイリアス付きソフトウェアレンダラー
/// @details 全描画メソッドは Screen::drawRect() で1ピクセル矩形を出力する。
///          SDF + smoothstep でサブピクセル精度のエッジ平滑化を行う。
///
/// @code
/// mitiru::render::AaRenderer aa;
/// aa.drawLineAA(screen, {10, 10}, {200, 150}, sgc::Colorf::white(), 2.0f);
/// aa.drawCircleAA(screen, {400, 300}, 50.0f, sgc::Colorf::red());
/// aa.drawRoundedRectAA(screen, rect, 8.0f, sgc::Colorf::blue());
/// @endcode
class AaRenderer
{
public:
	// ── 線分（Xiaolin Wu アルゴリズム） ─────────────────────

	/// @brief アンチエイリアス付き線分を描画する（Xiaolin Wuアルゴリズム）
	/// @param screen 描画先サーフェス
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	/// @param thickness 線の太さ（1.0 以上推奨）
	void drawLineAA(Screen& screen,
	                const sgc::Vec2f& from,
	                const sgc::Vec2f& to,
	                const sgc::Colorf& color,
	                float thickness = 1.0f) const
	{
		/// 太い線の場合は SDF ベースの矩形描画にフォールバック
		if (thickness > 2.0f)
		{
			drawThickLineAA(screen, from, to, color, thickness);
			return;
		}

		float x0 = from.x;
		float y0 = from.y;
		float x1 = to.x;
		float y1 = to.y;

		const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);

		if (steep)
		{
			std::swap(x0, y0);
			std::swap(x1, y1);
		}

		if (x0 > x1)
		{
			std::swap(x0, x1);
			std::swap(y0, y1);
		}

		const float dx = x1 - x0;
		const float dy = y1 - y0;
		const float gradient = (std::abs(dx) < 1e-6f) ? 1.0f : dy / dx;

		/// 始点端のピクセル
		{
			const float xEnd = roundPixel(x0);
			const float yEnd = y0 + gradient * (xEnd - x0);
			const float xGap = rfPart(x0 + 0.5f);

			const int xPx = static_cast<int>(xEnd);
			const int yPx = static_cast<int>(std::floor(yEnd));

			if (steep)
			{
				plotAA(screen, yPx, xPx, color, rfPart(yEnd) * xGap);
				plotAA(screen, yPx + 1, xPx, color, fPart(yEnd) * xGap);
			}
			else
			{
				plotAA(screen, xPx, yPx, color, rfPart(yEnd) * xGap);
				plotAA(screen, xPx, yPx + 1, color, fPart(yEnd) * xGap);
			}
		}

		/// 終点端のピクセル
		float xEnd1 = roundPixel(x1);
		float yEnd1 = y1 + gradient * (xEnd1 - x1);
		float xGap1 = fPart(x1 + 0.5f);

		const int xPx1 = static_cast<int>(xEnd1);
		const int yPx1 = static_cast<int>(std::floor(yEnd1));

		if (steep)
		{
			plotAA(screen, yPx1, xPx1, color, rfPart(yEnd1) * xGap1);
			plotAA(screen, yPx1 + 1, xPx1, color, fPart(yEnd1) * xGap1);
		}
		else
		{
			plotAA(screen, xPx1, yPx1, color, rfPart(yEnd1) * xGap1);
			plotAA(screen, xPx1, yPx1 + 1, color, fPart(yEnd1) * xGap1);
		}

		/// 中間ピクセルの描画
		float yInter = y0 + gradient * (roundPixel(x0) + 1.0f - x0);
		const int xStart = static_cast<int>(roundPixel(x0)) + 1;
		const int xEndI = xPx1;

		for (int x = xStart; x < xEndI; ++x)
		{
			const int yI = static_cast<int>(std::floor(yInter));

			if (steep)
			{
				plotAA(screen, yI, x, color, rfPart(yInter));
				plotAA(screen, yI + 1, x, color, fPart(yInter));
			}
			else
			{
				plotAA(screen, x, yI, color, rfPart(yInter));
				plotAA(screen, x, yI + 1, color, fPart(yInter));
			}

			yInter += gradient;
		}
	}

	// ── 円（SDF ベース） ────────────────────────────────────

	/// @brief アンチエイリアス付き円を描画する（SDF ベース）
	/// @param screen 描画先サーフェス
	/// @param center 中心座標
	/// @param radius 半径
	/// @param fillColor 塗りつぶし色
	/// @param borderWidth 枠線の太さ（0 で枠線なし）
	/// @param borderColor 枠線色
	void drawCircleAA(Screen& screen,
	                  const sgc::Vec2f& center,
	                  float radius,
	                  const sgc::Colorf& fillColor,
	                  float borderWidth = 0.0f,
	                  const sgc::Colorf& borderColor = sgc::Colorf{0.0f, 0.0f, 0.0f, 1.0f}) const
	{
		const float outerRadius = radius + borderWidth * 0.5f + AA_WIDTH;
		const int minX = static_cast<int>(std::floor(center.x - outerRadius));
		const int maxX = static_cast<int>(std::ceil(center.x + outerRadius));
		const int minY = static_cast<int>(std::floor(center.y - outerRadius));
		const int maxY = static_cast<int>(std::ceil(center.y + outerRadius));

		for (int py = minY; py <= maxY; ++py)
		{
			for (int px = minX; px <= maxX; ++px)
			{
				const float dx = static_cast<float>(px) + 0.5f - center.x;
				const float dy = static_cast<float>(py) + 0.5f - center.y;
				const float dist = std::sqrt(dx * dx + dy * dy) - radius;

				if (borderWidth > 0.0f)
				{
					/// 枠線あり: fillとborderを個別に計算する
					const float fillAlpha = smoothstep(AA_WIDTH, -AA_WIDTH, dist + borderWidth * 0.5f);
					const float borderAlpha = smoothstep(AA_WIDTH, -AA_WIDTH,
						std::abs(dist) - borderWidth * 0.5f);

					/// fill部分（円の内側）
					const float innerDist = dist + borderWidth * 0.5f;
					const float innerAlpha = smoothstep(AA_WIDTH, -AA_WIDTH, innerDist);

					/// border部分（枠線リング）
					const float ringAlpha = borderAlpha * (1.0f - innerAlpha);

					const float totalAlpha = innerAlpha * fillColor.a + ringAlpha * borderColor.a;
					if (totalAlpha > ALPHA_THRESHOLD)
					{
						/// fill と border を合成する
						const sgc::Colorf blended = composeFillBorder(
							fillColor, innerAlpha,
							borderColor, ringAlpha);
						plotAA(screen, px, py, blended, blended.a);
					}
				}
				else
				{
					/// 枠線なし: 単純な fill のみ
					const float alpha = smoothstep(AA_WIDTH, -AA_WIDTH, dist);
					if (alpha > ALPHA_THRESHOLD)
					{
						plotAA(screen, px, py, fillColor, alpha * fillColor.a);
					}
				}
			}
		}
	}

	// ── 角丸矩形（SDF ベース） ─────────────────────────────

	/// @brief アンチエイリアス付き角丸矩形を描画する（塗りつぶしのみ）
	/// @param screen 描画先サーフェス
	/// @param rect 矩形領域
	/// @param cornerRadius 角の丸み半径
	/// @param fillColor 塗りつぶし色
	void drawRoundedRectAA(Screen& screen,
	                       const sgc::Rectf& rect,
	                       float cornerRadius,
	                       const sgc::Colorf& fillColor) const
	{
		drawRoundedRectAA(screen, rect, cornerRadius, fillColor, 0.0f,
		                  sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f});
	}

	/// @brief アンチエイリアス付き角丸矩形を描画する（塗りつぶし + 枠線）
	/// @param screen 描画先サーフェス
	/// @param rect 矩形領域
	/// @param cornerRadius 角の丸み半径
	/// @param fillColor 塗りつぶし色
	/// @param borderWidth 枠線の太さ（0 で枠線なし）
	/// @param borderColor 枠線色
	void drawRoundedRectAA(Screen& screen,
	                       const sgc::Rectf& rect,
	                       float cornerRadius,
	                       const sgc::Colorf& fillColor,
	                       float borderWidth,
	                       const sgc::Colorf& borderColor) const
	{
		/// 角半径を矩形の半分以下にクランプする
		const float maxRadius = std::min(rect.width(), rect.height()) * 0.5f;
		const float cr = std::min(cornerRadius, maxRadius);

		const sgc::Vec2f rectCenter{
			rect.x() + rect.width() * 0.5f,
			rect.y() + rect.height() * 0.5f
		};
		const sgc::Vec2f halfSize{
			rect.width() * 0.5f,
			rect.height() * 0.5f
		};

		const float expand = borderWidth * 0.5f + AA_WIDTH;
		const int minX = static_cast<int>(std::floor(rect.x() - expand));
		const int maxX = static_cast<int>(std::ceil(rect.x() + rect.width() + expand));
		const int minY = static_cast<int>(std::floor(rect.y() - expand));
		const int maxY = static_cast<int>(std::ceil(rect.y() + rect.height() + expand));

		for (int py = minY; py <= maxY; ++py)
		{
			for (int px = minX; px <= maxX; ++px)
			{
				const sgc::Vec2f pixelPos{
					static_cast<float>(px) + 0.5f,
					static_cast<float>(py) + 0.5f
				};
				const float dist = sdfRoundedRect(pixelPos, rectCenter, halfSize, cr);

				if (borderWidth > 0.0f)
				{
					const float fillAlpha = smoothstep(AA_WIDTH, -AA_WIDTH, dist + borderWidth * 0.5f);
					const float borderAlpha = smoothstep(AA_WIDTH, -AA_WIDTH,
						std::abs(dist) - borderWidth * 0.5f);

					const float innerAlpha = smoothstep(AA_WIDTH, -AA_WIDTH, dist + borderWidth * 0.5f);
					const float ringAlpha = borderAlpha * (1.0f - innerAlpha);

					const float totalAlpha = innerAlpha * fillColor.a + ringAlpha * borderColor.a;
					if (totalAlpha > ALPHA_THRESHOLD)
					{
						const sgc::Colorf blended = composeFillBorder(
							fillColor, innerAlpha,
							borderColor, ringAlpha);
						plotAA(screen, px, py, blended, blended.a);
					}
				}
				else
				{
					const float alpha = smoothstep(AA_WIDTH, -AA_WIDTH, dist);
					if (alpha > ALPHA_THRESHOLD)
					{
						plotAA(screen, px, py, fillColor, alpha * fillColor.a);
					}
				}
			}
		}
	}

	// ── 三角形（カバレッジベース AA） ───────────────────────

	/// @brief アンチエイリアス付き三角形を描画する（エッジカバレッジ AA）
	/// @param screen 描画先サーフェス
	/// @param p0 頂点 0
	/// @param p1 頂点 1
	/// @param p2 頂点 2
	/// @param color 描画色
	void drawTriangleAA(Screen& screen,
	                    const sgc::Vec2f& p0,
	                    const sgc::Vec2f& p1,
	                    const sgc::Vec2f& p2,
	                    const sgc::Colorf& color) const
	{
		const float minXf = std::min({p0.x, p1.x, p2.x}) - AA_WIDTH;
		const float maxXf = std::max({p0.x, p1.x, p2.x}) + AA_WIDTH;
		const float minYf = std::min({p0.y, p1.y, p2.y}) - AA_WIDTH;
		const float maxYf = std::max({p0.y, p1.y, p2.y}) + AA_WIDTH;

		const int minX = static_cast<int>(std::floor(minXf));
		const int maxX = static_cast<int>(std::ceil(maxXf));
		const int minY = static_cast<int>(std::floor(minYf));
		const int maxY = static_cast<int>(std::ceil(maxYf));

		for (int py = minY; py <= maxY; ++py)
		{
			for (int px = minX; px <= maxX; ++px)
			{
				const sgc::Vec2f pixel{
					static_cast<float>(px) + 0.5f,
					static_cast<float>(py) + 0.5f
				};
				const float dist = sdfTriangle(pixel, p0, p1, p2);
				const float alpha = smoothstep(AA_WIDTH, -AA_WIDTH, dist);

				if (alpha > ALPHA_THRESHOLD)
				{
					plotAA(screen, px, py, color, alpha * color.a);
				}
			}
		}
	}

private:
	/// @brief アンチエイリアスの幅（ピクセル単位）
	static constexpr float AA_WIDTH = 1.0f;

	/// @brief 描画を省略するアルファ閾値
	static constexpr float ALPHA_THRESHOLD = 1.0f / 255.0f;

	// ── ヘルパー: ピクセル出力 ──────────────────────────────

	/// @brief 1ピクセルをアルファ付きで描画する
	/// @param screen 描画先
	/// @param x X座標
	/// @param y Y座標
	/// @param color 描画色
	/// @param alpha アルファ値 [0, 1]
	static void plotAA(Screen& screen, int x, int y,
	                   const sgc::Colorf& color, float alpha);

	// ── ヘルパー: Wu アルゴリズム用 ─────────────────────────

	/// @brief 小数部を返す
	[[nodiscard]] static float fPart(float x) noexcept
	{
		return x - std::floor(x);
	}

	/// @brief 1 - 小数部 を返す
	[[nodiscard]] static float rfPart(float x) noexcept
	{
		return 1.0f - fPart(x);
	}

	/// @brief 最近傍のピクセル座標に丸める
	[[nodiscard]] static float roundPixel(float x) noexcept
	{
		return std::floor(x + 0.5f);
	}

	// ── ヘルパー: SDF 関数 ──────────────────────────────────

	/// @brief smoothstep 補間関数
	/// @param edge0 下端
	/// @param edge1 上端
	/// @param x 入力値
	/// @return [0, 1] の滑らかな補間値
	[[nodiscard]] static constexpr float smoothstep(float edge0, float edge1, float x) noexcept
	{
		const float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
		return t * t * (3.0f - 2.0f * t);
	}

	/// @brief 角丸矩形の SDF（Signed Distance Field）
	/// @param p ピクセル座標
	/// @param center 矩形中心
	/// @param halfSize 矩形の半サイズ
	/// @param radius 角の丸み半径
	/// @return 距離（負: 内側、正: 外側）
	[[nodiscard]] static float sdfRoundedRect(
		const sgc::Vec2f& p,
		const sgc::Vec2f& center,
		const sgc::Vec2f& halfSize,
		float radius) noexcept
	{
		/// 中心からの相対座標（絶対値で第一象限に折り畳む）
		const float qx = std::abs(p.x - center.x) - (halfSize.x - radius);
		const float qy = std::abs(p.y - center.y) - (halfSize.y - radius);

		/// 外側: ユークリッド距離 - radius
		/// 内側: max(qx, qy) の負の距離
		const float outsideDist = std::sqrt(
			std::max(qx, 0.0f) * std::max(qx, 0.0f) +
			std::max(qy, 0.0f) * std::max(qy, 0.0f));
		const float insideDist = std::min(std::max(qx, qy), 0.0f);

		return outsideDist + insideDist - radius;
	}

	/// @brief 三角形の SDF
	/// @param p ピクセル座標
	/// @param a 頂点 A
	/// @param b 頂点 B
	/// @param c 頂点 C
	/// @return 距離（負: 内側、正: 外側）
	[[nodiscard]] static float sdfTriangle(
		const sgc::Vec2f& p,
		const sgc::Vec2f& a,
		const sgc::Vec2f& b,
		const sgc::Vec2f& c) noexcept
	{
		/// 各辺への符号付き距離を計算し、最大値を返す
		const float d0 = edgeDistance(p, a, b);
		const float d1 = edgeDistance(p, b, c);
		const float d2 = edgeDistance(p, c, a);

		/// 三角形の巻き方向を判定する
		const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);

		if (cross > 0.0f)
		{
			/// 反時計回り: 全ての距離が負なら内側
			return std::max({d0, d1, d2});
		}
		else
		{
			/// 時計回り: 距離の符号を反転する
			return std::max({-d0, -d1, -d2});
		}
	}

	/// @brief 辺への符号付き距離を計算する
	/// @param p 点
	/// @param a 辺の始点
	/// @param b 辺の終点
	/// @return 符号付き距離（左側が負）
	[[nodiscard]] static float edgeDistance(
		const sgc::Vec2f& p,
		const sgc::Vec2f& a,
		const sgc::Vec2f& b) noexcept
	{
		const float ex = b.x - a.x;
		const float ey = b.y - a.y;
		const float len = std::sqrt(ex * ex + ey * ey);

		if (len < 1e-6f)
		{
			return std::sqrt((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y));
		}

		/// 法線方向（左側が正）
		return ((p.x - a.x) * ey - (p.y - a.y) * ex) / len;
	}

	// ── ヘルパー: 太い線分 ──────────────────────────────────

	/// @brief SDF ベースの太い線分描画（エンドキャップ付き）
	/// @param screen 描画先
	/// @param from 始点
	/// @param to 終点
	/// @param color 描画色
	/// @param thickness 線の太さ
	void drawThickLineAA(Screen& screen,
	                     const sgc::Vec2f& from,
	                     const sgc::Vec2f& to,
	                     const sgc::Colorf& color,
	                     float thickness) const
	{
		const float halfThick = thickness * 0.5f;
		const float dx = to.x - from.x;
		const float dy = to.y - from.y;
		const float len = std::sqrt(dx * dx + dy * dy);

		const float expand = halfThick + AA_WIDTH;
		const float minXf = std::min(from.x, to.x) - expand;
		const float maxXf = std::max(from.x, to.x) + expand;
		const float minYf = std::min(from.y, to.y) - expand;
		const float maxYf = std::max(from.y, to.y) + expand;

		const int minX = static_cast<int>(std::floor(minXf));
		const int maxX = static_cast<int>(std::ceil(maxXf));
		const int minY = static_cast<int>(std::floor(minYf));
		const int maxY = static_cast<int>(std::ceil(maxYf));

		for (int py = minY; py <= maxY; ++py)
		{
			for (int px = minX; px <= maxX; ++px)
			{
				const float ppx = static_cast<float>(px) + 0.5f;
				const float ppy = static_cast<float>(py) + 0.5f;

				const float dist = sdfLineSegment(ppx, ppy, from, to, len) - halfThick;
				const float alpha = smoothstep(AA_WIDTH, -AA_WIDTH, dist);

				if (alpha > ALPHA_THRESHOLD)
				{
					plotAA(screen, px, py, color, alpha * color.a);
				}
			}
		}
	}

	/// @brief 線分の SDF（カプセル形状）
	/// @param px ピクセル X
	/// @param py ピクセル Y
	/// @param a 線分始点
	/// @param b 線分終点
	/// @param len 線分の長さ（事前計算済み）
	/// @return 線分への最短距離
	[[nodiscard]] static float sdfLineSegment(
		float px, float py,
		const sgc::Vec2f& a,
		const sgc::Vec2f& b,
		float len) noexcept
	{
		if (len < 1e-6f)
		{
			const float dx = px - a.x;
			const float dy = py - a.y;
			return std::sqrt(dx * dx + dy * dy);
		}

		/// ピクセルから始点へのベクトルを線分方向に射影する
		const float dx = b.x - a.x;
		const float dy = b.y - a.y;
		float t = ((px - a.x) * dx + (py - a.y) * dy) / (len * len);
		t = std::max(0.0f, std::min(1.0f, t));

		/// 最近傍点への距離
		const float closestX = a.x + t * dx;
		const float closestY = a.y + t * dy;
		const float ex = px - closestX;
		const float ey = py - closestY;
		return std::sqrt(ex * ex + ey * ey);
	}

	// ── ヘルパー: fill + border 合成 ────────────────────────

	/// @brief fill と border を合成する
	/// @param fillColor 塗りつぶし色
	/// @param fillAlpha 塗りつぶしアルファ
	/// @param borderColor 枠線色
	/// @param borderAlpha 枠線アルファ
	/// @return 合成結果色
	[[nodiscard]] static constexpr sgc::Colorf composeFillBorder(
		const sgc::Colorf& fillColor, float fillAlpha,
		const sgc::Colorf& borderColor, float borderAlpha) noexcept
	{
		const float fa = fillAlpha * fillColor.a;
		const float ba = borderAlpha * borderColor.a;
		const float totalAlpha = fa + ba * (1.0f - fa);

		if (totalAlpha < 1e-6f)
		{
			return sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};
		}

		const float invTotal = 1.0f / totalAlpha;
		return sgc::Colorf{
			AlphaBlend::saturate((fillColor.r * fa + borderColor.r * ba * (1.0f - fa)) * invTotal),
			AlphaBlend::saturate((fillColor.g * fa + borderColor.g * ba * (1.0f - fa)) * invTotal),
			AlphaBlend::saturate((fillColor.b * fa + borderColor.b * ba * (1.0f - fa)) * invTotal),
			AlphaBlend::saturate(totalAlpha)
		};
	}
};

} // namespace mitiru::render

// ── plotAA 実装（Screen.hpp のインクルード後に定義） ────────────

#include <mitiru/core/Screen.hpp>

inline void mitiru::render::AaRenderer::plotAA(
	Screen& screen, int x, int y,
	const sgc::Colorf& color, float alpha)
{
	if (x < 0 || x >= screen.width() || y < 0 || y >= screen.height())
	{
		return;
	}

	if (alpha < ALPHA_THRESHOLD)
	{
		return;
	}

	const float clampedAlpha = std::min(alpha, 1.0f);
	const sgc::Colorf pixelColor{color.r, color.g, color.b, clampedAlpha};

	screen.drawRect(
		sgc::Rectf{
			static_cast<float>(x),
			static_cast<float>(y),
			1.0f, 1.0f},
		pixelColor);
}
