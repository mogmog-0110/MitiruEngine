#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <mitiru/debug/DebugMenuModel.hpp>
#include <mitiru/module/ModuleApi.hpp>

namespace mitiru::debug
{

inline constexpr int kKeyBackspace = 0x08;
inline constexpr int kKeyEnter = 0x0D;
inline constexpr int kKeyShift = 0x10;
inline constexpr int kKeyEscape = 0x1B;
inline constexpr int kKeyPageUp = 0x21;
inline constexpr int kKeyPageDown = 0x22;
inline constexpr int kKeyEnd = 0x23;
inline constexpr int kKeyHome = 0x24;
inline constexpr int kKeyLeft = 0x25;
inline constexpr int kKeyUp = 0x26;
inline constexpr int kKeyRight = 0x27;
inline constexpr int kKeyDown = 0x28;
inline constexpr int kKeyF1 = 0x70;
inline constexpr int kKeyOemMinus = 0xBD;
inline constexpr int kKeyOemPeriod = 0xBE;
inline constexpr float kStickThreshold = 0.5f;

class DebugMenuInputReader
{
public:
	[[nodiscard]] MenuInput read(const module::InputSnapshot& snap, float dt)
	{
		MenuInput result;
		const bool stickUp = snap.gamepadAxes[module::gamepad::LeftStickY] > kStickThreshold;
		const bool stickDown = snap.gamepadAxes[module::gamepad::LeftStickY] < -kStickThreshold;
		const bool stickLeft = snap.gamepadAxes[module::gamepad::LeftStickX] < -kStickThreshold;
		const bool stickRight = snap.gamepadAxes[module::gamepad::LeftStickX] > kStickThreshold;

		result.upPressed = keyPressed(snap, kKeyUp) || padPressed(snap, module::gamepad::DPadUp) ||
		                   (stickUp && !m_stickUp);
		result.downPressed = keyPressed(snap, kKeyDown) || padPressed(snap, module::gamepad::DPadDown) ||
		                     (stickDown && !m_stickDown);
		result.leftPressed = keyPressed(snap, kKeyLeft) || padPressed(snap, module::gamepad::DPadLeft) ||
		                     (stickLeft && !m_stickLeft);
		result.rightPressed = keyPressed(snap, kKeyRight) || padPressed(snap, module::gamepad::DPadRight) ||
		                      (stickRight && !m_stickRight);
		result.upHeld = keyDown(snap, kKeyUp) || padDown(snap, module::gamepad::DPadUp) || stickUp;
		result.downHeld = keyDown(snap, kKeyDown) || padDown(snap, module::gamepad::DPadDown) || stickDown;
		result.leftHeld = keyDown(snap, kKeyLeft) || padDown(snap, module::gamepad::DPadLeft) || stickLeft;
		result.rightHeld = keyDown(snap, kKeyRight) || padDown(snap, module::gamepad::DPadRight) || stickRight;
		result.pagePrev = keyPressed(snap, kKeyPageUp) || padPressed(snap, module::gamepad::LB);
		result.pageNext = keyPressed(snap, kKeyPageDown) || padPressed(snap, module::gamepad::RB);
		result.home = keyPressed(snap, kKeyHome);
		result.end = keyPressed(snap, kKeyEnd);
		result.confirm = keyPressed(snap, kKeyEnter) || padPressed(snap, module::gamepad::A);
		result.cancel = keyPressed(snap, kKeyEscape) ||
		                padPressed(snap, module::gamepad::B | module::gamepad::Back);
		result.backspace = keyPressed(snap, kKeyBackspace);
		result.wheel = snap.mouseWheel;
		result.dt = dt;
		result.typed = typedText(snap);

		m_stickUp = stickUp;
		m_stickDown = stickDown;
		m_stickLeft = stickLeft;
		m_stickRight = stickRight;
		return result;
	}

	[[nodiscard]] static bool openRequested(const module::InputSnapshot& snap) noexcept
	{
		const std::uint32_t both = module::gamepad::LS | module::gamepad::RS;
		const bool padCombo = (snap.gamepadButtonsDown & both) == both && (snap.gamepadButtonsJustPressed & both) != 0;
		return keyPressed(snap, kKeyF1) || padCombo;
	}

	void reset() noexcept
	{
		m_stickUp = false;
		m_stickDown = false;
		m_stickLeft = false;
		m_stickRight = false;
	}

private:
	[[nodiscard]] static bool keyPressed(const module::InputSnapshot& snap, int key) noexcept
	{
		return snap.keysJustPressed[key] != 0;
	}

	[[nodiscard]] static bool keyDown(const module::InputSnapshot& snap, int key) noexcept
	{
		return snap.keysDown[key] != 0;
	}

	[[nodiscard]] static bool padPressed(const module::InputSnapshot& snap, std::uint32_t buttons) noexcept
	{
		return (snap.gamepadButtonsJustPressed & buttons) != 0;
	}

	[[nodiscard]] static bool padDown(const module::InputSnapshot& snap, std::uint32_t buttons) noexcept
	{
		return (snap.gamepadButtonsDown & buttons) != 0;
	}

