#pragma once

/// @file ModuleInputDevices.hpp
/// @brief マウスとパッドの実機の状態を InputSnapshot へ写す (Engine::buildModuleInputSnapshot の一部)。

#include <cstdint>

#include <mitiru/input/GamepadSlots.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/input/SdlGamepadInput.hpp>
#include <mitiru/module/ModuleApi.hpp>

#ifdef _WIN32
#include <mitiru/input/GamepadInput.hpp>
#endif

namespace mitiru::detail
{

/// @brief Win32 の WHEEL_DELTA。InputState はこの単位で積む。
inline constexpr float kWheelUnitsPerNotch = 120.0f;

/// @brief マウスの位置・移動量・5 ボタン・縦横ホイールを写す。
inline void fillSnapshotMouse(const InputState& in, module::InputSnapshot& snap) noexcept
{
	const auto [mx, my] = in.mousePosition();
	snap.mouseX = mx;
	snap.mouseY = my;
	// カーソルロック中は platform 蓄積の生デルタが返る (v23)
	const auto [mdx, mdy] = in.mouseDelta();
	snap.mouseDeltaX = mdx;
	snap.mouseDeltaY = mdy;
	for (int i = 0; i < InputState::MAX_MOUSE_BUTTONS; ++i)
	{
		const auto btn = static_cast<MouseButton>(i);
		const std::uint8_t down     = in.isMouseButtonDown(btn) ? 1u : 0u;
		const std::uint8_t pressed  = in.isMouseButtonJustPressed(btn) ? 1u : 0u;
		const std::uint8_t released = in.isMouseButtonJustReleased(btn) ? 1u : 0u;
		if (i < 3)
		{
			snap.mouseButtonsDown[i] = down;
			snap.mouseButtonsJustPressed[i] = pressed;
			snap.mouseButtonsJustReleased[i] = released;
		}
		else
		{
			snap.mouseXButtonsDown[i - 3] = down;
			snap.mouseXButtonsJustPressed[i - 3] = pressed;
			snap.mouseXButtonsJustReleased[i - 3] = released;
		}
	}
	snap.mouseWheel  = in.mouseWheelDelta() / kWheelUnitsPerNotch;
	snap.mouseWheelH = in.mouseWheelHDelta() / kWheelUnitsPerNotch;
}

#ifdef _WIN32
/// @brief XInput の player 番号 1 つ分を読む。未接続なら全 0。
inline module::GamepadState readXInputPad(const GamepadInput& pads, int player) noexcept
{
	module::GamepadState s{};
	if (!pads.isConnected(player)) { return s; }
	s.connected = 1;
	static constexpr GamepadButton kButtons[] = {
		GamepadButton::DPadUp, GamepadButton::DPadDown, GamepadButton::DPadLeft,
		GamepadButton::DPadRight, GamepadButton::Start, GamepadButton::Back,
		GamepadButton::LS, GamepadButton::RS, GamepadButton::LB, GamepadButton::RB,
		GamepadButton::A, GamepadButton::B, GamepadButton::X, GamepadButton::Y };
	for (const GamepadButton b : kButtons)
	{
		const auto bit = static_cast<std::uint32_t>(b);
		if (pads.isButtonDown(player, b))     { s.buttonsDown         |= bit; }
		if (pads.isButtonPressed(player, b))  { s.buttonsJustPressed  |= bit; }
		if (pads.isButtonReleased(player, b)) { s.buttonsJustReleased |= bit; }
	}
	for (int a = 0; a < module::gamepad::AxisCount; ++a)
	{
		s.axes[a] = pads.getAxis(player, static_cast<GamepadAxis>(a));
	}
	return s;
}
#endif

/// @brief SDL で開いた i 台目を読む。
inline module::GamepadState readSdlPad(const input::SdlGamepadInput& pads, int i) noexcept
{
	module::GamepadState s{};
	s.connected = 1;
	s.buttonsDown = pads.buttonsDown(i);
	s.buttonsJustPressed = pads.buttonsJustPressed(i);
	s.buttonsJustReleased = pads.buttonsJustReleased(i);
	for (int a = 0; a < module::gamepad::AxisCount; ++a) { s.axes[a] = pads.axis(i, a); }
	return s;
}

/// @brief XInput の 4 枠に SDL のパッドを足して gamepads[4] と 1 人用の合成を書く。
inline void fillSnapshotGamepads(const module::GamepadState (&xinput)[input::kGamepadSlots],
                                 const input::SdlGamepadInput& sdl, module::InputSnapshot& snap) noexcept
{
	module::GamepadState extra[input::SdlGamepadInput::kMaxPads] = {};
	int n = 0;
	for (int i = 0; i < sdl.count(); ++i)
	{
#ifdef _WIN32
		if (sdl.isXInputDevice(i)) { continue; }  // 同じ 1 台を XInput 側でも読んでいる
#endif
		extra[n++] = readSdlPad(sdl, i);
	}
	input::assignGamepadSlots(xinput, extra, n, snap.gamepads);
	input::mergeGamepads(snap.gamepads, snap);
}

}  // namespace mitiru::detail
