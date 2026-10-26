#pragma once

/// @file ContrastCheck.hpp
/// @brief UI の文字と下地のコントラスト比の計算と判定 (RmlUi には依らない純粋な部分)。
/// @details 基準は XAG 102: 標準の文字は 4.5:1、大きな文字 (PC は 1080p で 36 px) は 3:1。比は WCAG の相対輝度で測る。
///          文字の下がゲームの絵に透けるとき (下地が不透明でないとき) は、下の絵が黒から白のどこでも成り立つかで判定し、
///          成り立たない時は Fail でなく Unknown とする: 本当の下地は画面を撮って確かめるしかなく、通ったように
///          見せかけないため。色覚の型ごとの見え方 (render/ColorVision.hpp の Machado 行列) での比も添える。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <mitiru/render/ColorVision.hpp>

namespace mitiru::ui_rml
{

/// sRGB の 1 色。各成分 0..1、alpha 0..1
struct Rgba
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 1.0f;
};

[[nodiscard]] inline float srgbChannelToLinear(float c) noexcept
{
	return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline float relativeLuminance(const Rgba& c) noexcept
{
	return 0.2126f * srgbChannelToLinear(c.r) + 0.7152f * srgbChannelToLinear(c.g) + 0.0722f * srgbChannelToLinear(c.b);
}

[[nodiscard]] inline float contrastRatio(float luminanceA, float luminanceB) noexcept
{
	const float hi = std::max(luminanceA, luminanceB);
	const float lo = std::min(luminanceA, luminanceB);
	return (hi + 0.05f) / (lo + 0.05f);
}

/// @brief fg を bg の上に重ねた色 (alpha 合成)。bg が半透明なら結果も半透明
[[nodiscard]] inline Rgba compositeOver(const Rgba& fg, const Rgba& bg) noexcept
{
	const float a = fg.a + bg.a * (1.0f - fg.a);
	if (a <= 0.0f) { return {}; }
	auto mix = [&](float f, float b) { return (f * fg.a + b * bg.a * (1.0f - fg.a)) / a; };
	return { mix(fg.r, bg.r), mix(fg.g, bg.g), mix(fg.b, bg.b), a };
}

enum class ContrastVerdict : std::uint8_t
{
	Pass,
	Fail,      ///< 下地が不透明で、基準に届かない
	Unknown,   ///< 下地がゲームの絵に透けていて、黒から白までのどの絵の上でも成り立つとは言えない
};

[[nodiscard]] constexpr const char* contrastVerdictName(ContrastVerdict v) noexcept
{
	switch (v)
	{
	case ContrastVerdict::Pass:    return "pass";
	case ContrastVerdict::Fail:    return "fail";
	case ContrastVerdict::Unknown: return "unknown";
	}
	return "unknown";
}

/// XAG 102 の基準。large は文字が大きいか (PC は 1080p で 36 px、画面の高さに比例させる)
[[nodiscard]] constexpr float requiredContrast(bool large) noexcept { return large ? 3.0f : 4.5f; }
[[nodiscard]] inline float largeTextPx(float screenHeight) noexcept { return 36.0f * screenHeight / 1080.0f; }

struct ContrastResult
{
	float ratio = 1.0f;          ///< 不透明な下地ならその比。透けるなら黒から白までの最悪の比
	float required = 4.5f;
	ContrastVerdict verdict = ContrastVerdict::Unknown;
};

/// @brief 文字色 fg と、下の画面に重なる前の下地 bg (alpha 付き) から判定する。
/// @details 文字が半透明なら、下地と下の絵の上に重ねた色で測る。下の絵の輝度は 0..1 のどこにもなり得るとして、
///          比がいちばん低くなる所 (文字の輝度と下地の輝度が重なる所) を探す。
[[nodiscard]] inline ContrastResult judgeContrast(const Rgba& fg, const Rgba& bg, bool large) noexcept
{
	ContrastResult out;
	out.required = requiredContrast(large);
	auto ratioOver = [&](const Rgba& backdrop) {
		const Rgba flat = compositeOver(bg, backdrop);
		const Rgba text = compositeOver(fg, flat);
		return contrastRatio(relativeLuminance(text), relativeLuminance(flat));
	};
	const Rgba black{ 0, 0, 0, 1 };
	const Rgba white{ 1, 1, 1, 1 };
	if (bg.a >= 0.999f)
	{
		out.ratio = ratioOver(black);
		out.verdict = out.ratio >= out.required ? ContrastVerdict::Pass : ContrastVerdict::Fail;
		return out;
	}
	// 下の絵が黒から白まで動くとき、下地+文字の合成も連続に動く。端と、途中を 1/64 刻みで見て最悪を取る
	float worst = ratioOver(black);
	for (int i = 0; i <= 64; ++i)
	{
		const float v = static_cast<float>(i) / 64.0f;
		worst = std::min(worst, ratioOver({ v, v, v, 1.0f }));
	}
	worst = std::min(worst, ratioOver(white));
	out.ratio = worst;
	out.verdict = worst >= out.required ? ContrastVerdict::Pass : ContrastVerdict::Unknown;
	return out;
}

/// @brief 色覚の型ごとの見え方での比 (不透明な組だけ。透ける組は 0)。順に protan, deutan, tritan
[[nodiscard]] inline std::array<float, 3> contrastUnderColorVision(const Rgba& textFlat, const Rgba& backFlat) noexcept
{
	std::array<float, 3> out{ 0.0f, 0.0f, 0.0f };
	if (backFlat.a < 0.999f) { return out; }
	const render::ColorVisionMode modes[3] = { render::ColorVisionMode::Protanopia, render::ColorVisionMode::Deuteranopia,
	                                           render::ColorVisionMode::Tritanopia };
	for (int i = 0; i < 3; ++i)
	{
		const render::ColorMatrix3 m = render::simulationMatrix(modes[i], 1.0f);
		const auto t = render::filterSrgb(m, { textFlat.r, textFlat.g, textFlat.b });
		const auto b = render::filterSrgb(m, { backFlat.r, backFlat.g, backFlat.b });
		out[static_cast<std::size_t>(i)] = contrastRatio(relativeLuminance({ t[0], t[1], t[2], 1.0f }),
		                                                 relativeLuminance({ b[0], b[1], b[2], 1.0f }));
	}
	return out;
}

/// 画面の文字 1 か所の検査結果
struct ContrastFinding
{
	std::string where;    ///< 要素の目印 (タグ#id.class)
	std::string text;     ///< 文字の頭 (見分け用)
	Rgba color;
	Rgba background;      ///< 下の画面に重なる前に合成した下地 (alpha が 1 未満なら透ける)
	float fontPx = 0.0f;
	bool large = false;
	std::string uncertain;   ///< 色だけでは測れない理由 (画像や gradient の下地、opacity、filter)。空でなければ verdict は Unknown 以上に良くならない
	ContrastResult result;
	std::array<float, 3> underColorVision{ 0.0f, 0.0f, 0.0f };
};

[[nodiscard]] inline int countVerdict(const std::vector<ContrastFinding>& all, ContrastVerdict v) noexcept
{
	return static_cast<int>(std::count_if(all.begin(), all.end(),
		[v](const ContrastFinding& f) { return f.result.verdict == v; }));
}

}  // namespace mitiru::ui_rml
