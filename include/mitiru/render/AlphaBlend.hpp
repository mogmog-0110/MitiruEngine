#pragma once

/// @file AlphaBlend.hpp
/// @brief アルファ合成ユーティリティ
/// @details Porter-Duff over演算に基づくアルファブレンドと、
///          複数のブレンドモード（加算・乗算・スクリーン・オーバーレイ）を提供する。
///          ソフトウェアレンダリング全体で使用する共通基盤。

#include <algorithm>
#include <cmath>

#include <sgc/types/Color.hpp>

namespace mitiru::render
{

/// @brief ブレンドモード
enum class BlendFunc
{
	Normal,    ///< 通常（Porter-Duff over）
	Additive,  ///< 加算合成
	Multiply,  ///< 乗算合成
	Screen,    ///< スクリーン合成
	Overlay,   ///< オーバーレイ合成
};

/// @brief アルファ合成ユーティリティ（全関数 constexpr / noexcept）
/// @details ソフトウェアレンダラーのピクセル合成に使用する。
///          全関数がイミュータブルで、入力カラーを変更しない。
///
/// @code
/// auto result = mitiru::render::AlphaBlend::over(srcColor, dstColor);
/// auto premul = mitiru::render::AlphaBlend::premultiply(srcColor);
/// auto blended = mitiru::render::AlphaBlend::blend(src, dst, BlendFunc::Additive);
/// @endcode
struct AlphaBlend
{
	/// @brief 値を [0, 1] にクランプする
	/// @param v 入力値
	/// @return クランプされた値
	[[nodiscard]] static constexpr float saturate(float v) noexcept
	{
		return std::max(0.0f, std::min(1.0f, v));
	}

	/// @brief Porter-Duff over演算によるアルファ合成
	/// @details result.rgb = src.rgb * src.a + dst.rgb * dst.a * (1 - src.a)
	///          result.a   = src.a + dst.a * (1 - src.a)
	///          最終的に result.rgb / result.a で straight alpha に戻す。
	/// @param src ソース色（straight alpha）
	/// @param dst デスティネーション色（straight alpha）
	/// @return 合成結果色
	[[nodiscard]] static constexpr sgc::Colorf over(
		const sgc::Colorf& src,
		const sgc::Colorf& dst) noexcept
	{
		const float sa = src.a;
		const float da = dst.a;
		const float oneMinusSa = 1.0f - sa;

		const float outA = sa + da * oneMinusSa;

		if (outA < 1e-6f)
		{
			return sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};
		}

		const float invA = 1.0f / outA;
		return sgc::Colorf{
			saturate((src.r * sa + dst.r * da * oneMinusSa) * invA),
			saturate((src.g * sa + dst.g * da * oneMinusSa) * invA),
			saturate((src.b * sa + dst.b * da * oneMinusSa) * invA),
			saturate(outA)
		};
	}

	/// @brief アルファ値を指定した色と背景色を合成する
	/// @details drawRect用の簡易版: src色にalphaを適用してdstに合成する。
	/// @param srcRgb ソース色（RGBのみ使用）
	/// @param alpha ソースのアルファ値 [0, 1]
	/// @param dst デスティネーション色
	/// @return 合成結果色
	[[nodiscard]] static constexpr sgc::Colorf overWithAlpha(
		const sgc::Colorf& srcRgb,
		float alpha,
		const sgc::Colorf& dst) noexcept
	{
		const sgc::Colorf src{srcRgb.r, srcRgb.g, srcRgb.b, alpha};
		return over(src, dst);
	}

	/// @brief straight alpha を premultiplied alpha に変換する
	/// @param color 入力色（straight alpha）
	/// @return premultiplied alpha 色
	[[nodiscard]] static constexpr sgc::Colorf premultiply(
		const sgc::Colorf& color) noexcept
	{
		return sgc::Colorf{
			color.r * color.a,
			color.g * color.a,
			color.b * color.a,
			color.a
		};
	}

