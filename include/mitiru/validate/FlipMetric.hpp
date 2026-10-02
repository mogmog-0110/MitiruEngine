#pragma once

/// @file FlipMetric.hpp
/// @brief NVIDIA FLIP（BSD-3、external/flip）による知覚的な画像差の宣言。
/// @details 平均値と上位 0.1% 値で、全体の明るさのずれと小さな物の消失を判定する。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <mitiru/render/ScreenCapture.hpp>
#include <mitiru/util/ImageWriter.hpp>

// FLIP.h が定義する Max / Min マクロを外へ漏らさない。
#pragma push_macro("Max")
#pragma push_macro("Min")
#pragma push_macro("DEFAULT_ILLUMINANT")
#pragma push_macro("INV_DEFAULT_ILLUMINANT")
#undef Max
#undef Min
#include <flip/src/cpp/FLIP.h>
#undef Max
#undef Min
#undef DEFAULT_ILLUMINANT
#undef INV_DEFAULT_ILLUMINANT
#pragma pop_macro("INV_DEFAULT_ILLUMINANT")
#pragma pop_macro("DEFAULT_ILLUMINANT")
#pragma pop_macro("Min")
#pragma pop_macro("Max")

namespace mitiru::validate
{

/// @brief 0.7 m 離れた幅 0.7 m、横 3840 px のモニタを既定の観察条件とする。
inline const float kDefaultFlipPpd = FLIP::calculatePPD(0.7f, 3840.0f, 0.7f);

/// @brief 誤差の大きい方から 0.1% の位置を判定に使う。
/// @details 640 x 360 では約 230 画素となり、物 1 つ分の違いを拾える。
inline constexpr float kFlipHighPercentile = 0.999f;

struct FlipResult
{
	bool  valid = false;  ///< 2 枚の大きさが揃っていて計算できたか
	float mean  = 0.0f;
	float p999  = 0.0f;   ///< kFlipHighPercentile の位置の誤差
	float max   = 0.0f;
	int   width = 0;
	int   height = 0;
	std::vector<float> errorMap;  ///< 行優先、画素ごとの FLIP 誤差 [0,1]
};

namespace detail
{

// FLIP は線形 RGB を受け取るため、sRGB を戻してから渡す。alpha は黒の上に合成する。
inline FLIP::image<FLIP::color3> toFlipImage(const render::ScreenshotData& s)
{
	FLIP::image<FLIP::color3> img(s.width, s.height);
	for (int y = 0; y < s.height; ++y)
	{
		for (int x = 0; x < s.width; ++x)
		{
			const float a = static_cast<float>(s.pixelA(x, y)) / 255.0f;
			const FLIP::color3 srgb(static_cast<float>(s.pixelR(x, y)) / 255.0f * a,
			                        static_cast<float>(s.pixelG(x, y)) / 255.0f * a,
			                        static_cast<float>(s.pixelB(x, y)) / 255.0f * a);
			img.set(x, y, FLIP::color3::sRGBToLinearRGB(srgb));
		}
	}
	return img;
}

inline void summarize(FlipResult& r)
{
	double sum = 0.0;
	for (const float e : r.errorMap) { sum += e; }
	r.mean = static_cast<float>(sum / static_cast<double>(r.errorMap.size()));
	std::vector<float> sorted = r.errorMap;
	const auto rank = static_cast<std::size_t>(kFlipHighPercentile * static_cast<float>(sorted.size() - 1));
	std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(rank), sorted.end());
	r.p999 = sorted[rank];
	r.max = *std::max_element(r.errorMap.begin(), r.errorMap.end());
}

}  // namespace detail

[[nodiscard]] inline FlipResult computeFlip(const render::ScreenshotData& reference,
                                            const render::ScreenshotData& test,
                                            float ppd = kDefaultFlipPpd)
{
	FlipResult r;
	if (!reference.isValid() || !test.isValid()
	    || reference.width != test.width || reference.height != test.height)
	{
		return r;
	}
	auto ref = detail::toFlipImage(reference);
	auto tst = detail::toFlipImage(test);
	FLIP::image<float> error(reference.width, reference.height, 0.0f);
	FLIP::Parameters params;
	params.PPD = ppd;
	FLIP::evaluate(ref, tst, false, params, error);

	r.valid  = true;
	r.width  = reference.width;
	r.height = reference.height;
	r.errorMap.resize(static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height));
	for (int y = 0; y < r.height; ++y)
	{
		for (int x = 0; x < r.width; ++x)
		{
			r.errorMap[static_cast<std::size_t>(y) * static_cast<std::size_t>(r.width) + static_cast<std::size_t>(x)]
				= error.get(x, y);
		}
	}
	detail::summarize(r);
	return r;
}

/// @brief FLIP 付属ツールと同じ magma 配色で、誤差を RGBA 画像にする。黒は同じ、黄は大きな違いを表す。
[[nodiscard]] inline render::ScreenshotData flipErrorMapImage(const FlipResult& r)
{
	render::ScreenshotData out;
	if (!r.valid) { return out; }
	out.width  = r.width;
	out.height = r.height;
	out.pixels.resize(static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height) * 4);
	const auto toByte = [](float v) {
		return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
	};
	for (std::size_t i = 0; i < r.errorMap.size(); ++i)
	{
		const int index = static_cast<int>(std::clamp(r.errorMap[i], 0.0f, 1.0f) * 255.0f + 0.5f);
		const FLIP::color3 c = FLIP::MapMagma[index];
		out.pixels[i * 4 + 0] = toByte(c.x);
		out.pixels[i * 4 + 1] = toByte(c.y);
		out.pixels[i * 4 + 2] = toByte(c.z);
		out.pixels[i * 4 + 3] = 255;
	}
	return out;
}

inline bool writeFlipErrorMapPng(const FlipResult& r, const std::filesystem::path& path)
{
	const auto img = flipErrorMapImage(r);
	if (!img.isValid()) { return false; }
	const auto png = util::encodePng(img.pixels.data(), img.width, img.height);
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
	return static_cast<bool>(out);
}

}  // namespace mitiru::validate
