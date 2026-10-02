#pragma once

/// @file LayerDt.hpp
/// @brief layer 別 dt (`InputSnapshot::dtByLayer[8]`) を組み立てる純関数 (2-1)。
/// Engine::buildModuleInputSnapshot から呼ぶ計算部分だけを切り出してあり、Engine 本体を
/// 持ち込まずに単体テストできる (private メンバへの test hook を Engine.hpp に増やさずに済む)。

#include <cstdint>

namespace mitiru::module::detail
{

/// pause / hitStop の layer 別 dt gating。`alwaysLayersMask` の bit i が立っている layer は
/// pause 中でも通常どおり dt を受け取る (Godot `PROCESS_MODE_WHEN_PAUSED` 相当、game 側の
/// `MITIRU_PAUSE_ALWAYS_LAYERS(mask)` 宣言がこの mask を決める)。hitStop の gating は既存どおり
/// layer ごとの `layerFrozenByHitStop` に従い、mask の影響を受けない (別軸のため)。
inline void computeLayerDt(float dt, bool pausedGate, bool hitStop,
	const float (&layerTimeScale)[8], const bool (&layerFrozenByHitStop)[8],
	std::uint8_t alwaysLayersMask, float (&outDtByLayer)[8]) noexcept
{
	for (int i = 0; i < 8; ++i)
	{
		const bool alwaysOn = ((alwaysLayersMask >> i) & 1u) != 0u;
		float layerDt = (pausedGate && !alwaysOn) ? 0.0f : dt * layerTimeScale[i];
		if (hitStop && layerFrozenByHitStop[i]) { layerDt = 0.0f; }
		outDtByLayer[i] = layerDt;
	}
}

/// pause の種類 (`InputSnapshot::paused` の値、0 = 動作中) から「pause 中も dt を通す layer mask」を選ぶ。
/// 範囲外の種類は ingame (1) と同じ扱いにし、気づかないうちに全 layer が止まることがないようにする。
[[nodiscard]] inline std::uint8_t pauseLayersMaskFor(std::uint8_t pauseKind,
	const std::uint8_t (&maskByKind)[4]) noexcept
{
	const std::uint8_t k = (pauseKind >= 1 && pauseKind <= 3) ? pauseKind : std::uint8_t{1};
	return maskByKind[k];
}

}  // namespace mitiru::module::detail
