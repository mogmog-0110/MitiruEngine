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
#include <cstddef>
#include <cstdint>
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

/// @brief 直近の何色かの線形にした結果を覚える (色の入れ替わりが多い描画で pow を通さない)
/// @details 材質の色が 8 色ほどを順に繰り返す箱の群れは、1 色だけを覚える LinearColorMemo だと毎回外れる。
///          色のビットで引く直接写像の表で、結果は linearColor を毎回呼んだ時と同じ。
template <class Color, std::size_t N = 64>
class LinearColorTable
{
	static_assert((N & (N - 1)) == 0, "N は 2 の冪");

public:
	[[nodiscard]] const Color& operator()(const Color& srgb) noexcept
	{
		std::uint32_t bits[4] = {};
		static_assert(sizeof(Color) == sizeof(bits));
		std::memcpy(bits, &srgb, sizeof(bits));
		std::uint32_t h = bits[0] * 0x9E3779B1u;
		h = (h ^ (h >> 15) ^ bits[1]) * 0x85EBCA6Bu;
		h = (h ^ (h >> 13) ^ bits[2]) * 0xC2B2AE35u;
		h = (h ^ (h >> 16) ^ bits[3]) * 0x27D4EB2Fu;
		Entry& e = m_entries[(h ^ (h >> 15)) & (N - 1)];
		if (!e.valid || std::memcmp(&srgb, &e.in, sizeof(Color)) != 0)
		{
			e.in = srgb;
			e.out = linearColor(srgb);
			e.valid = true;
		}
		return e.out;
	}

private:
	struct Entry
	{
		Color in{};
		Color out{};
		bool valid = false;
	};
	Entry m_entries[N]{};
};

/// @brief 線形の色を書いた色の側へ (0..1 に収める)。アルファは変えない
template <class Color>
[[nodiscard]] Color srgbColor(const Color& linear) noexcept
{
	return {linearToSrgb(linear.r), linearToSrgb(linear.g), linearToSrgb(linear.b), linear.a};
}

} // namespace mitiru::render
