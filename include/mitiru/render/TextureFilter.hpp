#pragma once

/// @file TextureFilter.hpp
/// @brief テクスチャフィルタリングとフィルタ付きスプライト描画
/// @details ソフトウェアレンダラー用のバイリニア・バイキュービック補間サンプリングと、
///          適切なアルファブレンド付きのスプライト描画を提供する。
///          Screen の既存 drawSprite() よりも高品質な描画結果を得られる。

#include <algorithm>
#include <cmath>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/AlphaBlend.hpp>
#include <mitiru/render/Texture.hpp>

/// @brief 前方宣言（循環include回避）
namespace mitiru { class Screen; }

namespace mitiru::render
{

/// @brief テクスチャフィルタリングモード
enum class FilterMode
{
	Nearest,   ///< 最近傍補間（ピクセルパーフェクト、シャープ）
	Bilinear,  ///< バイリニア補間（4テクセルの加重平均）
	Bicubic,   ///< バイキュービック補間（16テクセルの加重平均、最も滑らか）
};

/// @brief テクスチャフィルタリングユーティリティ
/// @details テクスチャから浮動小数点UV座標でサンプリングし、
///          指定フィルタモードに基づいて補間した色を返す。
///
/// @code
/// auto color = mitiru::render::TextureFilter::sampleBilinear(texture, 0.5f, 0.5f);
/// mitiru::render::TextureFilter::drawSpriteFiltered(
///     screen, texture, destRect, FilterMode::Bilinear, 1.0f);
/// @endcode
struct TextureFilter
{
	// ── テクセルサンプリング ─────────────────────────────────

	/// @brief テクスチャから最近傍補間でサンプリングする
	/// @param texture テクスチャ
	/// @param u 水平テクスチャ座標 [0, 1]
	/// @param v 垂直テクスチャ座標 [0, 1]
	/// @return サンプリング結果の色
	[[nodiscard]] static sgc::Colorf sampleNearest(
		const Texture& texture, float u, float v) noexcept
	{
		if (!texture.valid()) return sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};

		const int tx = clampTexelX(static_cast<int>(u * static_cast<float>(texture.width())), texture);
		const int ty = clampTexelY(static_cast<int>(v * static_cast<float>(texture.height())), texture);

		return texelAt(texture, tx, ty);
	}

	/// @brief テクスチャからバイリニア補間でサンプリングする
	/// @details 4つの隣接テクセルの加重平均で滑らかな色を返す。
	/// @param texture テクスチャ
	/// @param u 水平テクスチャ座標 [0, 1]
	/// @param v 垂直テクスチャ座標 [0, 1]
	/// @return サンプリング結果の色
	[[nodiscard]] static sgc::Colorf sampleBilinear(
		const Texture& texture, float u, float v) noexcept
	{
		if (!texture.valid()) return sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};

		/// テクセル空間の連続座標（ピクセル中心を 0.5 とする）
		const float tx = u * static_cast<float>(texture.width()) - 0.5f;
		const float ty = v * static_cast<float>(texture.height()) - 0.5f;

		const int x0 = static_cast<int>(std::floor(tx));
		const int y0 = static_cast<int>(std::floor(ty));
		const int x1 = x0 + 1;
		const int y1 = y0 + 1;

		const float fx = tx - static_cast<float>(x0);
		const float fy = ty - static_cast<float>(y0);

		/// 4テクセルを取得する（範囲外はクランプ）
		const auto c00 = texelAt(texture, clampTexelX(x0, texture), clampTexelY(y0, texture));
		const auto c10 = texelAt(texture, clampTexelX(x1, texture), clampTexelY(y0, texture));
		const auto c01 = texelAt(texture, clampTexelX(x0, texture), clampTexelY(y1, texture));
		const auto c11 = texelAt(texture, clampTexelX(x1, texture), clampTexelY(y1, texture));

		/// 水平補間
		const auto top = AlphaBlend::lerp(c00, c10, fx);
		const auto bottom = AlphaBlend::lerp(c01, c11, fx);

