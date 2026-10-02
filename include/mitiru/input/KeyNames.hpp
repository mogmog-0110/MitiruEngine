#pragma once

/// @file KeyNames.hpp
/// @brief 仮想キーコード (Windows の VK、InputSnapshot::keysDown の添字) と、設定ファイルに書く名前の対応。

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace mitiru::input
{

struct KeyName
{
	std::string_view name;
	int vk;
};

/// 名前の付いたキー。英字と数字は 1 文字のまま ("A", "7") で、表には載せない。
inline constexpr std::array<KeyName, 54> kNamedKeys = {{
	{ "Back", 0x08 }, { "Tab", 0x09 }, { "Enter", 0x0D }, { "Shift", 0x10 }, { "Ctrl", 0x11 }, { "Alt", 0x12 },
	{ "Pause", 0x13 }, { "CapsLock", 0x14 }, { "Escape", 0x1B }, { "Space", 0x20 }, { "PageUp", 0x21 },
	{ "PageDown", 0x22 }, { "End", 0x23 }, { "Home", 0x24 }, { "Left", 0x25 }, { "Up", 0x26 }, { "Right", 0x27 },
	{ "Down", 0x28 }, { "Insert", 0x2D }, { "Delete", 0x2E },
	{ "Num0", 0x60 }, { "Num1", 0x61 }, { "Num2", 0x62 }, { "Num3", 0x63 }, { "Num4", 0x64 }, { "Num5", 0x65 },
	{ "Num6", 0x66 }, { "Num7", 0x67 }, { "Num8", 0x68 }, { "Num9", 0x69 },
	{ "F1", 0x70 }, { "F2", 0x71 }, { "F3", 0x72 }, { "F4", 0x73 }, { "F5", 0x74 }, { "F6", 0x75 },
	{ "F7", 0x76 }, { "F8", 0x77 }, { "F9", 0x78 }, { "F10", 0x79 }, { "F11", 0x7A }, { "F12", 0x7B },
	{ "LShift", 0xA0 }, { "RShift", 0xA1 }, { "LCtrl", 0xA2 }, { "RCtrl", 0xA3 }, { "LAlt", 0xA4 }, { "RAlt", 0xA5 },
	{ "Semicolon", 0xBA }, { "Comma", 0xBC }, { "Minus", 0xBD }, { "Period", 0xBE }, { "Slash", 0xBF },
	{ "Backquote", 0xC0 },
}};

[[nodiscard]] inline bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size()) { return false; }
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) { return false; }
	}
	return true;
}

/// @brief 名前 → VK。大文字小文字は区別しない。分からなければ -1。
[[nodiscard]] inline int vkFromKeyName(std::string_view name) noexcept
{
	if (name.size() == 1)
	{
		const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { return static_cast<unsigned char>(c); }
	}
	for (const KeyName& k : kNamedKeys)
	{
		if (equalsIgnoreCase(k.name, name)) { return k.vk; }
	}
	if (name.size() > 2 && name.size() <= 5 && equalsIgnoreCase(name.substr(0, 2), "VK"))
	{
		int vk = 0;
		for (const char c : name.substr(2))
		{
			if (c < '0' || c > '9') { return -1; }
			vk = vk * 10 + (c - '0');
		}
		return vk < 256 ? vk : -1;
	}
	return -1;
}

/// @brief VK → 名前。表に無いキーは "VK<10 進>"。
[[nodiscard]] inline std::string keyNameFromVk(int vk)
{
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { return std::string(1, static_cast<char>(vk)); }
	for (const KeyName& k : kNamedKeys)
	{
		if (k.vk == vk) { return std::string(k.name); }
	}
	return "VK" + std::to_string(vk);
}

}  // namespace mitiru::input
