#pragma once

/// @file ModuleInputDevices.hpp
/// @brief マウスとパッドの実機の状態を InputSnapshot へ写す (Engine::buildModuleInputSnapshot の一部)。

#include <cstdint>

#include <mitiru/input/GamepadSlots.hpp>
#include <mitiru/input/InputState.hpp>
#include <mitiru/input/SdlGamepadInput.hpp>
#include <mitiru/module/ModuleApi.hpp>

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

/// @brief 枠 slot のパッドを読む。空いた枠は全 0。
inline module::GamepadState readPad(const input::SdlGamepadInput& pads, int slot) noexcept
{
	module::GamepadState s{};
	if (!pads.connected(slot)) { return s; }
	s.connected = 1;
	s.buttonsDown = pads.buttonsDown(slot);
	s.buttonsJustPressed = pads.buttonsJustPressed(slot);
	s.buttonsJustReleased = pads.buttonsJustReleased(slot);
	for (int a = 0; a < module::gamepad::AxisCount; ++a) { s.axes[a] = pads.axis(slot, a); }
	return s;
}

/// @brief 枠 slot のパッドの拡張を読む (v48)。押し込みの瞬間は前フレームの snapshot の値と比べて出す。
inline module::GamepadExt readPadExt(const input::SdlGamepadInput& pads, int slot,
                                     const module::GamepadExt& prev) noexcept
{
	module::GamepadExt e{};
	if (!pads.connected(slot)) { return e; }
	const input::PadInfo info = pads.info(slot);
	e.kind = static_cast<std::uint8_t>(info.kind);
	e.power = static_cast<std::uint8_t>(info.power);
	e.battery = info.batteryPercent;
	e.caps = static_cast<std::uint8_t>((info.hasGyro ? module::kPadCapGyro : 0) | (info.hasAccel ? module::kPadCapAccel : 0)
		| (info.touchpadCount > 0 ? module::kPadCapTouchpad : 0) | (info.hasRgbLed ? module::kPadCapLightbar : 0)
		| (info.hasRumble ? module::kPadCapRumble : 0) | (info.hasTriggerRumble ? module::kPadCapTriggerRumble : 0)
		| (info.hasAdaptiveTriggers ? module::kPadCapAdaptiveTriggers : 0));
	e.extraDown = pads.touchpadPressed(slot) ? module::kPadExtraTouchpad : 0;
	e.extraPressed = static_cast<std::uint8_t>(e.extraDown & ~prev.extraDown);
	e.extraReleased = static_cast<std::uint8_t>(prev.extraDown & ~e.extraDown);
	const input::PadMotion m = pads.motion(slot);
	e.motionActive = (m.hasGyro || m.hasAccel) ? 1 : 0;
	for (int i = 0; i < 3; ++i) { e.gyro[i] = m.gyro[static_cast<std::size_t>(i)]; e.accel[i] = m.accel[static_cast<std::size_t>(i)]; }
	for (int f = 0; f < 2; ++f)
	{
		const input::PadTouchFinger t = pads.touchFinger(slot, 0, f);
		e.touch[f] = module::PadTouch{static_cast<std::uint8_t>(t.down ? 1 : 0), {}, t.x, t.y, t.pressure};
	}
	return e;
}

/// @brief gamepads[4] を枠どおりに書き、1 人用の合成も書く。
inline void fillSnapshotGamepads(const input::SdlGamepadInput& pads, module::InputSnapshot& snap) noexcept
{
	for (int i = 0; i < input::kGamepadSlots; ++i)
	{
		snap.gamepads[i] = readPad(pads, i);
		snap.gamepadsExt[i] = readPadExt(pads, i, snap.gamepadsExt[i]);
	}
	input::mergeGamepads(snap.gamepads, snap);
}

}  // namespace mitiru::detail
