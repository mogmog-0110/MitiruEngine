#pragma once

/// @file ColorVision.hpp
/// @brief 色覚の型ごとの見え方の再現 (simulate) と、見分けやすくする補正 (correct) の 3x3 行列。
/// @details どちらも線形 RGB に掛ける 1 次変換なので、3D LUT を焼かずに行列 1 つでフレーム全体へ掛けられる
///          (LUT に焼いても同じ値になる)。
///          再現は Machado, Oliveira, Fernandes (2009) の重度 1.0 の行列を、重さ s で単位行列と線形に混ぜる。
///          補正は Fidaner らの daltonize と同じ形で、再現で失われる差 (c - sim(c)) を見分けられる軸へ
///          移して足す。灰色は再現で変わらないので、補正でも変わらない。

#include <array>
#include <cstdint>
#include <cmath>
#include <string_view>

namespace mitiru::render
{

enum class ColorVisionMode : std::uint8_t { Off, Protanopia, Deuteranopia, Tritanopia };
enum class ColorFilterKind : std::uint8_t { Correct, Simulate };

struct ColorFilterSettings
{
	ColorVisionMode mode = ColorVisionMode::Off;
	ColorFilterKind kind = ColorFilterKind::Correct;
	float strength = 1.0f;   ///< 0..1

	[[nodiscard]] bool operator==(const ColorFilterSettings&) const noexcept = default;
};

/// 行優先の 3x3。out[r] = sum_c m[r*3+c] * in[c]
using ColorMatrix3 = std::array<float, 9>;

inline constexpr ColorMatrix3 kColorIdentity = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };

[[nodiscard]] constexpr ColorMatrix3 machadoFull(ColorVisionMode mode) noexcept
{
	switch (mode)
	{
	case ColorVisionMode::Protanopia:
		return { 0.152286f, 1.052583f, -0.204868f,
		         0.114503f, 0.786281f, 0.099216f,
		        -0.003882f, -0.048116f, 1.051998f };
	case ColorVisionMode::Deuteranopia:
		return { 0.367322f, 0.860646f, -0.227968f,
		         0.280085f, 0.672501f, 0.047413f,
		        -0.011820f, 0.042940f, 0.968881f };
	case ColorVisionMode::Tritanopia:
		return { 1.255528f, -0.076749f, -0.178779f,
		        -0.078411f, 0.930809f, 0.147602f,
		         0.004733f, 0.691367f, 0.303900f };
	case ColorVisionMode::Off:
		break;
	}
	return kColorIdentity;
}

[[nodiscard]] constexpr ColorMatrix3 mul(const ColorMatrix3& a, const ColorMatrix3& b) noexcept
{
	ColorMatrix3 out{};
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
		}
	}
	return out;
}

[[nodiscard]] constexpr ColorMatrix3 lerpMatrix(const ColorMatrix3& a, const ColorMatrix3& b, float t) noexcept
{
	ColorMatrix3 out{};
	for (int i = 0; i < 9; ++i) { out[i] = a[i] + (b[i] - a[i]) * t; }
	return out;
}

[[nodiscard]] constexpr std::array<float, 3> applyColorMatrix(const ColorMatrix3& m, const std::array<float, 3>& c) noexcept
{
	return { m[0] * c[0] + m[1] * c[1] + m[2] * c[2],
	         m[3] * c[0] + m[4] * c[1] + m[5] * c[2],
	         m[6] * c[0] + m[7] * c[1] + m[8] * c[2] };
}

[[nodiscard]] constexpr ColorMatrix3 simulationMatrix(ColorVisionMode mode, float severity) noexcept
{
	const float s = severity < 0.0f ? 0.0f : (severity > 1.0f ? 1.0f : severity);
	return lerpMatrix(kColorIdentity, machadoFull(mode), s);
}

/// @brief 失われる差をどこへ移すか。赤緑の型は緑と青へ、青黄の型は赤と緑へ。
[[nodiscard]] constexpr ColorMatrix3 daltonizeShift(ColorVisionMode mode) noexcept
{
	if (mode == ColorVisionMode::Tritanopia) { return { 1, 0, 0.7f, 0, 1, 0.7f, 0, 0, 0 }; }
	return { 0, 0, 0, 0.7f, 1, 0, 0.7f, 0, 1 };
}

/// @brief 補正 = I + t * Shift * (I - Sim)。
[[nodiscard]] constexpr ColorMatrix3 correctionMatrix(ColorVisionMode mode, float strength) noexcept
{
	if (mode == ColorVisionMode::Off) { return kColorIdentity; }
	const ColorMatrix3 sim = machadoFull(mode);
	ColorMatrix3 lost{};
	for (int i = 0; i < 9; ++i) { lost[i] = kColorIdentity[i] - sim[i]; }
	const ColorMatrix3 moved = mul(daltonizeShift(mode), lost);
	const float t = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
	ColorMatrix3 out{};
	for (int i = 0; i < 9; ++i) { out[i] = kColorIdentity[i] + t * moved[i]; }
	return out;
}

[[nodiscard]] constexpr ColorMatrix3 colorFilterMatrix(const ColorFilterSettings& s) noexcept
{
	if (s.mode == ColorVisionMode::Off) { return kColorIdentity; }
	return s.kind == ColorFilterKind::Simulate ? simulationMatrix(s.mode, s.strength)
	                                           : correctionMatrix(s.mode, s.strength);
}

[[nodiscard]] constexpr bool colorFilterActive(const ColorFilterSettings& s) noexcept
{
	return s.mode != ColorVisionMode::Off && s.strength > 0.0f;
}

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

/// @brief sRGB (0..1) の 1 色にフィルタを掛ける。GPU の pass と同じ計算 (テストと撮影の照合用)。
[[nodiscard]] inline std::array<float, 3> filterSrgb(const ColorMatrix3& m, const std::array<float, 3>& srgb) noexcept
{
	const auto lin = applyColorMatrix(m, { srgbToLinear(srgb[0]), srgbToLinear(srgb[1]), srgbToLinear(srgb[2]) });
	return { linearToSrgb(lin[0]), linearToSrgb(lin[1]), linearToSrgb(lin[2]) };
}

[[nodiscard]] constexpr std::string_view colorVisionModeName(ColorVisionMode m) noexcept
{
	switch (m)
	{
	case ColorVisionMode::Off:          return "off";
	case ColorVisionMode::Protanopia:   return "protanopia";
	case ColorVisionMode::Deuteranopia: return "deuteranopia";
	case ColorVisionMode::Tritanopia:   return "tritanopia";
	}
	return "off";
}

}  // namespace mitiru::render
