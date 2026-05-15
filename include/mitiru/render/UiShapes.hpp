#pragma once

/// @file UiShapes.hpp
/// @brief SDF (Signed Distance Field) ベースのUI形状レンダラー
/// @details 角丸矩形・ボーダー・ソフトシャドウ・グロー効果を
///          SDF数学で実現するソフトウェアレンダラー。
///          ScreenのdrawRect/drawCircle等に委譲してアンチエイリアス付きの
///          滑らかなUI形状を描画する。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Texture.hpp>

namespace mitiru
{
class Screen;
} // namespace mitiru

namespace mitiru::render
{

// ── パラメータ構造体 ───────────────────────────────────────

/// @brief 角丸矩形のレンダリングパラメータ
struct RoundedRectParams
{
	float x = 0.0f;              ///< 左上X座標
	float y = 0.0f;              ///< 左上Y座標
	float width = 100.0f;        ///< 幅
	float height = 50.0f;        ///< 高さ
	float cornerRadius = 8.0f;   ///< 角丸半径（ピクセル）

	float borderWidth = 0.0f;    ///< ボーダー幅（0で非表示）
	sgc::Colorf borderColor{0.5f, 0.5f, 0.5f, 1.0f};  ///< ボーダー色
	sgc::Colorf fillColor{0.2f, 0.2f, 0.2f, 1.0f};    ///< 塗りつぶし色

	sgc::Colorf shadowColor{0.0f, 0.0f, 0.0f, 0.4f};  ///< シャドウ色
	float shadowOffsetX = 0.0f;  ///< シャドウX方向オフセット
	float shadowOffsetY = 0.0f;  ///< シャドウY方向オフセット
	float shadowBlur = 0.0f;     ///< シャドウぼかし半径（0で無効）

	sgc::Colorf glowColor{0.0f, 0.0f, 0.0f, 0.0f};  ///< グロー色（アルファ0で無効）
	float glowRadius = 0.0f;    ///< グロー半径（0で無効）

	float opacity = 1.0f;        ///< 全体の不透明度 [0, 1]

	/// @brief パラメータのハッシュ値を計算する（キャッシュキー用）
	[[nodiscard]] std::size_t hash() const noexcept
	{
		auto hashFloat = [](float v) -> std::size_t {
			return std::hash<float>{}(v);
		};
		auto hashColor = [](const sgc::Colorf& c) -> std::size_t {
			std::size_t h = std::hash<float>{}(c.r);
			h ^= std::hash<float>{}(c.g) + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= std::hash<float>{}(c.b) + 0x9e3779b9 + (h << 6) + (h >> 2);
			h ^= std::hash<float>{}(c.a) + 0x9e3779b9 + (h << 6) + (h >> 2);
			return h;
		};

		std::size_t h = hashFloat(x);
		h ^= hashFloat(y) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(width) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(height) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(cornerRadius) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(borderWidth) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashColor(borderColor) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashColor(fillColor) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashColor(shadowColor) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(shadowOffsetX) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(shadowOffsetY) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(shadowBlur) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashColor(glowColor) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(glowRadius) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= hashFloat(opacity) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}

