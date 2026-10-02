#pragma once

/// @file GamepadSlots.hpp
/// @brief XInput と SDL のパッドを InputSnapshot の 4 枠 (gamepads[4]) に並べ、1 人用の合成 (gamepad*) を作る。
/// @details 枠は XInput の player 番号をそのまま使い (抜き差ししても他の人の枠が動かない)、空いた枠へ
///          SDL のパッドを開いた順に詰める。Windows で XInput にも見えている Xbox 系の SDL パッドは、
///          呼び出し側が渡さない (同じ 1 台を 2 人分に数えないため)。

#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::input
{

inline constexpr int kGamepadSlots = static_cast<int>(sizeof(module::InputSnapshot::gamepads)
                                                      / sizeof(module::InputSnapshot::gamepads[0]));

/// @brief xinput[i] を枠 i に置き、残りの空き枠へ extra を前から詰める。
inline void assignGamepadSlots(const module::GamepadState (&xinput)[kGamepadSlots],
                               const module::GamepadState* extra, int extraCount,
                               module::GamepadState (&out)[kGamepadSlots]) noexcept
{
	for (int i = 0; i < kGamepadSlots; ++i) { out[i] = xinput[i].connected ? xinput[i] : module::GamepadState{}; }
	int next = 0;
	for (int i = 0; i < kGamepadSlots && next < extraCount; ++i)
	{
		if (out[i].connected) { continue; }
		while (next < extraCount && !extra[next].connected) { ++next; }
		if (next < extraCount) { out[i] = extra[next++]; }
	}
}

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
