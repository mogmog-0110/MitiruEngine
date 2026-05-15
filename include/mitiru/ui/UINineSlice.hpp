#pragma once

/// @file UINineSlice.hpp
/// @brief UIモジュール用の汎用9スライスレンダリング
/// @details vn::NineSliceの機能をUIモジュールに統合し、Screen APIを通じて
///          9スライステクスチャ描画を行う。SpriteBatchへの直接描画と
///          Screenラッパー経由の両方をサポートする。

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/SpriteBatch.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/ui/UIStyle.hpp>

namespace mitiru::ui
{

/// @brief 9スライスの描画領域情報
/// @details 9つのリージョンそれぞれの矩形とUV座標を保持する。
struct NineSliceRegions
{
	sgc::Rectf destRects[9]; ///< 描画先矩形（TL, T, TR, L, C, R, BL, B, BR）
	sgc::Rectf uvRects[9];  ///< UV矩形

	/// @brief インデックス定数
	static constexpr int TopLeft     = 0;
	static constexpr int Top         = 1;
	static constexpr int TopRight    = 2;
	static constexpr int Left        = 3;
	static constexpr int Center      = 4;
	static constexpr int Right       = 5;
	static constexpr int BottomLeft  = 6;
	static constexpr int Bottom      = 7;
	static constexpr int BottomRight = 8;
};

/// @brief 9スライスの領域を計算する
/// @param config 9スライス設定
/// @param dest 描画先矩形
/// @return 計算済みの9リージョン
[[nodiscard]] inline NineSliceRegions computeNineSliceRegions(
	const UINineSliceConfig& config, const sgc::Rectf& dest) noexcept
{
	NineSliceRegions regions;

	const float il = config.edgeInsetLeft;
	const float ir = config.edgeInsetRight;
	const float it = config.edgeInsetTop;
	const float ib = config.edgeInsetBottom;
	const float tw = config.textureW;
	const float th = config.textureH;

	// UV座標（3列3行の区切り）
	const float u0 = 0.0f;
	const float u1 = il / tw;
	const float u2 = (tw - ir) / tw;
	const float u3 = 1.0f;

	const float v0 = 0.0f;
	const float v1 = it / th;
	const float v2 = (th - ib) / th;
	const float v3 = 1.0f;

	// 描画先座標
	const float dx0 = dest.x();
	const float dx1 = dest.x() + config.cornerW;
	const float dx2 = dest.x() + dest.width() - config.cornerW;
	const float dx3 = dest.x() + dest.width();

	const float dy0 = dest.y();
	const float dy1 = dest.y() + config.cornerH;
	const float dy2 = dest.y() + dest.height() - config.cornerH;
	const float dy3 = dest.y() + dest.height();

	// Row 0: TL, T, TR
	regions.destRects[0] = {dx0, dy0, dx1 - dx0, dy1 - dy0};
	regions.uvRects[0]   = {u0,  v0,  u1  - u0,  v1  - v0};

	regions.destRects[1] = {dx1, dy0, dx2 - dx1, dy1 - dy0};
	regions.uvRects[1]   = {u1,  v0,  u2  - u1,  v1  - v0};

	regions.destRects[2] = {dx2, dy0, dx3 - dx2, dy1 - dy0};
	regions.uvRects[2]   = {u2,  v0,  u3  - u2,  v1  - v0};

	// Row 1: L, C, R
	regions.destRects[3] = {dx0, dy1, dx1 - dx0, dy2 - dy1};
	regions.uvRects[3]   = {u0,  v1,  u1  - u0,  v2  - v1};

	regions.destRects[4] = {dx1, dy1, dx2 - dx1, dy2 - dy1};
	regions.uvRects[4]   = {u1,  v1,  u2  - u1,  v2  - v1};

	regions.destRects[5] = {dx2, dy1, dx3 - dx2, dy2 - dy1};
	regions.uvRects[5]   = {u2,  v1,  u3  - u2,  v2  - v1};

	// Row 2: BL, B, BR
	regions.destRects[6] = {dx0, dy2, dx1 - dx0, dy3 - dy2};
	regions.uvRects[6]   = {u0,  v2,  u1  - u0,  v3  - v2};

	regions.destRects[7] = {dx1, dy2, dx2 - dx1, dy3 - dy2};
	regions.uvRects[7]   = {u1,  v2,  u2  - u1,  v3  - v2};

	regions.destRects[8] = {dx2, dy2, dx3 - dx2, dy3 - dy2};
	regions.uvRects[8]   = {u2,  v2,  u3  - u2,  v3  - v2};

	return regions;
}

/// @brief 9スライスをSpriteBatchに描画する
/// @param batch 描画先SpriteBatch（begin/end間であること）
/// @param config 9スライス設定
/// @param dest 描画先矩形
/// @param tint カラーティント
inline void drawNineSliceToBatch(
	render::SpriteBatch& batch,
	const UINineSliceConfig& config,
	const sgc::Rectf& dest,
	const sgc::Colorf& tint)
{
	const auto regions = computeNineSliceRegions(config, dest);
	const auto texId = config.textureId;

	for (int i = 0; i < 9; ++i)
	{
		batch.drawSprite(texId, regions.destRects[i], regions.uvRects[i], tint);
	}
}

/// @brief 9スライスをSpriteBatchに描画する（テクスチャ有効性チェック付き）
/// @details テクスチャが有効な場合はスプライト描画、無効な場合は
///          ソリッドカラー矩形でフォールバックする。
/// @param batch 描画先SpriteBatch
/// @param texture テクスチャ
/// @param config 9スライス設定
/// @param dest 描画先矩形
/// @param alpha アルファ値（0.0-1.0）
inline void drawNineSlice(
	render::SpriteBatch& batch,
	const render::Texture& texture,
	const UINineSliceConfig& config,
	const sgc::Rectf& dest,
	float alpha = 1.0f)
{
	if (texture.valid())
	{
		const sgc::Colorf tint{1.0f, 1.0f, 1.0f, alpha};
		drawNineSliceToBatch(batch, config, dest, tint);
	}
	else
	{
		// テクスチャが無効な場合はソリッドカラーのフォールバック
		const sgc::Colorf fallback{0.3f, 0.3f, 0.3f, alpha};
		const auto regions = computeNineSliceRegions(config, dest);
		for (int i = 0; i < 9; ++i)
		{
			batch.drawRect(regions.destRects[i], fallback);
		}
	}
}

/// @brief 最小描画サイズを計算する
/// @param config 9スライス設定
/// @return 最小幅・高さ
[[nodiscard]] inline sgc::Rectf nineSliceMinimumSize(const UINineSliceConfig& config) noexcept
{
	return sgc::Rectf{
		0.0f, 0.0f,
		config.cornerW * 2.0f,
		config.cornerH * 2.0f
	};
}

} // namespace mitiru::ui
