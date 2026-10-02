#pragma once
// パッドで UI のフォーカスを動かす。十字キーと左スティックの倒し始めを上下左右のキー、A を Enter として RmlUi へ渡す
// (RmlUi は nav-* を持つ要素の間でフォーカスを動かし、tab-index: auto の要素は Enter で click する)。
// InputSnapshot の押下状態とスティックだけから作るので、replay でも同じ所へフォーカスが動く。

#include "RmlUiHost.hpp"

#include <RmlUi/Core/Input.h>

#include <cstdint>

namespace mitiru::ui_rml
{

class RmlPadNavigator
{
public:
	static constexpr std::uint32_t kDPadUp = 0x0001;
	static constexpr std::uint32_t kDPadDown = 0x0002;
	static constexpr std::uint32_t kDPadLeft = 0x0004;
	static constexpr std::uint32_t kDPadRight = 0x0008;
	static constexpr std::uint32_t kA = 0x1000;
	static constexpr float kStickThreshold = 0.5f;

	/// このフレームで押し始めたものだけを emit(Rml::Input::KeyIdentifier) で返す。押し続けても繰り返さない。
	template <class Emit>
	void translate(const UiPad& pad, Emit&& emit)
	{
		const std::uint32_t pressed = pad.buttonsDown & ~m_prevButtons;
		m_prevButtons = pad.buttonsDown;
		const Rml::Input::KeyIdentifier stick = stickKey(pad.stickX, pad.stickY);
		const bool stickTilted = stick != Rml::Input::KI_UNKNOWN && stick != m_prevStick;
		m_prevStick = stick;

		const struct { std::uint32_t bit; Rml::Input::KeyIdentifier key; } dpad[] = {
			{ kDPadUp, Rml::Input::KI_UP }, { kDPadDown, Rml::Input::KI_DOWN },
			{ kDPadLeft, Rml::Input::KI_LEFT }, { kDPadRight, Rml::Input::KI_RIGHT },
		};
		for (const auto& d : dpad)
		{
			if ((pressed & d.bit) != 0 || (stickTilted && stick == d.key)) { emit(d.key); }
		}
		if ((pressed & kA) != 0) { emit(Rml::Input::KI_RETURN); }
	}

private:
	static Rml::Input::KeyIdentifier stickKey(float x, float y) noexcept
	{
		const float ax = x < 0.0f ? -x : x;
		const float ay = y < 0.0f ? -y : y;
		if (ax < kStickThreshold && ay < kStickThreshold) { return Rml::Input::KI_UNKNOWN; }
		if (ax >= ay) { return x > 0.0f ? Rml::Input::KI_RIGHT : Rml::Input::KI_LEFT; }
		return y > 0.0f ? Rml::Input::KI_UP : Rml::Input::KI_DOWN;
	}

	std::uint32_t m_prevButtons = 0;
	Rml::Input::KeyIdentifier m_prevStick = Rml::Input::KI_UNKNOWN;
};

} // namespace mitiru::ui_rml
