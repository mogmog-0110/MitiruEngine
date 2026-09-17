#pragma once

/// @file FixedStepPlan.hpp
/// @brief 固定タイムステップの本数計画 (timeScale 早回し / スパイラル防止上限)。
/// timeScale は dt ではなく「1 フレームで回すステップ数」を増減させる。dt は常に
/// kFixedDt に固定されるので、早回しでも sim は実時間と bit 一致し、キャプチャも決定的。

namespace mitiru::detail
{

/// 蓄積時間から今フレームで回す固定ステップ数を返す。floor(accumulator / fixedDt) を
/// cap で頭打ち。accumulator 自体は変更しない (呼び出し側が消化ぶんを減算する)。
inline int planFixedSteps(float accumulator, float fixedDt, int cap) noexcept
{
	if (fixedDt <= 0.0f || cap <= 0) { return 0; }
	int n = 0;
	while (accumulator >= fixedDt && n < cap) { accumulator -= fixedDt; ++n; }
	return n;
}

/// 上限で頭打ちになった後も残っている「消化できなかった」ステップ数。Godot の
/// max_physics_steps_per_frame と同じく、これが正ならゲームは実時間より遅く進んでいる
/// (スローモーション)。非決定論モードではこのぶんの時間を捨てて追いかけるのをやめ、
/// スパイラル (毎フレーム上限まで回して更に遅れる) を断つ。
inline int countDroppedFixedSteps(float accumulatorAfterPlan, float fixedDt) noexcept
{
	if (fixedDt <= 0.0f || accumulatorAfterPlan < fixedDt) { return 0; }
	return static_cast<int>(accumulatorAfterPlan / fixedDt);
}

/// 決定論モードのステップ上限。timeScale ぶん (+余裕) を 1 フレームで消化できる高さにする。
/// 非決定論では壁時計遅延に対するスパイラル防止として baseCap をそのまま使う。
inline int deterministicStepCap(int baseCap, float timeScale) noexcept
{
	const int need = static_cast<int>(timeScale) + 2;
	return need > baseCap ? need : baseCap;
}

}  // namespace mitiru::detail
