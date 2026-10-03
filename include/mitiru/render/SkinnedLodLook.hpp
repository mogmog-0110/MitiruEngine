#pragma once

/// @file SkinnedLodLook.hpp
/// @brief ゲーム DLL がスキンのキャラの LOD の選び方を決める POD (ABI v50、ADR 0065 / 0067)。Screen::skinnedLod に渡す
/// @details 描画だけを変える。ゲームの当たり判定は DLL が同じ AnimPoseParams から出す骨で決まり、段に依らない。
///          host は次に呼ばれるまで同じ設定で描く。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

/// @brief SkinnedLodLook::flags の bit
inline constexpr std::uint8_t kSkinnedLodNoPoseThinning = 1u << 0;   ///< 遠い段でも姿勢を毎フレーム評価する

/// @brief 24 byte。screen と scale は 0 で既定 (0.25 / 0.12 / 0.06、倍率 1)
struct SkinnedLodLook
{
	float        screen[3] = {0.0f, 0.0f, 0.0f};   ///< 段 1..3 へ移る、外接球の直径が画面の高さに占める割合
	float        scale = 0.0f;                     ///< 割合に掛ける倍率 (2 なら同じ距離で 1 つ細かい段を使う)
	std::int8_t  forcedLevel = -1;                 ///< -1 = 画面の大きさで選ぶ、0..3 = 全員をその段に固定
	std::uint8_t flags = 0;                        ///< kSkinnedLod*
	std::uint8_t reserved[6] = {};
};

static_assert(sizeof(SkinnedLodLook) == 24 && offsetof(SkinnedLodLook, forcedLevel) == 16,
              "SkinnedLodLook は 24 byte の POD (ABI v50)");
static_assert(std::is_standard_layout_v<SkinnedLodLook> && std::is_trivially_copyable_v<SkinnedLodLook>);

} // namespace mitiru::render
