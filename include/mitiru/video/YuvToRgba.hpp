#pragma once

/// @file YuvToRgba.hpp
/// @brief 8 ビットの YUV の面を RGBA に直す整数の変換 (ADR 0116)
/// @details 係数は Kr/Kb から constexpr で求め、画素ごとは整数だけで計算する。浮動小数点の実行時の揺れが入らないので、
/// 同じ YUV の面からは、どの機械でも同じ RGBA になる (撮影とリプレイの照合が動画の絵まで届く)。
/// 色空間が示されていない (UNSPECIFIED) ときは、高さ 720 以上を BT.709、それ未満を BT.601 とみなす。

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace mitiru::video
{

enum class YuvMatrix : std::uint8_t
{
	Bt601,
	Bt709,
	Bt2020,
	Identity,   ///< 4:4:4 の GBR (Y=G、U=B、V=R)
};

struct YuvLayout
{
	YuvMatrix matrix = YuvMatrix::Bt709;
	bool      fullRange = false;
	int       subX = 1;   ///< 色の面の横の間引き。4:2:0 と 4:2:2 は 1、4:4:4 は 0
	int       subY = 1;   ///< 色の面の縦の間引き。4:2:0 は 1、4:2:2 と 4:4:4 は 0
	bool      mono = false;   ///< 色の面が無い (4:0:0)
};

namespace detail
{

struct YuvCoeffs
{
	int yGain;   ///< 16.16。limited は 255/219、full は 1
	int yOffset;   ///< 引く値 (limited は 16、full は 0)
	int rv;
	int gu;
	int gv;
	int bu;
};

constexpr int fix16(double v) { return static_cast<int>(v * 65536.0 + (v >= 0.0 ? 0.5 : -0.5)); }

constexpr YuvCoeffs makeCoeffs(double kr, double kb, bool full)
{
	const double kg = 1.0 - kr - kb;
	const double cScale = full ? 1.0 : 255.0 / 224.0;   // 色差の範囲 (limited は 16..240)
	YuvCoeffs c{};
	c.yGain = full ? 65536 : fix16(255.0 / 219.0);
	c.yOffset = full ? 0 : 16;
	c.rv = fix16(2.0 * (1.0 - kr) * cScale);
	c.bu = fix16(2.0 * (1.0 - kb) * cScale);
	c.gu = fix16(-2.0 * kb * (1.0 - kb) / kg * cScale);
	c.gv = fix16(-2.0 * kr * (1.0 - kr) / kg * cScale);
	return c;
}

[[nodiscard]] constexpr YuvCoeffs coeffsFor(YuvMatrix m, bool full)
{
	switch (m)
	{
	case YuvMatrix::Bt601: return makeCoeffs(0.299, 0.114, full);
	case YuvMatrix::Bt2020: return makeCoeffs(0.2627, 0.0593, full);
	case YuvMatrix::Identity:
	case YuvMatrix::Bt709: break;
	}
	return makeCoeffs(0.2126, 0.0722, full);
}

[[nodiscard]] constexpr std::uint8_t clamp8(int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

}  // namespace detail

/// @brief Y・U・V の 3 面を RGBA (不透明) にする。stride は byte 単位。mono のときは u と v を見ない
inline void convertYuvToRgba(const std::uint8_t* y, std::ptrdiff_t yStride, const std::uint8_t* u, const std::uint8_t* v, std::ptrdiff_t cStride,
                             int width, int height, const YuvLayout& layout, std::uint8_t* rgba)
{
	if (layout.matrix == YuvMatrix::Identity && !layout.mono)
	{
		for (int py = 0; py < height; ++py)
		{
			std::uint8_t* out = rgba + static_cast<std::size_t>(py) * static_cast<std::size_t>(width) * 4;
			const std::uint8_t* yr = y + py * yStride;
			const std::uint8_t* ur = u + (py >> layout.subY) * cStride;
			const std::uint8_t* vr = v + (py >> layout.subY) * cStride;
			for (int px = 0; px < width; ++px, out += 4)
			{
				out[0] = vr[px >> layout.subX];
				out[1] = yr[px];
				out[2] = ur[px >> layout.subX];
				out[3] = 255;
			}
		}
		return;
	}
	const detail::YuvCoeffs c = detail::coeffsFor(layout.matrix, layout.fullRange);
	for (int py = 0; py < height; ++py)
	{
		std::uint8_t* out = rgba + static_cast<std::size_t>(py) * static_cast<std::size_t>(width) * 4;
		const std::uint8_t* yr = y + py * yStride;
		const std::uint8_t* ur = layout.mono ? nullptr : u + (py >> layout.subY) * cStride;
		const std::uint8_t* vr = layout.mono ? nullptr : v + (py >> layout.subY) * cStride;
		for (int px = 0; px < width; ++px, out += 4)
		{
			const int yy = static_cast<int>((static_cast<std::int64_t>(yr[px] - c.yOffset) * c.yGain + 32768) >> 16);
			int cu = 0;
			int cv = 0;
			if (!layout.mono)
			{
				cu = ur[px >> layout.subX] - 128;
				cv = vr[px >> layout.subX] - 128;
			}
			out[0] = detail::clamp8(yy + static_cast<int>((static_cast<std::int64_t>(c.rv) * cv + 32768) >> 16));
			out[1] = detail::clamp8(yy + static_cast<int>((static_cast<std::int64_t>(c.gu) * cu + static_cast<std::int64_t>(c.gv) * cv + 32768) >> 16));
			out[2] = detail::clamp8(yy + static_cast<int>((static_cast<std::int64_t>(c.bu) * cu + 32768) >> 16));
			out[3] = 255;
		}
	}
}

}  // namespace mitiru::video