		/// 垂直補間
		return AlphaBlend::lerp(top, bottom, fy);
	}

	/// @brief テクスチャからバイキュービック補間でサンプリングする
	/// @details Catmull-Romスプラインに基づく16テクセルの加重平均。
	///          バイリニアよりも更に滑らかな結果を返す。
	/// @param texture テクスチャ
	/// @param u 水平テクスチャ座標 [0, 1]
	/// @param v 垂直テクスチャ座標 [0, 1]
	/// @return サンプリング結果の色
	[[nodiscard]] static sgc::Colorf sampleBicubic(
		const Texture& texture, float u, float v) noexcept
	{
		if (!texture.valid()) return sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};

		const float tx = u * static_cast<float>(texture.width()) - 0.5f;
		const float ty = v * static_cast<float>(texture.height()) - 0.5f;

		const int ix = static_cast<int>(std::floor(tx));
		const int iy = static_cast<int>(std::floor(ty));
		const float fx = tx - static_cast<float>(ix);
		const float fy = ty - static_cast<float>(iy);

		/// 水平方向のCatmull-Rom重みを計算する
		const float wx[4] = {
			catmullRom(fx + 1.0f),
			catmullRom(fx),
			catmullRom(1.0f - fx),
			catmullRom(2.0f - fx)
		};

		/// 垂直方向のCatmull-Rom重みを計算する
		const float wy[4] = {
			catmullRom(fy + 1.0f),
			catmullRom(fy),
			catmullRom(1.0f - fy),
			catmullRom(2.0f - fy)
		};

		float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
		float totalWeight = 0.0f;

		for (int j = -1; j <= 2; ++j)
		{
			for (int i = -1; i <= 2; ++i)
			{
				const float w = wx[i + 1] * wy[j + 1];
				const auto c = texelAt(texture,
					clampTexelX(ix + i, texture),
					clampTexelY(iy + j, texture));

				r += c.r * w;
				g += c.g * w;
				b += c.b * w;
				a += c.a * w;
				totalWeight += w;
			}
		}

		if (std::abs(totalWeight) < 1e-6f)
		{
			return sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};
		}

		const float invW = 1.0f / totalWeight;
		return sgc::Colorf{
			AlphaBlend::saturate(r * invW),
			AlphaBlend::saturate(g * invW),
			AlphaBlend::saturate(b * invW),
			AlphaBlend::saturate(a * invW)
		};
	}

	/// @brief 指定フィルタモードでテクスチャをサンプリングする
	/// @param texture テクスチャ
	/// @param u 水平テクスチャ座標 [0, 1]
	/// @param v 垂直テクスチャ座標 [0, 1]
	/// @param mode フィルタモード
	/// @return サンプリング結果の色
	[[nodiscard]] static sgc::Colorf sample(
		const Texture& texture, float u, float v, FilterMode mode) noexcept
	{
		switch (mode)
		{
		case FilterMode::Nearest:
			return sampleNearest(texture, u, v);
		case FilterMode::Bilinear:
			return sampleBilinear(texture, u, v);
		case FilterMode::Bicubic:
			return sampleBicubic(texture, u, v);
		}
		return sampleNearest(texture, u, v);
	}

	// ── フィルタ付きスプライト描画 ──────────────────────────

	/// @brief テクスチャをフィルタ付きで描画先矩形に描画する
	/// @details 描画先の各ピクセルに対してテクスチャ座標を計算し、
	///          指定フィルタモードでサンプリングしてアルファブレンドで出力する。
	/// @param screen 描画先サーフェス
	/// @param texture テクスチャ
	/// @param destRect 描画先矩形
	/// @param mode フィルタモード
	/// @param alpha 全体アルファ乗算値 [0, 1]
	static void drawSpriteFiltered(Screen& screen,
	                               const Texture& texture,
	                               const sgc::Rectf& destRect,
	                               FilterMode mode = FilterMode::Bilinear,
	                               float alpha = 1.0f);

