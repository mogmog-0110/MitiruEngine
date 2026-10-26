#pragma once

/// @file Light2D.hpp
/// @brief 2D ライトの POD (ABI v62、ADR 0099)。Screen::light2D と Canvas::light2D が運ぶ。
/// @details 点・スポット・全体光を 1 つの型で持つ。coneDeg が 0 なら全周の点光源、0 より大きければ directionDeg を向く円錐。
///          光は法線マップ (sprite の id に _n を付けた画像、ADR 0103) の N・L と、height の高さから決まる。
///          呼んだ後に描いた物へ、そのフレームの終わりまで当たる。式は render/Light2DMath.hpp。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru::render
{

inline constexpr int kMaxLights2D = 16;   ///< 1 フレームに頼めるライトの数 (Screen の持つ表の大きさ)

/// @brief Light2DPod::flags の bit。Canvas の運ぶ flags は 16 bit なので、ここも 16 bit に収める
inline constexpr std::uint32_t kLight2DShadow = 1u << 0;   ///< 遮蔽物の影を落とす (R6 の段階 2。立てても段階 1 では効かない)

struct Light2DPod
{
	float x = 0.0f;                          ///< 論理座標
	float y = 0.0f;
	float height = 0.5f;                     ///< 面からの高さ (法線マップの N・L に効く。論理座標と同じ単位)
	float radius = 128.0f;                   ///< 光が届く半径 (論理座標)
	float color[3] = {1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float directionDeg = 0.0f;               ///< 円錐の向き (度、+x が 0、画面で時計回りが正)
	float coneDeg = 0.0f;                    ///< 円錐の外側の半角 (度)。0 = 全周の点光源
	float falloff = 1.0f;                    ///< 減衰の指数 (1 = 直線、2 = 二乗)
	std::uint32_t flags = 0;                 ///< kLight2D*
	std::uint32_t reserved[2] = {0, 0};      ///< 必ず 0
};

static_assert(sizeof(Light2DPod) == 56 && offsetof(Light2DPod, color) == 16 && offsetof(Light2DPod, intensity) == 28 &&
              offsetof(Light2DPod, flags) == 44, "Light2DPod wire size 固定 (v62)");
static_assert(std::is_trivially_copyable_v<Light2DPod> && std::is_standard_layout_v<Light2DPod>);

}  // namespace mitiru::render
