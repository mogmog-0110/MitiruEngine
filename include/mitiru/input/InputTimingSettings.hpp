#pragma once

/// @file InputTimingSettings.hpp
/// @brief 長押し、ダブルタップ、リピートの秒数を持つ利用者向け設定の宣言。
/// @details 設定画面と host 側の UI がこの値を使う。InputTriggers.hpp の部品は渡された秒数で動き、この型には依存しない。

namespace mitiru::input
{

struct InputTimingSettings
{
	float holdSec = 0.4f;             ///< 長押しとみなす秒数
	float tapWindowSec = 0.25f;       ///< ダブルタップで 2 回目を待つ秒数
	float repeatDelaySec = 0.4f;      ///< 押し続けてから最初のリピートまで
	float repeatIntervalSec = 0.08f;  ///< リピートの間隔

	[[nodiscard]] bool operator==(const InputTimingSettings&) const noexcept = default;
};

}  // namespace mitiru::input
