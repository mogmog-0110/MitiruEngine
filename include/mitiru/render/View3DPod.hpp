#pragma once

/// @file View3DPod.hpp
/// @brief ゲーム DLL が副ビュー (分割画面・小窓・材質に貼る絵) を作るときに渡す POD (ABI v49、ADR 0062)
/// @details 番号 (slot 0..kMaxView3DSlots-1) はゲームが選び、host が副ビューの本体と対応を持つ。
///          同じ slot に同じ中身を毎フレーム渡しても作り直さない。大きさや旗が変わった時だけ作り直す。
///          影・TAA・SSAO・bloom・AA は主ビューの設定 (sceneLook3D) に従って副ビューにも掛かる。bit2..5 はそれを
///          このビューだけ止める (v50、ADR 0067)。どれも 0 のままなら主ビューと同じ扱い。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

inline constexpr int kMaxView3DSlots = 8;

inline constexpr std::uint32_t kView3DShadows            = 1u << 0;   ///< 自分のカメラに合わせた影マップで影を受ける
inline constexpr std::uint32_t kView3DSky                = 1u << 1;   ///< skybox3D / sceneLook の空を張る (空気遠近と体積フォグも)
inline constexpr std::uint32_t kView3DNoTemporal         = 1u << 2;   ///< TAA を掛けない。主ビューが TAA でも FXAA にする (履歴の要らない地図など)
inline constexpr std::uint32_t kView3DNoAmbientOcclusion = 1u << 3;
inline constexpr std::uint32_t kView3DNoBloom            = 1u << 4;
inline constexpr std::uint32_t kView3DNoAntiAlias        = 1u << 5;   ///< TAA も FXAA も掛けない

/// @brief 副ビュー 1 枚の作り (32 byte)。width / height は描く画素数で、画面のどこへ置くかは compositeView3D で決める
struct View3DPod
{
	std::int32_t  width = 0;
	std::int32_t  height = 0;
	std::uint32_t flags = kView3DShadows | kView3DSky;
	float         clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};   ///< 書いた sRGB の色
	std::int32_t  shadowMapSize = 0;   ///< 影マップの一辺 (v50)。0 なら主ビューと同じ。256〜4096 の 2 の冪へ切り上げる
};

static_assert(sizeof(View3DPod) == 32, "View3DPod は 32 byte の POD (ABI v49)");
static_assert(offsetof(View3DPod, clear) == 12 && offsetof(View3DPod, shadowMapSize) == 28, "View3DPod の並び (ABI v49)");
static_assert(std::is_standard_layout_v<View3DPod> && std::is_trivially_copyable_v<View3DPod>);

} // namespace mitiru::render
