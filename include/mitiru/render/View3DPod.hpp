#pragma once

/// @file View3DPod.hpp
/// @brief ゲーム DLL が副ビュー (分割画面・小窓・材質に貼る絵) を作るときに渡す POD (ABI v49、ADR 0062)
/// @details 番号 (slot 0..kMaxView3DSlots-1) はゲームが選び、host が副ビューの本体と対応を持つ。
///          同じ slot に同じ中身を毎フレーム渡しても作り直さない。大きさや旗が変わった時だけ作り直す。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

inline constexpr int kMaxView3DSlots = 8;

inline constexpr std::uint32_t kView3DShadows = 1u << 0;   ///< 主ビューの影マップを読む
inline constexpr std::uint32_t kView3DSky     = 1u << 1;   ///< skybox3D / sceneLook の空を張る

/// @brief 副ビュー 1 枚の作り (32 byte)。width / height は描く画素数で、画面のどこへ置くかは compositeView3D で決める
struct View3DPod
{
	std::int32_t  width = 0;
	std::int32_t  height = 0;
	std::uint32_t flags = kView3DShadows | kView3DSky;
	float         clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};   ///< 書いた sRGB の色
	std::uint32_t reserved = 0;
};

static_assert(sizeof(View3DPod) == 32, "View3DPod は 32 byte の POD (ABI v49)");
static_assert(offsetof(View3DPod, clear) == 12 && offsetof(View3DPod, reserved) == 28, "View3DPod の並び (ABI v49)");
static_assert(std::is_standard_layout_v<View3DPod> && std::is_trivially_copyable_v<View3DPod>);

} // namespace mitiru::render
