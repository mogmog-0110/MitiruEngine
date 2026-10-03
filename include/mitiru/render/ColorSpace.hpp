#pragma once

/// @file ColorSpace.hpp
/// @brief 書いた色 (sRGB) と GPU が光を足し合わせる線形の色の行き来
/// @details 描画 API が受け取る色 (Material::diffuse、光、環境光、フォグ、頂点色、インスタンスの色、
///          デカールや粒の色) は、16 進や CSS と同じ sRGB で書いた値として扱う。GPU へ上げる所で
///          linearColor を通して線形にし、トーンマップの後で sRGB へ戻す。テクスチャは SRV を
///          _UNORM_SRGB にしてハードウェアが同じ変換をする。glTF の係数と COLOR_0 は仕様で線形なので、
///          読み込む側が srgbColor で書いた色の側へ寄せる。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace mitiru::render
{

[[nodiscard]] inline float srgbToLinear(float v) noexcept
{
	return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline float linearToSrgb(float v) noexcept
{
	if (v <= 0.0f) { return 0.0f; }
	if (v >= 1.0f) { return 1.0f; }
	return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

/// @brief 書いた RGB を線形へ。最大の成分が 1 を超える色 (光や点滅の色を明るさごと書いたもの) は、
///        1 に収めた色を線形にしてから同じ倍率を掛ける。成分ごとに曲線へ通すと 3 倍の白が 12 倍になる
[[nodiscard]] inline std::array<float, 3> linearRgb(float r, float g, float b) noexcept
{
	const float m = std::max({r, g, b, 1.0f});
	return {srgbToLinear(r / m) * m, srgbToLinear(g / m) * m, srgbToLinear(b / m) * m};
}

/// @brief linearRgb の色の型版。アルファは覆う割合なので変えない。Color は r/g/b/a を持つ型 (sgc::Colorf)。
///        テンプレートにしてあるのは、色の型を持たないテクスチャ圧縮の側からもこのヘッダを読むため
template <class Color>
[[nodiscard]] Color linearColor(const Color& srgb) noexcept
{
	const auto c = linearRgb(srgb.r, srgb.g, srgb.b);
	return {c[0], c[1], c[2], srgb.a};
}

/// @brief 前と同じ色なら、前に線形にした結果を返す (pow を通さない)
/// @details 描画ごとの定数バッファは、場面の色 (環境光・フォグ等) と並びの近い描画の材質の色を何度も線形にする。
///          前と同じかはビットで比べるので、結果は linearColor を毎回呼んだときと同じになる。
template <class Color>
class LinearColorMemo
{
public:
	[[nodiscard]] const Color& operator()(const Color& srgb) noexcept
	{
		if (!m_valid || std::memcmp(&srgb, &m_in, sizeof(Color)) != 0)
		{
			m_in = srgb;
			m_out = linearColor(srgb);
			m_valid = true;
		}
		return m_out;
	}

private:
	Color m_in{};
	Color m_out{};
	bool m_valid = false;
};

/// @brief 線形の色を書いた色の側へ (0..1 に収める)。アルファは変えない
template <class Color>
[[nodiscard]] Color srgbColor(const Color& linear) noexcept
{
	return {linearToSrgb(linear.r), linearToSrgb(linear.g), linearToSrgb(linear.b), linear.a};
}

} // namespace mitiru::render
