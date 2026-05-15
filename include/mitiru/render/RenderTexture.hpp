#pragma once

/// @file RenderTexture.hpp
/// @brief オフスクリーンレンダーターゲット（Siv3D RenderTexture風）
/// @details 画面ではなく内部テクスチャに描画し、その結果を
///          テクスチャとして他の描画に使用できる。
///          ポストエフェクトやミニマップ等に活用する。
///
/// @code
/// mitiru::render::RenderTexture rt(256, 256);
/// rt.clear({0, 0, 0, 1});
/// rt.drawRect({10, 10, 50, 50}, {1, 0, 0, 1});
/// // rt.texture() で結果をTextureとして取得可能
/// @endcode

#include <mitiru/render/Texture.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <sgc/types/Color.hpp>
#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>

namespace mitiru::render
{

/// @brief オフスクリーンレンダーターゲット
/// @details ソフトウェアフレームバッファへの描画を行い、
///          結果をTextureとして取得する。
class RenderTexture
{
public:
	/// @brief コンストラクタ
	/// @param width 幅（ピクセル）
	/// @param height 高さ（ピクセル）
	RenderTexture(int width, int height)
		: m_width(width)
		, m_height(height)
		, m_pixels(static_cast<std::size_t>(width) * height * 4, 0)
	{
	}

	/// @brief クリアする
	/// @param color クリア色
	void clear(const sgc::Colorf& color = {0, 0, 0, 1})
	{
		const auto r = toByte(color.r);
		const auto g = toByte(color.g);
		const auto b = toByte(color.b);
		const auto a = toByte(color.a);
		const auto total = static_cast<std::size_t>(m_width) * m_height;
		for (std::size_t i = 0; i < total; ++i)
		{
			m_pixels[i * 4]     = r;
			m_pixels[i * 4 + 1] = g;
			m_pixels[i * 4 + 2] = b;
			m_pixels[i * 4 + 3] = a;
		}
	}

	/// @brief 矩形を描画する
	/// @param rect 矩形領域
	/// @param color 描画色
	void drawRect(const sgc::Rectf& rect, const sgc::Colorf& color)
	{
		const int x0 = std::max(0, static_cast<int>(rect.x()));
		const int y0 = std::max(0, static_cast<int>(rect.y()));
		const int x1 = std::min(m_width, static_cast<int>(rect.x() + rect.width()));
		const int y1 = std::min(m_height, static_cast<int>(rect.y() + rect.height()));

		const auto r = toByte(color.r);
		const auto g = toByte(color.g);
		const auto b = toByte(color.b);
		const auto a = toByte(color.a);

		for (int y = y0; y < y1; ++y)
		{
			for (int x = x0; x < x1; ++x)
			{
				const auto idx = static_cast<std::size_t>((y * m_width + x) * 4);
				// アルファブレンド
				const float srcA = color.a;
				const float dstR = m_pixels[idx] / 255.0f;
				const float dstG = m_pixels[idx + 1] / 255.0f;
				const float dstB = m_pixels[idx + 2] / 255.0f;
				m_pixels[idx]     = toByte(color.r * srcA + dstR * (1 - srcA));
				m_pixels[idx + 1] = toByte(color.g * srcA + dstG * (1 - srcA));
				m_pixels[idx + 2] = toByte(color.b * srcA + dstB * (1 - srcA));
				m_pixels[idx + 3] = std::max(m_pixels[idx + 3], a);
			}
		}
	}

	/// @brief ピクセルを直接設定する
	/// @param x X座標
	/// @param y Y座標
	/// @param color ピクセル色
	void setPixel(int x, int y, const sgc::Colorf& color)
	{
		if (x < 0 || x >= m_width || y < 0 || y >= m_height) return;
		const auto idx = static_cast<std::size_t>((y * m_width + x) * 4);
		m_pixels[idx]     = toByte(color.r);
		m_pixels[idx + 1] = toByte(color.g);
		m_pixels[idx + 2] = toByte(color.b);
		m_pixels[idx + 3] = toByte(color.a);
	}

	/// @brief 結果をTextureとして取得する
	/// @return テクスチャオブジェクト
	[[nodiscard]] Texture texture() const
	{
		return Texture(m_width, m_height, m_pixels);
	}

	/// @brief 指定座標のピクセル色を取得する
	/// @param x X座標
	/// @param y Y座標
	/// @return ピクセル色（範囲外は黒）
	[[nodiscard]] sgc::Colorf pixelAt(int x, int y) const noexcept
	{
		if (x < 0 || x >= m_width || y < 0 || y >= m_height)
		{
			return {0, 0, 0, 1};
		}
		const auto idx = static_cast<std::size_t>((y * m_width + x) * 4);
		return {
			m_pixels[idx] / 255.0f,
			m_pixels[idx + 1] / 255.0f,
			m_pixels[idx + 2] / 255.0f,
			m_pixels[idx + 3] / 255.0f
		};
	}

	/// @brief 幅を取得する
	[[nodiscard]] int width() const noexcept { return m_width; }

	/// @brief 高さを取得する
	[[nodiscard]] int height() const noexcept { return m_height; }

	/// @brief ピクセルデータを取得する
	[[nodiscard]] const std::vector<uint8_t>& pixels() const noexcept { return m_pixels; }

private:
	/// @brief float [0,1] をバイト [0,255] に変換する
	static uint8_t toByte(float v) noexcept
	{
		return static_cast<uint8_t>(
			std::max(0.0f, std::min(255.0f, v * 255.0f)));
	}

	int m_width;                      ///< 幅
	int m_height;                     ///< 高さ
	std::vector<uint8_t> m_pixels;    ///< RGBA8ピクセルバッファ
};

} // namespace mitiru::render
