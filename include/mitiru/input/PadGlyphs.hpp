#pragma once

/// @file PadGlyphs.hpp
/// @brief 割り当てを画面に出す時の、入力ごとの表示名と絵柄の名前 (パッドは機種ごと)。
/// @details パッドのボタンのビットは位置で決まる (A ビット = 下のボタン)。同じビットでも、Xbox は "A"、
///          PlayStation は "×"、Nintendo は "B" と書かれている。機種は SdlGamepadInput::info(slot).kind で分かる。
///          glyph は UI が絵を選ぶための名前 ("xbox_a" / "ps_cross" / "nintendo_b" / "key_space" など)。

#include <cstdint>
#include <string>
#include <string_view>

#include <mitiru/input/ActionMap.hpp>
#include <mitiru/input/GamepadFeatures.hpp>
#include <mitiru/input/InputDeviceKind.hpp>

namespace mitiru::input
{

struct InputGlyph
{
	std::string label;   ///< 画面に出す文字 (UTF-8)
	std::string glyph;   ///< 絵柄の名前
};

struct PadButtonGlyph
{
	std::uint32_t bit;
	std::string_view xbox, ps, nintendo;          ///< 表示名
	std::string_view xboxId, psId, nintendoId;    ///< 絵柄の名前
};

inline constexpr PadButtonGlyph kPadButtonGlyphs[] = {
	{ module::gamepad::A, "A", "×", "B", "xbox_a", "ps_cross", "nintendo_b" },
	{ module::gamepad::B, "B", "○", "A", "xbox_b", "ps_circle", "nintendo_a" },
	{ module::gamepad::X, "X", "□", "Y", "xbox_x", "ps_square", "nintendo_y" },
	{ module::gamepad::Y, "Y", "△", "X", "xbox_y", "ps_triangle", "nintendo_x" },
	{ module::gamepad::LB, "LB", "L1", "L", "xbox_lb", "ps_l1", "nintendo_l" },
	{ module::gamepad::RB, "RB", "R1", "R", "xbox_rb", "ps_r1", "nintendo_r" },
	{ module::gamepad::Start, "Menu", "Options", "+", "xbox_menu", "ps_options", "nintendo_plus" },
	{ module::gamepad::Back, "View", "Create", "-", "xbox_view", "ps_create", "nintendo_minus" },
	{ module::gamepad::LS, "LS", "L3", "L Stick", "xbox_ls", "ps_l3", "nintendo_ls" },
	{ module::gamepad::RS, "RS", "R3", "R Stick", "xbox_rs", "ps_r3", "nintendo_rs" },
	{ module::gamepad::DPadUp, "↑", "↑", "↑", "dpad_up", "dpad_up", "dpad_up" },
	{ module::gamepad::DPadDown, "↓", "↓", "↓", "dpad_down", "dpad_down", "dpad_down" },
	{ module::gamepad::DPadLeft, "←", "←", "←", "dpad_left", "dpad_left", "dpad_left" },
	{ module::gamepad::DPadRight, "→", "→", "→", "dpad_right", "dpad_right", "dpad_right" },
};

[[nodiscard]] inline std::string lowerAscii(std::string s)
{
	for (char& c : s) { if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); } }
	return s;
}

/// @brief 入力 1 つの表示名と絵柄の名前。パッドの入力は f の書き方になる。
[[nodiscard]] inline InputGlyph inputGlyph(const InputSource& s, PadFamily f)
{
	switch (s.kind)
	{
	case SourceKind::Key:
	{
		const std::string name = keyNameFromVk(s.code);
		return { name, "key_" + lowerAscii(name) };
	}
	case SourceKind::Mouse:
	{
		const std::string name(nameByCode(kMouseButtonNames, s.code));
		return { "Mouse " + name, "mouse_" + lowerAscii(name) };
	}
	case SourceKind::PadButton:
		for (const PadButtonGlyph& g : kPadButtonGlyphs)
		{
			if (g.bit != s.code) { continue; }
			if (f == PadFamily::PlayStation) { return { std::string(g.ps), std::string(g.psId) }; }
			if (f == PadFamily::Nintendo) { return { std::string(g.nintendo), std::string(g.nintendoId) }; }
			return { std::string(g.xbox), std::string(g.xboxId) };
		}
		break;
	case SourceKind::PadAxis:
	{
		if (s.code == module::gamepad::LeftTrigger || s.code == module::gamepad::RightTrigger)
		{
			const bool left = s.code == module::gamepad::LeftTrigger;
			if (f == PadFamily::PlayStation) { return { left ? "L2" : "R2", left ? "ps_l2" : "ps_r2" }; }
			if (f == PadFamily::Nintendo) { return { left ? "ZL" : "ZR", left ? "nintendo_zl" : "nintendo_zr" }; }
			return { left ? "LT" : "RT", left ? "xbox_lt" : "xbox_rt" };
		}
		const std::string name(nameByCode(kPadAxisNames, s.code));
		const std::string dir = s.sign > 0 ? "+" : (s.sign < 0 ? "-" : "");
		return { name + dir, "axis_" + lowerAscii(name) + (s.sign > 0 ? "_pos" : (s.sign < 0 ? "_neg" : "")) };
	}
	case SourceKind::None:
		break;
	}
	return { "-", "none" };
}

/// @brief 入力 1 つの表示名と絵柄の名前。パッドの入力は kind の機種の書き方になる。
[[nodiscard]] inline InputGlyph inputGlyph(const InputSource& s, PadKind kind = PadKind::Unknown)
{
	return inputGlyph(s, padFamilyOf(kind));
}

}  // namespace mitiru::input