	[[nodiscard]] bool operator==(const RoundedRectParams& rhs) const noexcept = default;
};

// ── SDF数学ユーティリティ ──────────────────────────────────

namespace sdf
{

/// @brief smoothstep 補間（GLSLと同等）
/// @param edge0 下端
/// @param edge1 上端
/// @param x 入力値
/// @return [0, 1] の滑らかな補間値
[[nodiscard]] inline constexpr float smoothstep(float edge0, float edge1, float x) noexcept
{
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/// @brief 角丸矩形のSDF距離を計算する
/// @param px ピクセルX座標（矩形中心基準）
/// @param py ピクセルY座標（矩形中心基準）
/// @param halfW 矩形の半幅
/// @param halfH 矩形の半高さ
/// @param radius 角丸半径
/// @return 符号付き距離（負=内側, 正=外側）
[[nodiscard]] inline float roundedRect(float px, float py,
                                        float halfW, float halfH,
                                        float radius) noexcept
{
	const float r = std::min(radius, std::min(halfW, halfH));
	const float qx = std::abs(px) - (halfW - r);
	const float qy = std::abs(py) - (halfH - r);
	const float outsideDist = std::sqrt(
		std::max(qx, 0.0f) * std::max(qx, 0.0f) +
		std::max(qy, 0.0f) * std::max(qy, 0.0f));
	const float insideDist = std::min(std::max(qx, qy), 0.0f);
	return outsideDist + insideDist - r;
}

/// @brief 円のSDF距離を計算する
/// @param px ピクセルX座標（中心基準）
/// @param py ピクセルY座標（中心基準）
/// @param radius 半径
/// @return 符号付き距離
[[nodiscard]] inline float circle(float px, float py, float radius) noexcept
{
	return std::sqrt(px * px + py * py) - radius;
}

/// @brief 色のアルファブレンド（前景 over 背景）
/// @param dst 背景色
/// @param src 前景色
/// @return ブレンド結果
[[nodiscard]] inline sgc::Colorf alphaBlend(const sgc::Colorf& dst,
                                             const sgc::Colorf& src) noexcept
{
	const float sa = src.a;
	const float da = dst.a * (1.0f - sa);
	const float outA = sa + da;
	if (outA < 1e-6f)
	{
		return {0.0f, 0.0f, 0.0f, 0.0f};
	}
	return {
		(src.r * sa + dst.r * da) / outA,
		(src.g * sa + dst.g * da) / outA,
		(src.b * sa + dst.b * da) / outA,
		outA
	};
}

} // namespace sdf

// ── UiShapeRenderer ────────────────────────────────────────

/// @brief SDFベースUI形状レンダラー
/// @details 各ピクセルのSDF距離を計算し、smoothstepでアンチエイリアスを掛けた
///          滑らかなUI形状をScreen上に描画する。
///
///          描画フロー:
///          1. バウンディングボックス（シャドウ・グロー含む）を算出
///          2. 各スキャンラインでSDF距離を評価
///          3. smoothstepでアルファを決定
///          4. ScreenのソフトウェアフレームバッファまたはdrawRectに出力
///
/// @code
/// mitiru::render::UiShapeRenderer shapes;
/// mitiru::render::RoundedRectParams params;
/// params.x = 50; params.y = 50;
/// params.width = 200; params.height = 80;
/// params.cornerRadius = 12;
/// params.shadowBlur = 8; params.shadowOffsetY = 4;
/// shapes.drawRoundedRect(screen, params);
/// @endcode
class UiShapeRenderer
{
public:
	/// @brief アンチエイリアスのピクセル幅（滑らかさの調整）
	static constexpr float AA_WIDTH = 1.0f;

	/// @brief 角丸矩形を全効果付きで描画する
	void drawRoundedRect(Screen& screen, const RoundedRectParams& params) const;

	/// @brief SDF円を描画する（アンチエイリアス付き）
	void drawCircle(Screen& screen,
	                float cx, float cy, float radius,
	                const sgc::Colorf& fillColor,
	                float borderWidth = 0.0f,
	                const sgc::Colorf& borderColor = {0.5f, 0.5f, 0.5f, 1.0f}) const;

	/// @brief ピル（カプセル）形状を描画する
	void drawPill(Screen& screen,
	              float x, float y, float w, float h,
	              const sgc::Colorf& fillColor) const;

	/// @brief ソフトシャドウのみを描画する（形状本体なし）
	void drawRoundedRectShadow(Screen& screen,
	                            const sgc::Rectf& rect,
	                            float cornerRadius,
	                            const sgc::Colorf& shadowColor,
	                            float shadowBlur,
	                            float offsetX, float offsetY) const;

	/// @brief 円形プログレスアーク（円弧）を描画する
	void drawProgressArc(Screen& screen,
	                     float cx, float cy,
	                     float radius, float thickness,
	                     float progress,
	                     const sgc::Colorf& color,
	                     const sgc::Colorf& bgColor) const;

private:
	static constexpr float PI = 3.14159265358979323846f;

	/// @brief ピクセルをScreenに書き込む
	static void writePixel(Screen& screen, int x, int y, const sgc::Colorf& color);
};

// ── UiShapeCache ───────────────────────────────────────────

/// @brief SDF描画結果のテクスチャキャッシュ
/// @details 同一パラメータの再描画を回避するため、レンダリング結果を
///          Textureとして保持する。LRU方式で古いエントリを破棄する。
///
/// @code
/// mitiru::render::UiShapeCache cache(32);
/// const auto& tex = cache.getOrCreate(screen, params);
/// screen.drawSprite(tex, {params.x, params.y, params.width, params.height});
/// @endcode
class UiShapeCache
{
public:
	/// @brief コンストラクタ
	/// @param maxEntries キャッシュの最大エントリ数
	explicit UiShapeCache(std::size_t maxEntries = 64) noexcept
		: m_maxEntries(maxEntries)
	{
	}

