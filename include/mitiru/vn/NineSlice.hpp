#pragma once

/// @file NineSlice.hpp
/// @brief 伸縮可能なウィンドウスキン向けの 9-slice 画像描画。
/// @details ソーステクスチャを 9 領域（4 隅、4 辺、1 中央）に分割し、
///          SpriteBatch 描画に適した頂点/UV データを生成する。
///          隅は固定サイズのまま、辺と中央は対象矩形を埋めるよう伸縮する。

#include <cstdint>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/SpriteBatch.hpp>

namespace mitiru::vn
{

/// @brief 9-slice テクスチャの設定。
struct NineSliceConfig
{
	std::uint32_t textureId = 0;         ///< ソーステクスチャの識別子。

	float cornerW   = 16.0f;             ///< 隅の幅（ソースピクセル）。
	float cornerH   = 16.0f;             ///< 隅の高さ（ソースピクセル）。

	float edgeInsetLeft   = 16.0f;       ///< 左辺のインセット（ソースピクセル）。
	float edgeInsetRight  = 16.0f;       ///< 右辺のインセット（ソースピクセル）。
	float edgeInsetTop    = 16.0f;       ///< 上辺のインセット（ソースピクセル）。
	float edgeInsetBottom = 16.0f;       ///< 下辺のインセット（ソースピクセル）。

	float textureW  = 64.0f;            ///< ソーステクスチャの全体幅。
	float textureH  = 64.0f;            ///< ソーステクスチャの全体高さ。

	/// @brief 隅サイズが均一な設定を生成する。
	/// @param texId テクスチャ識別子。
	/// @param corner 隅サイズ（ソースピクセル）。
	/// @param texW ソーステクスチャ幅。
	/// @param texH ソーステクスチャ高さ。
	/// @return 構成済みの NineSliceConfig。
	[[nodiscard]] static NineSliceConfig uniform(
		std::uint32_t texId, float corner, float texW, float texH) noexcept
	{
		NineSliceConfig cfg;
		cfg.textureId       = texId;
		cfg.cornerW         = corner;
		cfg.cornerH         = corner;
		cfg.edgeInsetLeft   = corner;
		cfg.edgeInsetRight  = corner;
		cfg.edgeInsetTop    = corner;
		cfg.edgeInsetBottom = corner;
		cfg.textureW        = texW;
		cfg.textureH        = texH;
		return cfg;
	}
};

/// @brief 9-slice テクスチャを SpriteBatch に描画する。
/// @details 9 領域はそれぞれ個別の sprite 描画コールとして発行される。
///          隅はソースのアスペクト比を保ち、辺は片軸方向に、
///          中央は両軸方向に伸長する。
///
/// @code
/// mitiru::vn::NineSliceConfig cfg =
///     mitiru::vn::NineSliceConfig::uniform(texId, 16.0f, 64.0f, 64.0f);
/// mitiru::vn::NineSlice slice(cfg);
///
/// batch.begin();
/// slice.draw(batch, destRect, sgc::Colorf{1.0f, 1.0f, 1.0f, 0.9f});
/// batch.end();
/// @endcode
class NineSlice
{
	NineSliceConfig m_config;

public:
	/// @brief 指定した設定で構築する。
	/// @param config 9-slice パラメータ。
	explicit NineSlice(NineSliceConfig config) noexcept
		: m_config(config)
	{
	}

	/// @brief 現在の設定にアクセスする。
	[[nodiscard]] const NineSliceConfig& config() const noexcept { return m_config; }

	/// @brief 設定を差し替える。
	/// @param config 新しい 9-slice パラメータ。
	void setConfig(NineSliceConfig config) noexcept { m_config = config; }

	/// @brief 9-slice を SpriteBatch に描画する。
	/// @param batch 対象の SpriteBatch（begin/end の間であること）。
	/// @param dest スクリーン空間上の描画先矩形。
	/// @param tint 色ティント / アルファ変調。
	void draw(render::SpriteBatch& batch,
	          const sgc::Rectf& dest,
	          const sgc::Colorf& tint) const
	{
		const float il = m_config.edgeInsetLeft;
		const float ir = m_config.edgeInsetRight;
		const float it = m_config.edgeInsetTop;
		const float ib = m_config.edgeInsetBottom;
		const float tw = m_config.textureW;
		const float th = m_config.textureH;

		// 3 列・3 行分の UV 座標。
		const float u0 = 0.0f;
		const float u1 = il / tw;
		const float u2 = (tw - ir) / tw;
		const float u3 = 1.0f;

		const float v0 = 0.0f;
		const float v1 = it / th;
		const float v2 = (th - ib) / th;
		const float v3 = 1.0f;

		// 3 列・3 行分の描画先座標。
		const float dx0 = dest.x();
		const float dx1 = dest.x() + m_config.cornerW;
		const float dx2 = dest.x() + dest.width() - m_config.cornerW;
		const float dx3 = dest.x() + dest.width();

		const float dy0 = dest.y();
		const float dy1 = dest.y() + m_config.cornerH;
		const float dy2 = dest.y() + dest.height() - m_config.cornerH;
		const float dy3 = dest.y() + dest.height();

		const auto texId = m_config.textureId;

		// 行 0: 左上、上辺、右上
		batch.drawSprite(texId,
			sgc::Rectf{dx0, dy0, dx1 - dx0, dy1 - dy0},
			sgc::Rectf{u0, v0, u1 - u0, v1 - v0}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx1, dy0, dx2 - dx1, dy1 - dy0},
			sgc::Rectf{u1, v0, u2 - u1, v1 - v0}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx2, dy0, dx3 - dx2, dy1 - dy0},
			sgc::Rectf{u2, v0, u3 - u2, v1 - v0}, tint);

		// 行 1: 左辺、中央、右辺
		batch.drawSprite(texId,
			sgc::Rectf{dx0, dy1, dx1 - dx0, dy2 - dy1},
			sgc::Rectf{u0, v1, u1 - u0, v2 - v1}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx1, dy1, dx2 - dx1, dy2 - dy1},
			sgc::Rectf{u1, v1, u2 - u1, v2 - v1}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx2, dy1, dx3 - dx2, dy2 - dy1},
			sgc::Rectf{u2, v1, u3 - u2, v2 - v1}, tint);

		// 行 2: 左下、下辺、右下
		batch.drawSprite(texId,
			sgc::Rectf{dx0, dy2, dx1 - dx0, dy3 - dy2},
			sgc::Rectf{u0, v2, u1 - u0, v3 - v2}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx1, dy2, dx2 - dx1, dy3 - dy2},
			sgc::Rectf{u1, v2, u2 - u1, v3 - v2}, tint);

		batch.drawSprite(texId,
			sgc::Rectf{dx2, dy2, dx3 - dx2, dy3 - dy2},
			sgc::Rectf{u2, v2, u3 - u2, v3 - v2}, tint);
	}

	/// @brief この 9-slice を描画可能な最小サイズを計算する。
	/// @return 最小の幅と高さを Rectf として返す（x=0, y=0, w=min, h=min）。
	[[nodiscard]] sgc::Rectf minimumSize() const noexcept
	{
		return sgc::Rectf{
			0.0f, 0.0f,
			m_config.cornerW * 2.0f,
			m_config.cornerH * 2.0f
		};
	}
};

} // namespace mitiru::vn
