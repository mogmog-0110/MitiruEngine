#pragma once

/// @file HdrCapture.hpp
/// @brief トーンマップの肩を適用する前の線形 HDR を読み戻す撮影用拡張 (ADR 0148)。ABI を変えず、`IRenderer3D::invokeExtension` の id で呼ぶ。
/// @details 露出、AO、bloom、彩度、コントラストを適用した線形シーンを読み戻す。肩、sRGB、FXAA、アウトライン、2D の重ねは適用前となる。
/// 字幕、暗転、レターボックスは含まない。既定は無効で、呼ばない限り GPU の処理も画像も変わらない。
/// 法線と ID の補助パスは、film が照明なしの材質で描いた画像を kind 0 で撮る (ADR 0153)。描画器に足したのは速度の kind 1 だけ。

#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

/// invokeExtension の id。data は HdrCaptureRequest。on が 1 の間、撮影用ターゲットにも書く。0 で止める。戻り値は 0、描画先が使えなければ -1。
inline constexpr std::uint32_t kExtHdrCaptureEnable = 2;
/// invokeExtension の id。data は HdrCaptureRequest。直前に描き終えたフレームを RGB の float で rgb に書き、書いた float の数を返す。行 0 が上となる。
/// capacityFloats が足りなければ width と height だけを書いて -2、まだ撮っていなければ -1 を返す。
inline constexpr std::uint32_t kExtHdrCaptureRead = 3;

/// HdrCaptureRequest::kind。0 = 露出済みの線形のシーン。1 = 画面の動き (下)。他の値は予約で、-1 を返す。
inline constexpr std::uint32_t kHdrCaptureScene = 0;
/// 直前のフレームの動きベクトルを (x, y, 0) の float で返す。単位は画素、値は「今の位置 − 前のフレームの位置」で、右と下が正、行 0 が上。
/// Enable で頼むと、TAA も動きのぼけも無くても動きベクトルのパスを回す。頼んだ最初のフレームと、カメラの切れ目 (resetTemporalHistory) の直後は 0。
inline constexpr std::uint32_t kHdrCaptureVelocity = 1;

struct HdrCaptureRequest
{
	std::uint32_t kind = 0;             ///< 撮る物。kHdrCaptureScene か kHdrCaptureVelocity。Enable と Read で同じ値を渡す (Read は Enable の kind と違えば -1)
	std::uint32_t on = 0;               ///< Enable の入力
	std::uint32_t width = 0;            ///< Read の出力
	std::uint32_t height = 0;           ///< Read の出力
	std::uint32_t capacityFloats = 0;   ///< Read の入力 (rgb の大きさ。float の数)
	float*        rgb = nullptr;        ///< Read の出力先
};

static_assert(std::is_trivially_copyable_v<HdrCaptureRequest> && std::is_standard_layout_v<HdrCaptureRequest>,
              "HdrCaptureRequest は POD。invokeExtension の data の形 (ADR 0099 の表)");

}  // namespace mitiru::render
