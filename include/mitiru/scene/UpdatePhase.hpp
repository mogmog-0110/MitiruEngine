#pragma once

/// @file UpdatePhase.hpp
/// @brief システム更新順序のフェーズ定義
///
/// `SystemRunner` (scene) と `SystemScheduler` (ecs) の両方で共有する。
/// 定義を 1 か所に集約し、フェーズの意味がモジュール間でずれないようにする。

#include <cstdint>

namespace mitiru::scene
{

/// @brief システム更新フェーズ
/// @details 値の大小がそのまま実行順序（小さいほど先）になる。
enum class UpdatePhase : std::uint8_t
{
	Input,
	PreSim,
	Sim,
	PostSim,
	Anim,
	PostAnim,
	Camera,
	Final,
};

} // namespace mitiru::scene