	/// @brief キャッシュからテクスチャを取得する（なければ描画して作成）
	/// @param screen 描画に使用するスクリーン（サイズ参照用）
	/// @param params 角丸矩形パラメータ
	/// @return レンダリング済みテクスチャへの参照
	[[nodiscard]] const Texture& getOrCreate(Screen& screen, const RoundedRectParams& params);

	/// @brief キャッシュをクリアする
	void clear() noexcept
	{
		m_map.clear();
		m_lruList.clear();
	}

	/// @brief キャッシュされたエントリ数を取得する
	[[nodiscard]] std::size_t size() const noexcept
	{
		return m_map.size();
	}

	/// @brief 最大エントリ数を取得する
	[[nodiscard]] std::size_t maxEntries() const noexcept
	{
		return m_maxEntries;
	}

	/// @brief 最大エントリ数を変更する
	/// @param maxEntries 新しい最大値
	void setMaxEntries(std::size_t maxEntries) noexcept
	{
		m_maxEntries = maxEntries;
		while (m_map.size() > m_maxEntries)
		{
			evictOldest();
		}
	}

private:
	struct CacheEntry
	{
		std::size_t key;
		Texture texture;
	};

	std::size_t m_maxEntries;
	std::list<CacheEntry> m_lruList;
	std::unordered_map<std::size_t, std::list<CacheEntry>::iterator> m_map;

	void evictIfNeeded()
	{
		while (m_map.size() >= m_maxEntries)
		{
			evictOldest();
		}
	}

	void evictOldest()
	{
		if (m_lruList.empty()) return;
		const auto& last = m_lruList.back();
		m_map.erase(last.key);
		m_lruList.pop_back();
	}

