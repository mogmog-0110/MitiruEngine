#pragma once

/// @file CameraPriority.hpp
/// @brief カメラの勝者の選び方と blend の曲線。カットシーン (narrative/Sequence) とゲーム中の仮想カメラ
///        (camera/VirtualCameraStack) が同じ規則で選ぶように、ここに 1 つだけ置く。
/// @details 同じ priority は添字の小さい方が勝つ (カットシーンの区間表がこれまでそう決めていた。変えると既存の台本の映りが変わる)。

#include <cstdint>

namespace mitiru::camera
{

enum class BlendCurve : std::uint8_t
{
	Cut = 0,      ///< 混ぜずに切り替える
	Linear = 1,
	Smooth = 2,   ///< smoothstep。カットシーンの混ぜはこれ
	EaseIn = 3,
	EaseOut = 4,
};

[[nodiscard]] constexpr float smoothstep01(float x) noexcept
{
	const float t = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
	return t * t * (3.0f - 2.0f * t);
}

/// @brief 経過の割合 x (0..1) を混ぜる重みにする。Cut は 1 (混ぜない)
[[nodiscard]] constexpr float blendWeight(BlendCurve curve, float x) noexcept
{
	const float t = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
	switch (curve)
	{
	case BlendCurve::Linear:  return t;
	case BlendCurve::Smooth:  return smoothstep01(t);
	case BlendCurve::EaseIn:  return t * t;
	case BlendCurve::EaseOut: return t * (2.0f - t);
	case BlendCurve::Cut:     break;
	}
	return 1.0f;
}

/// @brief 0..count-1 のうち isActive が真で priority が一番高いものの添字。無ければ -1
template <class IsActive, class PriorityOf>
[[nodiscard]] constexpr int pickWinner(int count, IsActive&& isActive, PriorityOf&& priorityOf)
{
	int winner = -1;
	for (int i = 0; i < count; ++i)
	{
		if (isActive(i) && (winner < 0 || priorityOf(i) > priorityOf(winner))) { winner = i; }
	}
	return winner;
}

}  // namespace mitiru::camera
