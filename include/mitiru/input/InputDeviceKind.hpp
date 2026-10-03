#pragma once

/// @file InputDeviceKind.hpp
/// @brief 今使っている機器 (キーボードとマウスかパッドか) とパッドの書き方の系統。ボタンの絵を選ぶ時に使う。
/// @details host は InputSnapshot::inputDevice / padFamily (ABI v50) にこの値を入れる。ゲーム DLL は
///          input::glyphFor (GlyphLookup.hpp) にそのまま渡して、自分で描く HUD の絵柄の名前を引ける。

#include <cmath>
#include <cstdint>

#include <mitiru/input/GamepadFeatures.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::input
{

enum class InputDevice : std::uint8_t { KeyboardMouse, Gamepad };

/// @brief 同じボタンのビットを何と書くか (A ビットは Xbox で A、PlayStation で ×、Nintendo で B)
enum class PadFamily : std::uint8_t { Xbox, PlayStation, Nintendo };

[[nodiscard]] constexpr PadFamily padFamilyOf(PadKind kind) noexcept
{
	switch (kind)
	{
	case PadKind::PS3: case PadKind::PS4: case PadKind::PS5:
		return PadFamily::PlayStation;
	case PadKind::SwitchPro: case PadKind::JoyConLeft: case PadKind::JoyConRight: case PadKind::JoyConPair:
	case PadKind::GameCube:
		return PadFamily::Nintendo;
	default:
		return PadFamily::Xbox;
	}
}

/// @brief 最後に触った機器。片方だけを触ったフレームで切り替え、両方か、どちらも触っていなければ prev のまま。
[[nodiscard]] inline InputDevice lastUsedDevice(const module::InputSnapshot& snap, InputDevice prev) noexcept
{
	bool pad = snap.gamepadButtonsDown != 0;
	for (const float a : snap.gamepadAxes) { pad = pad || std::fabs(a) > 0.5f; }
	bool kbm = snap.mouseDeltaX != 0.0f || snap.mouseDeltaY != 0.0f;
	for (const std::uint8_t b : snap.mouseButtonsDown) { kbm = kbm || b != 0; }
	for (const std::uint8_t k : snap.keysDown) { kbm = kbm || k != 0; }
	if (pad == kbm) { return prev; }
	return pad ? InputDevice::Gamepad : InputDevice::KeyboardMouse;
}

/// @brief 繋がっているパッドのうち最初の台の書き方。繋がっていなければ Xbox
[[nodiscard]] inline PadFamily connectedPadFamily(const module::InputSnapshot& snap) noexcept
{
	for (int i = 0; i < 4; ++i)
	{
		if (snap.gamepads[i].connected != 0) { return padFamilyOf(static_cast<PadKind>(snap.gamepadsExt[i].kind)); }
	}
	return PadFamily::Xbox;
}

}  // namespace mitiru::input