	/// @brief premultiplied alpha を straight alpha に変換する
	/// @param color 入力色（premultiplied alpha）
	/// @return straight alpha 色
	[[nodiscard]] static constexpr sgc::Colorf unpremultiply(
		const sgc::Colorf& color) noexcept
	{
		if (color.a < 1e-6f)
		{
			return sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};
		}
		const float invA = 1.0f / color.a;
		return sgc::Colorf{
			saturate(color.r * invA),
			saturate(color.g * invA),
			saturate(color.b * invA),
			color.a
		};
	}

	/// @brief 指定ブレンドモードで2色を合成する
	/// @param src ソース色
	/// @param dst デスティネーション色
	/// @param func ブレンドモード
	/// @return 合成結果色
	[[nodiscard]] static constexpr sgc::Colorf blend(
		const sgc::Colorf& src,
		const sgc::Colorf& dst,
		BlendFunc func) noexcept
	{
		switch (func)
		{
		case BlendFunc::Normal:
			return over(src, dst);

		case BlendFunc::Additive:
			return blendAdditive(src, dst);

		case BlendFunc::Multiply:
			return blendMultiply(src, dst);

		case BlendFunc::Screen:
			return blendScreen(src, dst);

		case BlendFunc::Overlay:
			return blendOverlay(src, dst);
		}

		return over(src, dst);
	}

	/// @brief 2色を線形補間する
	/// @param a 色A
	/// @param b 色B
	/// @param t 補間係数 [0, 1]
	/// @return 補間結果
	[[nodiscard]] static constexpr sgc::Colorf lerp(
		const sgc::Colorf& a,
		const sgc::Colorf& b,
		float t) noexcept
	{
		const float s = 1.0f - t;
		return sgc::Colorf{
			a.r * s + b.r * t,
			a.g * s + b.g * t,
			a.b * s + b.b * t,
			a.a * s + b.a * t
		};
	}

private:
	/// @brief 加算合成
	[[nodiscard]] static constexpr sgc::Colorf blendAdditive(
		const sgc::Colorf& src,
		const sgc::Colorf& dst) noexcept
	{
		return sgc::Colorf{
			saturate(src.r * src.a + dst.r),
			saturate(src.g * src.a + dst.g),
			saturate(src.b * src.a + dst.b),
			saturate(src.a + dst.a)
		};
	}

	/// @brief 乗算合成
	[[nodiscard]] static constexpr sgc::Colorf blendMultiply(
		const sgc::Colorf& src,
		const sgc::Colorf& dst) noexcept
	{
		/// multiply: src * dst, アルファで通常合成と補間
		const float sa = src.a;
		const float oneMinusSa = 1.0f - sa;
		return sgc::Colorf{
			saturate(src.r * dst.r * sa + dst.r * oneMinusSa),
			saturate(src.g * dst.g * sa + dst.g * oneMinusSa),
			saturate(src.b * dst.b * sa + dst.b * oneMinusSa),
			saturate(sa + dst.a * oneMinusSa)
		};
	}

	/// @brief スクリーン合成
	[[nodiscard]] static constexpr sgc::Colorf blendScreen(
		const sgc::Colorf& src,
		const sgc::Colorf& dst) noexcept
	{
		/// screen: 1 - (1 - src) * (1 - dst), アルファで通常合成と補間
		const float sa = src.a;
		const float oneMinusSa = 1.0f - sa;
		const float sr = 1.0f - (1.0f - src.r) * (1.0f - dst.r);
		const float sg = 1.0f - (1.0f - src.g) * (1.0f - dst.g);
		const float sb = 1.0f - (1.0f - src.b) * (1.0f - dst.b);
		return sgc::Colorf{
			saturate(sr * sa + dst.r * oneMinusSa),
			saturate(sg * sa + dst.g * oneMinusSa),
			saturate(sb * sa + dst.b * oneMinusSa),
			saturate(sa + dst.a * oneMinusSa)
		};
	}

	/// @brief オーバーレイ合成
	[[nodiscard]] static constexpr sgc::Colorf blendOverlay(
		const sgc::Colorf& src,
		const sgc::Colorf& dst) noexcept
	{
		/// overlay: dst < 0.5 ? 2*src*dst : 1 - 2*(1-src)*(1-dst)
		const auto overlayChannel = [](float s, float d) -> float
		{
			return (d < 0.5f)
				? 2.0f * s * d
				: 1.0f - 2.0f * (1.0f - s) * (1.0f - d);
		};

		const float sa = src.a;
		const float oneMinusSa = 1.0f - sa;
		return sgc::Colorf{
			saturate(overlayChannel(src.r, dst.r) * sa + dst.r * oneMinusSa),
			saturate(overlayChannel(src.g, dst.g) * sa + dst.g * oneMinusSa),
			saturate(overlayChannel(src.b, dst.b) * sa + dst.b * oneMinusSa),
			saturate(sa + dst.a * oneMinusSa)
		};
	}
};

} // namespace mitiru::render
