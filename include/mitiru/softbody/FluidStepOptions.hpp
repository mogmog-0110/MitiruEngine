#pragma once

/// @file FluidStepOptions.hpp
/// @brief 流れる体の解き方の選択。全部 0 (既定) なら従来の動きと 1 bit も変わらない。
/// @details FluidBodyState と FluidParams は大きさも offset も動かさず、呼び手が持つ別の型で FluidStepInput::options に渡す。使い方は docs/SOFTBODY_FLUID.md。

#include <cstdint>

namespace mitiru::softbody
{

struct FluidStepOptions
{
	std::uint8_t interpolateContainer = 0;        ///< 1 なら、コンテナを前の tick の置き方から今の tick の置き方へ substep ごとに補間する
	std::uint8_t relativeContainerDamping = 0;    ///< 1 なら、コンテナの減衰を coreVelocity に対する相対速度に掛ける
	std::uint8_t tensileScaledBySubstep = 0;      ///< 1 なら、tensileK を 240 Hz、反復 3 回のときの値として substep の長さと反復の数に合わせて替える
	std::uint8_t pad = 0;
};

inline constexpr float kFluidTensileRefSubstepHz = 240.0f;
inline constexpr float kFluidTensileRefIterations = 3.0f;
/// 60 Hz、反復 1 回では倍率が 48 になり、1 回の補正がパーティクルの間隔に近づいて体が崩れるので止める
inline constexpr float kFluidTensileMaxScale = 16.0f;

} // namespace mitiru::softbody
