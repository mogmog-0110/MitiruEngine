#pragma once

/// @file HdrMath.hpp
/// @brief HDR 出力 (scRGB) の式。DX12 の HLSL (DX12Tonemap.hpp の highlight excess の PS と Dx12HdrPresentPass.hpp の Present の PS) と同じ式で、
///        試験は GPU の読み戻しをこの CPU 実装と比べる
/// @details 色の仕上げまでの 3D は今までどおり 8 bit の sRGB (SDR) の絵に描く。HDR のときは、その絵に載らなかった highlight excess
///          (トーンマップの肩を表示のピークまで開いた色 - SDR の色、線形、1 = SDR の基準の白) を別に書き、Present の直前に
///          「SDR の絵を線形へ戻した色 + highlight excess」を基準の白の明るさ (nits / 80) で掛けて scRGB (FP16) に書く。
///          excess が 0 の画素は SDR の絵のまま基準の白の明るさに載るだけなので、SDR の範囲の色は SDR の経路と一致する。

#include <algorithm>
#include <array>
#include <cmath>

namespace mitiru::render
{

/// scRGB の (1, 1, 1) が表す明るさ (nits)
inline constexpr float kScRgbWhiteNits = 80.0f;
/// Windows が SDR の基準の白を取れないときの値 (BT.2408 の参照白)
inline constexpr float kFallbackPaperWhiteNits = 203.0f;
inline constexpr float kFallbackPeakNits = 1000.0f;
inline constexpr float kMaxHeadroom = 16.0f;

/// 利用者の設定 graphics.hdr。auto はモニターが HDR のときだけ scRGB で Present する
enum class HdrMode : unsigned char { Off, Auto };

[[nodiscard]] inline const char* hdrModeName(HdrMode m) noexcept { return m == HdrMode::Auto ? "auto" : "off"; }

struct HdrParams
{
	float paperWhiteNits = kFallbackPaperWhiteNits;   ///< OS の SDR の基準の白 (利用者が決める)
	float peakNits = kFallbackPeakNits;               ///< 表示が出せる最大の明るさ (DXGI_OUTPUT_DESC1::MaxLuminance)
};

/// 基準の白に対するピークの比 (1..16)。肩の漸近線
[[nodiscard]] inline float hdrHeadroom(const HdrParams& p) noexcept
{
	const float paper = std::max(p.paperWhiteNits, 1.0f);
	return std::clamp(p.peakNits / paper, 1.0f, kMaxHeadroom);
}

/// DISPLAYCONFIG_SDR_WHITE_LEVEL の生の値 (1000 = 80 nits) を nits に直す。0 は取得できなかったことを表す
[[nodiscard]] inline float sdrWhiteLevelToNits(unsigned long raw) noexcept
{
	return raw == 0 ? kFallbackPaperWhiteNits : static_cast<float>(raw) / 1000.0f * kScRgbWhiteNits;
}

using HdrRgb = std::array<float, 3>;

[[nodiscard]] inline float srgbEncode(float x) noexcept
{
	return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(std::max(x, 0.0031308f), 1.0f / 2.4f) - 0.055f;
}

[[nodiscard]] inline float srgbDecode(float c) noexcept
{
	return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/// Khronos PBR Neutral の肩。最大成分が 0.9 を超えたら、漸近線 asymptote へ寄せる有理式で縮め、色全体を同じ比で縮める。
/// asymptote = 1 は DX12_TONEMAP_PS の neutralShoulder と同じ。0.9 で値も傾きも連続
[[nodiscard]] inline HdrRgb neutralShoulder(HdrRgb c, float asymptote) noexcept
{
	constexpr float knee = 0.9f;
	const float d = asymptote - knee;
	const float peak = std::max({c[0], c[1], c[2]});
	if (peak <= knee) { return c; }
	const float scale = (asymptote - d * d / (peak + d - knee)) / peak;
	return {c[0] * scale, c[1] * scale, c[2] * scale};
}

/// トーンマップの肩から色調補正、表示ガンマまで (sRGB に符号化する直前の、線形で 1 = 基準の白)。asymptote = 1 が SDR の経路。
/// 色調補正のコントラストの上端は asymptote で止める (SDR は 1)
[[nodiscard]] inline HdrRgb gradeToDisplay(HdrRgb scene, float exposure, float saturation, float contrast, float gamma, float asymptote) noexcept
{
	for (float& v : scene) { v = std::max(v * exposure, 0.0f); }
	HdrRgb m = neutralShoulder(scene, asymptote);
	const float luma = 0.2126f * m[0] + 0.7152f * m[1] + 0.0722f * m[2];
	const float exponent = 2.2f / std::max(gamma, 1e-4f);
	for (float& v : m)
	{
		v = luma + (v - luma) * saturation;
		v = std::clamp(0.18f + (v - 0.18f) * contrast, 0.0f, asymptote);
		v = std::pow(v, exponent);
	}
	return m;
}

/// SDR の絵に載らなかった highlight excess (線形、0 以上)。headroom が 1 なら常に 0
[[nodiscard]] inline HdrRgb hdrExcess(const HdrRgb& scene, float exposure, float saturation, float contrast, float gamma, float headroom) noexcept
{
	const HdrRgb sdr = gradeToDisplay(scene, exposure, saturation, contrast, gamma, 1.0f);
	const HdrRgb hdr = gradeToDisplay(scene, exposure, saturation, contrast, gamma, std::max(headroom, 1.0f));
	return {std::max(hdr[0] - sdr[0], 0.0f), std::max(hdr[1] - sdr[1], 0.0f), std::max(hdr[2] - sdr[2], 0.0f)};
}

/// Present の PS。sdr は 8 bit の sRGB 符号化済みの絵 (0..1)、excess は hdrExcess。返すのは scRGB
[[nodiscard]] inline HdrRgb scRgbOutput(const HdrRgb& sdr, const HdrRgb& excess, float paperWhiteNits) noexcept
{
	const float scale = paperWhiteNits / kScRgbWhiteNits;
	return {(srgbDecode(sdr[0]) + excess[0]) * scale, (srgbDecode(sdr[1]) + excess[1]) * scale, (srgbDecode(sdr[2]) + excess[2]) * scale};
}

}  // namespace mitiru::render
