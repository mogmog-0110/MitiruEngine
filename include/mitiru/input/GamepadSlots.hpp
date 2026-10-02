#pragma once

/// @file GamepadSlots.hpp
/// @brief InputSnapshot のパッド枠の数と、全台を 1 人用の合成 (gamepad*) にまとめる処理。
/// @details どのパッドをどの枠に置くかは GamepadSlotTable.hpp が決める。

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::input
{

inline constexpr int kGamepadSlots = static_cast<int>(sizeof(module::InputSnapshot::gamepads)
                                                      / sizeof(module::InputSnapshot::gamepads[0]));

/// @brief 全台を 1 台分にまとめて InputSnapshot の gamepad* に書く。ボタンは OR、軸は最初に繋がっている台のもの。
inline void mergeGamepads(const module::GamepadState (&pads)[kGamepadSlots], module::InputSnapshot& snap) noexcept
{
	snap.gamepadConnected = 0;
	snap.gamepadButtonsDown = 0;
	snap.gamepadButtonsJustPressed = 0;
	snap.gamepadButtonsJustReleased = 0;
	for (float& a : snap.gamepadAxes) { a = 0.0f; }
	for (const module::GamepadState& p : pads)
	{
		if (!p.connected) { continue; }
		if (snap.gamepadConnected == 0)
		{
			for (int a = 0; a < module::gamepad::AxisCount; ++a) { snap.gamepadAxes[a] = p.axes[a]; }
		}
		snap.gamepadConnected = 1;
		snap.gamepadButtonsDown         |= p.buttonsDown;
		snap.gamepadButtonsJustPressed  |= p.buttonsJustPressed;
		snap.gamepadButtonsJustReleased |= p.buttonsJustReleased;
	}
}

}  // namespace mitiru::input
