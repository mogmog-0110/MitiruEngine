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

/// @brief 位置で束縛したキーの、今の配列での刻印 (UTF-8)。空なら表の名前を使う。窓の層が OS から引いて渡す
using KeyLabelFn = std::string (*)(int vk);

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

/// @brief 刻印の文字から絵柄の名前を決める。絵があるのは英数字と一部の記号だけ。無ければ透明な絵
/// @details 位置の名前から作った絵 (US 配列の刻印) を出すと、AZERTY で W の絵が Z のキーに付いてしまう。
[[nodiscard]] inline std::string keyGlyphForLabel(std::string_view label)
{
	if (label.size() == 1)
	{
		const char c = label[0];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { return "key_" + lowerAscii(std::string(label)); }
		switch (c)
		{
		case ',': return "key_comma";
		case '.': return "key_period";
		case ';': return "key_semicolon";
		case '/': return "key_slash";
		case '-': return "key_minus";
		case '`': return "key_backquote";
		default: break;
		}
	}
	return "none";
}

[[nodiscard]] inline std::string modifierLabel(std::uint8_t mods)
{
	std::string out;
	if (mods & kModCtrl) { out += "Ctrl+"; }
	if (mods & kModShift) { out += "Shift+"; }
	if (mods & kModAlt) { out += "Alt+"; }
	return out;
}

/// @brief 入力 1 つの表示名と絵柄の名前。パッドの入力は f の書き方になる。
/// @param keyLabel 位置で束縛しているとき、キーを今の配列の刻印にする。nullptr なら表の名前
[[nodiscard]] inline InputGlyph inputGlyphBase(const InputSource& s, PadFamily f, KeyLabelFn keyLabel)
{
	switch (s.kind)
	{
	case SourceKind::Key:
	{
		const std::string name = keyNameFromVk(s.code);
		const std::string label = keyLabel != nullptr ? keyLabel(s.code) : std::string();
		const std::string byName = "key_" + lowerAscii(name);
		if (label.empty()) { return { name, byName }; }
		// US 配列の PC では刻印が表の名前と同じキーを指す。表示を変えない
		const std::string byLabel = keyGlyphForLabel(label);
		if (label == name || byLabel == byName) { return { name, byName }; }
		return { label, byLabel };
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
	case SourceKind::Wheel:
	{
		const std::string dir = s.code == 0 ? (s.sign > 0 ? "Up" : "Down") : (s.sign > 0 ? "Right" : "Left");
		return { "Wheel " + dir, "wheel_" + lowerAscii(dir) };
	}
	case SourceKind::None:
		break;
	}
	return { "-", "none" };
}

/// 修飾キーつきの入力には絵が無いので、文字の表示名だけを出す (絵は透明)
[[nodiscard]] inline InputGlyph inputGlyph(const InputSource& s, PadFamily f, KeyLabelFn keyLabel = nullptr)
{
	InputGlyph g = inputGlyphBase(s, f, keyLabel);
	if (s.mods != 0)
	{
		g.label = modifierLabel(s.mods) + g.label;
		g.glyph = "none";
	}
	return g;
}

/// @brief 入力 1 つの表示名と絵柄の名前。パッドの入力は kind の機種の書き方になる。
[[nodiscard]] inline InputGlyph inputGlyph(const InputSource& s, PadKind kind = PadKind::Unknown,
                                           KeyLabelFn keyLabel = nullptr)
{
	return inputGlyph(s, padFamilyOf(kind), keyLabel);
}

}  // namespace mitiru::input