	[[nodiscard]] static std::string typedText(const module::InputSnapshot& snap)
	{
		std::string result;
		for (int key = 0x30; key <= 0x39; ++key)
		{
			if (keyPressed(snap, key)) { result.push_back(static_cast<char>(key)); }
		}
		for (int key = 0x41; key <= 0x5A; ++key)
		{
			if (keyPressed(snap, key)) { result.push_back(static_cast<char>(key - 'A' + 'a')); }
		}
		if (keyPressed(snap, kKeyOemMinus))
		{
			result.push_back(keyDown(snap, kKeyShift) ? '_' : '-');
		}
		if (keyPressed(snap, kKeyOemPeriod))
		{
			result.push_back('.');
		}
		return result;
	}

	bool m_stickUp = false;
	bool m_stickDown = false;
	bool m_stickLeft = false;
	bool m_stickRight = false;
};

inline void hideInputFromGame(module::InputSnapshot& snap) noexcept
{
	std::fill(std::begin(snap.keysDown), std::end(snap.keysDown), 0);
	std::fill(std::begin(snap.keysJustPressed), std::end(snap.keysJustPressed), 0);
	std::fill(std::begin(snap.keysJustReleased), std::end(snap.keysJustReleased), 0);
	std::fill(std::begin(snap.mouseButtonsDown), std::end(snap.mouseButtonsDown), 0);
	std::fill(std::begin(snap.mouseButtonsJustPressed), std::end(snap.mouseButtonsJustPressed), 0);
	std::fill(std::begin(snap.mouseButtonsJustReleased), std::end(snap.mouseButtonsJustReleased), 0);
	std::fill(std::begin(snap.mouseXButtonsDown), std::end(snap.mouseXButtonsDown), 0);
	std::fill(std::begin(snap.mouseXButtonsJustPressed), std::end(snap.mouseXButtonsJustPressed), 0);
	std::fill(std::begin(snap.mouseXButtonsJustReleased), std::end(snap.mouseXButtonsJustReleased), 0);
	snap.mouseWheel = 0.0f;
	snap.mouseWheelH = 0.0f;
	snap.mouseDeltaX = 0.0f;
	snap.mouseDeltaY = 0.0f;
	snap.gamepadButtonsDown = 0;
	snap.gamepadButtonsJustPressed = 0;
	snap.gamepadButtonsJustReleased = 0;
	std::fill(std::begin(snap.gamepadAxes), std::end(snap.gamepadAxes), 0.0f);
	for (module::GamepadState& pad : snap.gamepads)
	{
		pad.buttonsDown = 0;
		pad.buttonsJustPressed = 0;
		pad.buttonsJustReleased = 0;
		std::fill(std::begin(pad.axes), std::end(pad.axes), 0.0f);
	}
	snap.actionsDown = 0;
	snap.actionsPressed = 0;
	snap.actionsReleased = 0;
	std::fill(std::begin(snap.actionsDownByPlayer), std::end(snap.actionsDownByPlayer), 0);
	std::fill(std::begin(snap.actionsPressedByPlayer), std::end(snap.actionsPressedByPlayer), 0);
	std::fill(std::begin(snap.actionsReleasedByPlayer), std::end(snap.actionsReleasedByPlayer), 0);
	snap.textInputLen = 0;
	snap.textInput[0] = '\0';
	snap.imeCompositionLen = 0;
	snap.imeCursor = 0;
	snap.imeComposition[0] = '\0';
}

class HeldInputMask
{
public:
	void capture(const module::InputSnapshot& snap) noexcept
	{
		for (std::size_t i = 0; i < m_keys.size(); ++i)
		{
			m_keys[i] = snap.keysDown[i] != 0;
		}
		m_combinedButtons = snap.gamepadButtonsDown;
		for (std::size_t i = 0; i < m_padButtons.size(); ++i)
		{
			m_padButtons[i] = snap.gamepads[i].buttonsDown;
		}
	}

	void apply(module::InputSnapshot& snap) noexcept
	{
		for (std::size_t i = 0; i < m_keys.size(); ++i)
		{
			if (!m_keys[i]) { continue; }
			if (snap.keysDown[i] != 0) { snap.keysDown[i] = 0; }
			else { m_keys[i] = false; }
		}
		m_combinedButtons &= snap.gamepadButtonsDown;
		snap.gamepadButtonsDown &= ~m_combinedButtons;
		for (std::size_t i = 0; i < m_padButtons.size(); ++i)
		{
			m_padButtons[i] &= snap.gamepads[i].buttonsDown;
			snap.gamepads[i].buttonsDown &= ~m_padButtons[i];
		}
	}

	[[nodiscard]] bool any() const noexcept
	{
		if (m_combinedButtons != 0) { return true; }
		for (bool key : m_keys) { if (key) { return true; } }
		for (std::uint32_t buttons : m_padButtons) { if (buttons != 0) { return true; } }
		return false;
	}

private:
	std::array<bool, 256> m_keys{};
	std::uint32_t m_combinedButtons = 0;
	std::array<std::uint32_t, 4> m_padButtons{};
};

}  // namespace mitiru::debug
