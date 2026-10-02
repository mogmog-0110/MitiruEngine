#pragma once
// 窓が受けた WM_KEYDOWN / WM_KEYUP / WM_CHAR (Win32Window::takeKeyMessages) を、RmlUi の
// ProcessKeyDown / ProcessKeyUp / ProcessTextInput に渡す形へ直す。文字はキーの押下状態からは決まらない
// (Shift・配列・IME で変わる) ので、窓が受けた順のまま 1 件ずつ写す。IME の確定文字は窓の側で
// WM_CHAR の列に直してあるので、ここでは普通の文字と区別しない。

#include <mitiru/platform/win32/Win32KeyMessage.hpp>

#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Types.h>

#include <cstdint>
#include <span>

namespace mitiru::ui_rml
{

struct RmlKeyEvent
{
	enum class Kind : std::uint8_t { Down, Up, Text };
	Kind kind = Kind::Down;
	Rml::Input::KeyIdentifier key = Rml::Input::KI_UNKNOWN;
	int modifiers = 0;
	Rml::Character character = Rml::Character::Null;
};

namespace detail
{

struct VkRange { std::uint32_t first, last; int base; };

// 仮想キーコードと RmlUi の識別子が同じ並びで連続している区間。
inline constexpr VkRange kVkRanges[] = {
	{ 0x30, 0x39, Rml::Input::KI_0 },        { 0x41, 0x5A, Rml::Input::KI_A },
	{ 0x60, 0x69, Rml::Input::KI_NUMPAD0 },  { 0x6A, 0x6F, Rml::Input::KI_MULTIPLY },
	{ 0x70, 0x87, Rml::Input::KI_F1 },       { 0x25, 0x28, Rml::Input::KI_LEFT },
	{ 0xA0, 0xA5, Rml::Input::KI_LSHIFT },   { 0xBA, 0xC0, Rml::Input::KI_OEM_1 },
	{ 0xDB, 0xDF, Rml::Input::KI_OEM_4 },
};

struct VkSingle { std::uint32_t vk; Rml::Input::KeyIdentifier key; };

inline constexpr VkSingle kVkSingles[] = {
	{ 0x08, Rml::Input::KI_BACK },     { 0x09, Rml::Input::KI_TAB },      { 0x0C, Rml::Input::KI_CLEAR },
	{ 0x0D, Rml::Input::KI_RETURN },   { 0x10, Rml::Input::KI_LSHIFT },   { 0x11, Rml::Input::KI_LCONTROL },
	{ 0x12, Rml::Input::KI_LMENU },    { 0x13, Rml::Input::KI_PAUSE },    { 0x14, Rml::Input::KI_CAPITAL },
	{ 0x15, Rml::Input::KI_KANA },     { 0x19, Rml::Input::KI_KANJI },    { 0x1B, Rml::Input::KI_ESCAPE },
	{ 0x1C, Rml::Input::KI_CONVERT },  { 0x1D, Rml::Input::KI_NONCONVERT }, { 0x20, Rml::Input::KI_SPACE },
	{ 0x21, Rml::Input::KI_PRIOR },    { 0x22, Rml::Input::KI_NEXT },     { 0x23, Rml::Input::KI_END },
	{ 0x24, Rml::Input::KI_HOME },     { 0x2C, Rml::Input::KI_SNAPSHOT }, { 0x2D, Rml::Input::KI_INSERT },
	{ 0x2E, Rml::Input::KI_DELETE },   { 0x5B, Rml::Input::KI_LWIN },     { 0x5C, Rml::Input::KI_RWIN },
	{ 0x5D, Rml::Input::KI_APPS },     { 0x90, Rml::Input::KI_NUMLOCK },  { 0x91, Rml::Input::KI_SCROLL },
	{ 0xE2, Rml::Input::KI_OEM_102 },  { 0xE5, Rml::Input::KI_PROCESSKEY },
};

// lParam の bit 24。テンキーの Enter は VK_RETURN のままこの印だけが違う。
inline constexpr std::int32_t kExtendedKeyBit = 1 << 24;

} // namespace detail

[[nodiscard]] constexpr Rml::Input::KeyIdentifier toRmlKey(std::uint32_t vk, std::int32_t lParam) noexcept
{
	if (vk == 0x0D && (lParam & detail::kExtendedKeyBit) != 0) { return Rml::Input::KI_NUMPADENTER; }
	for (const auto& r : detail::kVkRanges)
	{
		if (vk >= r.first && vk <= r.last)
		{
			return static_cast<Rml::Input::KeyIdentifier>(r.base + static_cast<int>(vk - r.first));
		}
	}
	for (const auto& s : detail::kVkSingles)
	{
		if (s.vk == vk) { return s.key; }
	}
	return Rml::Input::KI_UNKNOWN;
}

[[nodiscard]] constexpr int toRmlModifiers(std::uint32_t keyState) noexcept
{
	namespace ks = platform::keystate;
	int m = 0;
	// AltGr で打った文字は Ctrl+Alt を伴って届くが、ショートカットとして扱うと文字が入らない。
	if ((keyState & ks::kAltGr) == 0)
	{
		if (keyState & ks::kControl) { m |= Rml::Input::KM_CTRL; }
		if (keyState & ks::kAlt)     { m |= Rml::Input::KM_ALT; }
	}
	if (keyState & ks::kShift)    { m |= Rml::Input::KM_SHIFT; }
	if (keyState & ks::kCapsLock) { m |= Rml::Input::KM_CAPSLOCK; }
	if (keyState & ks::kNumLock)  { m |= Rml::Input::KM_NUMLOCK; }
	return m;
}

/// サロゲートペアは WM_CHAR 2 件に分かれて届くので、前半をフレームをまたいで覚えておく。
class RmlKeyTranslator
{
public:
	template <class Emit>
	void translate(std::span<const platform::Win32KeyMessage> messages, Emit&& emit)
	{
		for (const auto& m : messages) { translateOne(m, emit); }
	}

private:
	template <class Emit>
	void translateOne(const platform::Win32KeyMessage& m, Emit& emit)
	{
		const int mods = toRmlModifiers(m.keyState);
		switch (m.message)
		{
		case platform::kWmKeyDown:
		case platform::kWmSysKeyDown:
			emit(RmlKeyEvent{ RmlKeyEvent::Kind::Down, toRmlKey(m.wParam, m.lParam), mods, Rml::Character::Null });
			break;
		case platform::kWmKeyUp:
		case platform::kWmSysKeyUp:
			emit(RmlKeyEvent{ RmlKeyEvent::Kind::Up, toRmlKey(m.wParam, m.lParam), mods, Rml::Character::Null });
			break;
		case platform::kWmChar:
			if (const char32_t c = takeCodePoint(static_cast<char16_t>(m.wParam)); isTypedText(c))
			{
				emit(RmlKeyEvent{ RmlKeyEvent::Kind::Text, Rml::Input::KI_UNKNOWN, mods, static_cast<Rml::Character>(c) });
			}
			break;
		default:
			break;
		}
	}

	// 0 を返したら、まだ文字になっていない (サロゲートの前半を受けた) か、不正な列。
	char32_t takeCodePoint(char16_t unit) noexcept
	{
		if (unit >= 0xD800 && unit < 0xDC00)
		{
			m_highSurrogate = unit;
			return 0;
		}
		const char16_t high = m_highSurrogate;
		m_highSurrogate = 0;
		if (unit >= 0xDC00 && unit < 0xE000)
		{
			if (high == 0) { return 0; }
			return 0x10000u + ((static_cast<char32_t>(high) - 0xD800u) << 10) + (static_cast<char32_t>(unit) - 0xDC00u);
		}
		return unit == u'\r' ? U'\n' : static_cast<char32_t>(unit);
	}

	// Backspace (0x08) や Ctrl+文字の制御コードはキーとして別に届くので、文字としては送らない。
	static constexpr bool isTypedText(char32_t c) noexcept
	{
		return c == U'\n' || (c >= 32 && c != 127);
	}

	char16_t m_highSurrogate = 0;
};

} // namespace mitiru::ui_rml