	[[nodiscard]] static Texture renderToTexture(Screen& screen, const RoundedRectParams& params);
};

} // namespace mitiru::render

// ════════════════════════════════════════════════════════════
// Screen依存メソッドのインライン実装
// Screen.hppのインクルード完了後に定義する（循環依存回避）
// ════════════════════════════════════════════════════════════
#include <mitiru/core/Screen.hpp>

// ── UiShapeRenderer ────────────────────────────────────────

inline void mitiru::render::UiShapeRenderer::writePixel(
	Screen& screen, int x, int y, const sgc::Colorf& color)
{
	if (x < 0 || x >= screen.width() || y < 0 || y >= screen.height()) return;

	screen.drawRect(
		sgc::Rectf{static_cast<float>(x), static_cast<float>(y), 1.0f, 1.0f},
		color);
}

inline void mitiru::render::UiShapeRenderer::drawRoundedRect(
	Screen& screen, const RoundedRectParams& params) const
{
	if (params.opacity <= 0.0f) return;
	if (params.width <= 0.0f || params.height <= 0.0f) return;

	const float halfW = params.width * 0.5f;
	const float halfH = params.height * 0.5f;
	const float cx = params.x + halfW;
	const float cy = params.y + halfH;
	const float radius = std::min(params.cornerRadius, std::min(halfW, halfH));

	/// シャドウ・グロー含むバウンディングボックスを算出する
	const float expand = std::max(params.shadowBlur + std::max(std::abs(params.shadowOffsetX),
	                                                            std::abs(params.shadowOffsetY)),
	                               params.glowRadius);
	const int minX = std::max(0, static_cast<int>(std::floor(params.x - expand)));
	const int maxX = std::min(screen.width() - 1,
	                          static_cast<int>(std::ceil(params.x + params.width + expand)));
	const int minY = std::max(0, static_cast<int>(std::floor(params.y - expand)));
	const int maxY = std::min(screen.height() - 1,
	                          static_cast<int>(std::ceil(params.y + params.height + expand)));

	for (int py = minY; py <= maxY; ++py)
	{
		const float fy = static_cast<float>(py) + 0.5f;

		/// スキャンライン最適化: Y方向のSDF範囲を事前チェック
		const float yDist = std::abs(fy - cy) - halfH;
		if (yDist > expand + AA_WIDTH) continue;

		for (int px = minX; px <= maxX; ++px)
		{
			const float fx = static_cast<float>(px) + 0.5f;

			/// スキャンライン最適化: X方向の距離事前チェック
			const float xDist = std::abs(fx - cx) - halfW;
			if (xDist > expand + AA_WIDTH) continue;

			sgc::Colorf pixel{0.0f, 0.0f, 0.0f, 0.0f};

			/// シャドウレイヤー
			if (params.shadowBlur > 0.0f && params.shadowColor.a > 0.0f)
			{
				const float sdx = fx - cx - params.shadowOffsetX;
				const float sdy = fy - cy - params.shadowOffsetY;
				const float sDist = sdf::roundedRect(sdx, sdy, halfW, halfH, radius);
				const float sAlpha = 1.0f - sdf::smoothstep(-params.shadowBlur, 0.0f, sDist);
				if (sAlpha > 0.0f)
				{
					const sgc::Colorf shadow{
						params.shadowColor.r,
						params.shadowColor.g,
						params.shadowColor.b,
						params.shadowColor.a * sAlpha * params.opacity
					};
					pixel = sdf::alphaBlend(pixel, shadow);
				}
			}

			/// グローレイヤー
			if (params.glowRadius > 0.0f && params.glowColor.a > 0.0f)
			{
				const float gdx = fx - cx;
				const float gdy = fy - cy;
				const float gDist = sdf::roundedRect(gdx, gdy, halfW, halfH, radius);
				const float gAlpha = 1.0f - sdf::smoothstep(-params.glowRadius * 0.5f,
				                                              params.glowRadius, gDist);
				if (gAlpha > 0.0f)
				{
					const sgc::Colorf glow{
						params.glowColor.r,
						params.glowColor.g,
						params.glowColor.b,
						params.glowColor.a * gAlpha * params.opacity
					};
					pixel = sdf::alphaBlend(pixel, glow);
				}
			}

			/// 本体（塗り + ボーダー）レイヤー
			const float dx = fx - cx;
			const float dy = fy - cy;
			const float dist = sdf::roundedRect(dx, dy, halfW, halfH, radius);

			/// 外縁アンチエイリアス
			const float outerAlpha = 1.0f - sdf::smoothstep(-AA_WIDTH, 0.0f, dist);

			if (outerAlpha > 0.0f)
			{
				sgc::Colorf bodyColor;

				if (params.borderWidth > 0.0f)
				{
					/// ボーダー: 外縁 ~ (外縁-borderWidth) の帯
					const float innerDist = dist + params.borderWidth;
					const float borderMix = sdf::smoothstep(-AA_WIDTH, 0.0f, innerDist);
					bodyColor = {
						params.borderColor.r * (1.0f - borderMix) + params.fillColor.r * borderMix,
						params.borderColor.g * (1.0f - borderMix) + params.fillColor.g * borderMix,
						params.borderColor.b * (1.0f - borderMix) + params.fillColor.b * borderMix,
						params.borderColor.a * (1.0f - borderMix) + params.fillColor.a * borderMix
					};
				}
				else
				{
					bodyColor = params.fillColor;
				}

				bodyColor.a *= outerAlpha * params.opacity;
				pixel = sdf::alphaBlend(pixel, bodyColor);
			}

			/// 最終ピクセルの書き込み
			if (pixel.a > 1.0f / 255.0f)
			{
				writePixel(screen, px, py, pixel);
			}
		}
	}
}

inline void mitiru::render::UiShapeRenderer::drawCircle(
	Screen& screen,
	float cx, float cy, float radius,
	const sgc::Colorf& fillColor,
	float borderWidth,
	const sgc::Colorf& borderColor) const
{
	if (radius <= 0.0f) return;

	const int minX = std::max(0, static_cast<int>(std::floor(cx - radius - AA_WIDTH)));
	const int maxX = std::min(screen.width() - 1,
	                          static_cast<int>(std::ceil(cx + radius + AA_WIDTH)));
	const int minY = std::max(0, static_cast<int>(std::floor(cy - radius - AA_WIDTH)));
	const int maxY = std::min(screen.height() - 1,
	                          static_cast<int>(std::ceil(cy + radius + AA_WIDTH)));

	for (int py = minY; py <= maxY; ++py)
	{
		const float fy = static_cast<float>(py) + 0.5f;
		const float dy = fy - cy;

		if (std::abs(dy) > radius + AA_WIDTH) continue;

		/// X方向の可視範囲を計算する（スキャンライン最適化）
		const float xRange = std::sqrt(std::max(0.0f,
			(radius + AA_WIDTH) * (radius + AA_WIDTH) - dy * dy));
		const int scanMinX = std::max(minX, static_cast<int>(std::floor(cx - xRange)));
		const int scanMaxX = std::min(maxX, static_cast<int>(std::ceil(cx + xRange)));

		for (int px = scanMinX; px <= scanMaxX; ++px)
		{
			const float fx = static_cast<float>(px) + 0.5f;
			const float dx = fx - cx;

			const float dist = sdf::circle(dx, dy, radius);
			const float alpha = 1.0f - sdf::smoothstep(-AA_WIDTH, 0.0f, dist);

			if (alpha <= 0.0f) continue;

			sgc::Colorf color;
			if (borderWidth > 0.0f)
			{
				const float innerDist = dist + borderWidth;
				const float borderMix = sdf::smoothstep(-AA_WIDTH, 0.0f, innerDist);
				color = {
					borderColor.r * (1.0f - borderMix) + fillColor.r * borderMix,
					borderColor.g * (1.0f - borderMix) + fillColor.g * borderMix,
					borderColor.b * (1.0f - borderMix) + fillColor.b * borderMix,
					borderColor.a * (1.0f - borderMix) + fillColor.a * borderMix
				};
			}
			else
			{
				color = fillColor;
			}

			color.a *= alpha;
			writePixel(screen, px, py, color);
		}
	}
}

inline void mitiru::render::UiShapeRenderer::drawPill(
	Screen& screen,
	float x, float y, float w, float h,
	const sgc::Colorf& fillColor) const
{
	RoundedRectParams params;
	params.x = x;
	params.y = y;
	params.width = w;
	params.height = h;
	params.cornerRadius = std::min(w, h) * 0.5f;
	params.fillColor = fillColor;
	params.borderWidth = 0.0f;
	params.shadowBlur = 0.0f;
	params.glowRadius = 0.0f;
	drawRoundedRect(screen, params);
}

inline void mitiru::render::UiShapeRenderer::drawRoundedRectShadow(
	Screen& screen,
	const sgc::Rectf& rect,
	float cornerRadius,
	const sgc::Colorf& shadowColor,
	float shadowBlur,
	float offsetX, float offsetY) const
{
	if (shadowBlur <= 0.0f || shadowColor.a <= 0.0f) return;
	if (rect.width() <= 0.0f || rect.height() <= 0.0f) return;

	const float halfW = rect.width() * 0.5f;
	const float halfH = rect.height() * 0.5f;
	const float cx = rect.x() + halfW + offsetX;
	const float cy = rect.y() + halfH + offsetY;
	const float radius = std::min(cornerRadius, std::min(halfW, halfH));

	const float expand = shadowBlur;
	const int minX = std::max(0, static_cast<int>(std::floor(cx - halfW - expand)));
	const int maxX = std::min(screen.width() - 1,
	                          static_cast<int>(std::ceil(cx + halfW + expand)));
	const int minY = std::max(0, static_cast<int>(std::floor(cy - halfH - expand)));
	const int maxY = std::min(screen.height() - 1,
	                          static_cast<int>(std::ceil(cy + halfH + expand)));

	for (int py = minY; py <= maxY; ++py)
	{
		const float fy = static_cast<float>(py) + 0.5f;
		const float dy = fy - cy;

		if (std::abs(dy) > halfH + expand + AA_WIDTH) continue;

		for (int px = minX; px <= maxX; ++px)
		{
			const float fx = static_cast<float>(px) + 0.5f;
			const float dx = fx - cx;

			const float dist = sdf::roundedRect(dx, dy, halfW, halfH, radius);
			const float alpha = 1.0f - sdf::smoothstep(-shadowBlur, 0.0f, dist);

			if (alpha > 1.0f / 255.0f)
			{
				const sgc::Colorf pixel{
					shadowColor.r, shadowColor.g, shadowColor.b,
					shadowColor.a * alpha
				};
				writePixel(screen, px, py, pixel);
			}
		}
	}
}

inline void mitiru::render::UiShapeRenderer::drawProgressArc(
	Screen& screen,
	float cx, float cy,
	float radius, float thickness,
	float progress,
	const sgc::Colorf& color,
	const sgc::Colorf& bgColor) const
{
	if (radius <= 0.0f || thickness <= 0.0f) return;

	const float outerR = radius;
	const float innerR = radius - thickness;
	const float clampedProgress = std::clamp(progress, 0.0f, 1.0f);
	const float progressAngle = clampedProgress * 2.0f * PI;

	const int minX = std::max(0, static_cast<int>(std::floor(cx - outerR - AA_WIDTH)));
	const int maxX = std::min(screen.width() - 1,
	                          static_cast<int>(std::ceil(cx + outerR + AA_WIDTH)));
	const int minY = std::max(0, static_cast<int>(std::floor(cy - outerR - AA_WIDTH)));
	const int maxY = std::min(screen.height() - 1,
	                          static_cast<int>(std::ceil(cy + outerR + AA_WIDTH)));

	for (int py = minY; py <= maxY; ++py)
	{
		const float fy = static_cast<float>(py) + 0.5f;
		const float dy = fy - cy;

		if (std::abs(dy) > outerR + AA_WIDTH) continue;

		for (int px = minX; px <= maxX; ++px)
		{
			const float fx = static_cast<float>(px) + 0.5f;
			const float dx = fx - cx;

			const float dist = std::sqrt(dx * dx + dy * dy);

			/// リング形状のアルファ（外縁・内縁のAA）
			const float outerAlpha = 1.0f - sdf::smoothstep(-AA_WIDTH, 0.0f, dist - outerR);
			const float innerAlpha = sdf::smoothstep(-AA_WIDTH, 0.0f, dist - innerR);
			const float ringAlpha = outerAlpha * innerAlpha;

			if (ringAlpha <= 0.0f) continue;

			/// 角度を計算する（12時方向=0、時計回り）
			float angle = std::atan2(dx, -dy);
			if (angle < 0.0f)
			{
				angle += 2.0f * PI;
			}

			/// 進行度に基づいて前景/背景を決定する
			const float aaAngle = AA_WIDTH / std::max(radius, 1.0f);
			const float edgeAlpha = (std::abs(angle - progressAngle) < aaAngle)
				? 1.0f - sdf::smoothstep(0.0f, aaAngle, angle - progressAngle)
				: ((angle <= progressAngle) ? 1.0f : 0.0f);

			const sgc::Colorf arcColor{
				color.r * edgeAlpha + bgColor.r * (1.0f - edgeAlpha),
				color.g * edgeAlpha + bgColor.g * (1.0f - edgeAlpha),
				color.b * edgeAlpha + bgColor.b * (1.0f - edgeAlpha),
				(color.a * edgeAlpha + bgColor.a * (1.0f - edgeAlpha)) * ringAlpha
			};

			if (arcColor.a > 1.0f / 255.0f)
			{
				writePixel(screen, px, py, arcColor);
			}
		}
	}
}

// ── UiShapeCache ───────────────────────────────────────────

inline const mitiru::render::Texture& mitiru::render::UiShapeCache::getOrCreate(
	Screen& screen, const RoundedRectParams& params)
{
	const std::size_t key = params.hash();

	auto it = m_map.find(key);
	if (it != m_map.end())
	{
		m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
		return it->second->texture;
	}

	evictIfNeeded();

	const auto tex = renderToTexture(screen, params);
	m_lruList.push_front(CacheEntry{key, tex});
	m_map[key] = m_lruList.begin();

	return m_lruList.front().texture;
}

inline mitiru::render::Texture mitiru::render::UiShapeCache::renderToTexture(
	Screen& screen, const RoundedRectParams& params)
{
	const float expand = std::max(params.shadowBlur + std::max(std::abs(params.shadowOffsetX),
	                                                           std::abs(params.shadowOffsetY)),
	                              params.glowRadius);
	const int texW = static_cast<int>(std::ceil(params.width + expand * 2.0f));
	const int texH = static_cast<int>(std::ceil(params.height + expand * 2.0f));

	if (texW <= 0 || texH <= 0)
	{
		return {};
	}

	Screen offscreen(texW, texH);
	offscreen.enableSoftwareFramebuffer();
	offscreen.clear({0.0f, 0.0f, 0.0f, 0.0f});

	RoundedRectParams localParams = params;
	localParams.x = expand;
	localParams.y = expand;

	UiShapeRenderer renderer;
	renderer.drawRoundedRect(offscreen, localParams);
	offscreen.present();

	return Texture(texW, texH, offscreen.pixels());
}