private:
	/// @brief テクスチャの指定テクセルの色を取得する
	/// @param texture テクスチャ
	/// @param x テクセルX座標（クランプ済み前提）
	/// @param y テクセルY座標（クランプ済み前提）
	/// @return テクセル色
	[[nodiscard]] static sgc::Colorf texelAt(
		const Texture& texture, int x, int y) noexcept
	{
		const auto& px = texture.pixels();
		const auto i = static_cast<std::size_t>((y * texture.width() + x) * 4);

		if (i + 3 >= px.size())
		{
			return sgc::Colorf{0.0f, 0.0f, 0.0f, 0.0f};
		}

		return sgc::Colorf{
			px[i + 0] / 255.0f,
			px[i + 1] / 255.0f,
			px[i + 2] / 255.0f,
			px[i + 3] / 255.0f
		};
	}

	/// @brief テクセルX座標をクランプする
	[[nodiscard]] static int clampTexelX(int x, const Texture& texture) noexcept
	{
		return std::max(0, std::min(x, texture.width() - 1));
	}

	/// @brief テクセルY座標をクランプする
	[[nodiscard]] static int clampTexelY(int y, const Texture& texture) noexcept
	{
		return std::max(0, std::min(y, texture.height() - 1));
	}

	/// @brief Catmull-Rom スプライン基底関数
	/// @details t = |距離| として重みを計算する。
	///          0 <= t < 1: (3t^3 - 5t^2 + 2) / 2
	///          1 <= t < 2: (-t^3 + 5t^2 - 8t + 4) / 2
	///          t >= 2: 0
	/// @param t 距離（非負）
	/// @return スプラインの重み
	[[nodiscard]] static constexpr float catmullRom(float t) noexcept
	{
		const float at = t < 0.0f ? -t : t;

		if (at < 1.0f)
		{
			return (3.0f * at * at * at - 5.0f * at * at + 2.0f) * 0.5f;
		}
		else if (at < 2.0f)
		{
			return (-at * at * at + 5.0f * at * at - 8.0f * at + 4.0f) * 0.5f;
		}

		return 0.0f;
	}
};

} // namespace mitiru::render

// ── drawSpriteFiltered 実装（Screen.hpp のインクルード後に定義） ─

#include <mitiru/core/Screen.hpp>

inline void mitiru::render::TextureFilter::drawSpriteFiltered(
	Screen& screen,
	const Texture& texture,
	const sgc::Rectf& destRect,
	FilterMode mode,
	float alpha)
{
	if (!texture.valid() || alpha < 1.0f / 255.0f)
	{
		return;
	}

	const int dstMinX = std::max(0, static_cast<int>(std::floor(destRect.x())));
	const int dstMinY = std::max(0, static_cast<int>(std::floor(destRect.y())));
	const int dstMaxX = std::min(screen.width() - 1,
		static_cast<int>(std::ceil(destRect.x() + destRect.width())) - 1);
	const int dstMaxY = std::min(screen.height() - 1,
		static_cast<int>(std::ceil(destRect.y() + destRect.height())) - 1);

	const float invW = 1.0f / destRect.width();
	const float invH = 1.0f / destRect.height();

	for (int py = dstMinY; py <= dstMaxY; ++py)
	{
		for (int px = dstMinX; px <= dstMaxX; ++px)
		{
			/// ピクセル中心のテクスチャ座標を計算する
			const float u = (static_cast<float>(px) + 0.5f - destRect.x()) * invW;
			const float v = (static_cast<float>(py) + 0.5f - destRect.y()) * invH;

			/// UV範囲外はスキップ
			if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
			{
				continue;
			}

			/// 指定モードでサンプリング
			auto texColor = sample(texture, u, v, mode);

			/// 全体アルファを適用する
			texColor.a *= alpha;

			if (texColor.a < 1.0f / 255.0f)
			{
				continue;
			}

			/// アルファブレンド付きで出力する
			screen.drawRect(
				sgc::Rectf{
					static_cast<float>(px),
					static_cast<float>(py),
					1.0f, 1.0f},
				texColor);
		}
	}
}
