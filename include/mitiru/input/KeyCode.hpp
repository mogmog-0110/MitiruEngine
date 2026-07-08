#pragma once

/// @file KeyCode.hpp
/// @brief プラットフォーム非依存キーコード
/// @details ゲームエンジン内部で使用する統一キーコード定義。
///          文字列との相互変換も提供する。

#include <cstdint>
#include <string>
#include <string_view>

namespace mitiru
{

/// @brief プラットフォーム非依存キーコード
/// @details Windows仮想キーコードに準拠した値を使用する。
enum class KeyCode : int
{
	Unknown = 0,         ///< 不明なキー

	/// --- 数字キー ---
	Num0 = 48,           ///< 0
	Num1 = 49,           ///< 1
	Num2 = 50,           ///< 2
	Num3 = 51,           ///< 3
	Num4 = 52,           ///< 4
	Num5 = 53,           ///< 5
	Num6 = 54,           ///< 6
	Num7 = 55,           ///< 7
	Num8 = 56,           ///< 8
	Num9 = 57,           ///< 9

	/// --- 文字キー ---
	A = 65,              ///< A
	B = 66,              ///< B
	C = 67,              ///< C
	D = 68,              ///< D
	E = 69,              ///< E
	F = 70,              ///< F
	G = 71,              ///< G
	H = 72,              ///< H
	I = 73,              ///< I
	J = 74,              ///< J
	K = 75,              ///< K
	L = 76,              ///< L
	M = 77,              ///< M
	N = 78,              ///< N
	O = 79,              ///< O
	P = 80,              ///< P
	Q = 81,              ///< Q
	R = 82,              ///< R
	S = 83,              ///< S
	T = 84,              ///< T
	U = 85,              ///< U
	V = 86,              ///< V
	W = 87,              ///< W
	X = 88,              ///< X
	Y = 89,              ///< Y
	Z = 90,              ///< Z

	/// --- 制御キー ---
	Backspace = 8,       ///< Backspace
	Tab = 9,             ///< Tab
	Enter = 13,          ///< Enter / Return
	Shift = 16,          ///< Shift（汎用）
	Ctrl = 17,           ///< Ctrl / Control（汎用）
	Alt = 18,            ///< Alt（汎用）
	Escape = 27,         ///< Escape
	Space = 32,          ///< Space

	/// --- 修飾キー（左右別） ---
	LShift = 160,        ///< 左Shift（VK_LSHIFT）
	RShift = 161,        ///< 右Shift（VK_RSHIFT）
	LCtrl = 162,         ///< 左Ctrl（VK_LCONTROL）
	RCtrl = 163,         ///< 右Ctrl（VK_RCONTROL）
	LAlt = 164,          ///< 左Alt（VK_LMENU）
	RAlt = 165,          ///< 右Alt（VK_RMENU）

	/// --- 矢印キー ---
	Left = 37,           ///< 左矢印
	Up = 38,             ///< 上矢印
	Right = 39,          ///< 右矢印
	Down = 40,           ///< 下矢印

	/// --- 矢印キー（別名 / DOM 互換） ---
	ArrowLeft = Left,    ///< 左矢印（別名）
	ArrowUp = Up,        ///< 上矢印（別名）
	ArrowRight = Right,  ///< 右矢印（別名）
	ArrowDown = Down,    ///< 下矢印（別名）

	/// --- ファンクションキー ---
	F1 = 112,            ///< F1
	F2 = 113,            ///< F2
	F3 = 114,            ///< F3
	F4 = 115,            ///< F4
	F5 = 116,            ///< F5
	F6 = 117,            ///< F6
	F7 = 118,            ///< F7
	F8 = 119,            ///< F8
	F9 = 120,            ///< F9
	F10 = 121,           ///< F10
	F11 = 122,           ///< F11
	F12 = 123,           ///< F12

	/// --- その他 ---
	Delete = 46,         ///< Delete
	Insert = 45,         ///< Insert
	Home = 36,           ///< Home
	End = 35,            ///< End
	PageUp = 33,         ///< Page Up
	PageDown = 34,       ///< Page Down
	CapsLock = 20,       ///< Caps Lock

	/// --- 句読点 / 記号キー（OEM, US配列基準） ---
	Semicolon = 186,     ///< ; :  （VK_OEM_1）
	Equal = 187,         ///< = +  （VK_OEM_PLUS）
	Comma = 188,         ///< , <  （VK_OEM_COMMA）
	Minus = 189,         ///< - _  （VK_OEM_MINUS）
	Period = 190,        ///< . >  （VK_OEM_PERIOD）
	Slash = 191,         ///< / ?  （VK_OEM_2）
	Backquote = 192,     ///< ` ~  （VK_OEM_3）
	LeftBracket = 219,   ///< [ {  （VK_OEM_4）
	Backslash = 220,     ///< \\ |  （VK_OEM_5）
	RightBracket = 221,  ///< ] }  （VK_OEM_6）
	Quote = 222,         ///< ' "  （VK_OEM_7）

	/// --- テンキー ---
	Numpad0 = 96,        ///< テンキー0（VK_NUMPAD0）
	Numpad1 = 97,        ///< テンキー1
	Numpad2 = 98,        ///< テンキー2
	Numpad3 = 99,        ///< テンキー3
	Numpad4 = 100,       ///< テンキー4
	Numpad5 = 101,       ///< テンキー5
	Numpad6 = 102,       ///< テンキー6
	Numpad7 = 103,       ///< テンキー7
	Numpad8 = 104,       ///< テンキー8
	Numpad9 = 105,       ///< テンキー9
	NumpadMultiply = 106, ///< テンキー*（VK_MULTIPLY）
	NumpadAdd = 107,     ///< テンキー+（VK_ADD）
	NumpadSubtract = 109, ///< テンキー-（VK_SUBTRACT）
	NumpadDecimal = 110, ///< テンキー.（VK_DECIMAL）
	NumpadDivide = 111,  ///< テンキー/（VK_DIVIDE）

	/// @brief テンキーEnter
	/// @details Win32 仮想キーコード上は通常 Enter (VK_RETURN = 0x0D) と区別されない。
	///          物理位置は extended-key bit (lParam bit 24) で判別するが、
	///          VK レベルでは同一値となる。
	NumpadEnter = Enter,
};

/// --- コンパイル時不変条件 ---
/// @details 既存の consumer ゲームがリテラル値に依存しているため、
///          Win32 VK との整合性を機械的に保証する。
static_assert(static_cast<int>(KeyCode::Left) == 37,    "KeyCode::Left must equal VK_LEFT (0x25)");
static_assert(static_cast<int>(KeyCode::Up) == 38,      "KeyCode::Up must equal VK_UP (0x26)");
static_assert(static_cast<int>(KeyCode::Right) == 39,   "KeyCode::Right must equal VK_RIGHT (0x27)");
static_assert(static_cast<int>(KeyCode::Down) == 40,    "KeyCode::Down must equal VK_DOWN (0x28)");
static_assert(static_cast<int>(KeyCode::Escape) == 27,  "KeyCode::Escape must equal VK_ESCAPE (0x1B)");
static_assert(static_cast<int>(KeyCode::Space) == 32,   "KeyCode::Space must equal VK_SPACE (0x20)");
static_assert(static_cast<int>(KeyCode::Enter) == 13,   "KeyCode::Enter must equal VK_RETURN (0x0D)");
static_assert(static_cast<int>(KeyCode::ArrowLeft) == static_cast<int>(KeyCode::Left),   "ArrowLeft must alias Left");
static_assert(static_cast<int>(KeyCode::ArrowRight) == static_cast<int>(KeyCode::Right), "ArrowRight must alias Right");
static_assert(static_cast<int>(KeyCode::ArrowUp) == static_cast<int>(KeyCode::Up),       "ArrowUp must alias Up");
static_assert(static_cast<int>(KeyCode::ArrowDown) == static_cast<int>(KeyCode::Down),   "ArrowDown must alias Down");
static_assert(static_cast<int>(KeyCode::NumpadEnter) == static_cast<int>(KeyCode::Enter), "NumpadEnter aliases Enter at VK level");

/// @brief キーコードを文字列に変換する
/// @param code キーコード
/// @return キーコードの文字列表現
[[nodiscard]] inline std::string_view keyCodeToString(KeyCode code) noexcept
{
	switch (code)
	{
	case KeyCode::Unknown:   return "Unknown";
	case KeyCode::Num0:      return "0";
	case KeyCode::Num1:      return "1";
	case KeyCode::Num2:      return "2";
	case KeyCode::Num3:      return "3";
	case KeyCode::Num4:      return "4";
	case KeyCode::Num5:      return "5";
	case KeyCode::Num6:      return "6";
	case KeyCode::Num7:      return "7";
	case KeyCode::Num8:      return "8";
	case KeyCode::Num9:      return "9";
	case KeyCode::A:         return "A";
	case KeyCode::B:         return "B";
	case KeyCode::C:         return "C";
	case KeyCode::D:         return "D";
	case KeyCode::E:         return "E";
	case KeyCode::F:         return "F";
	case KeyCode::G:         return "G";
	case KeyCode::H:         return "H";
	case KeyCode::I:         return "I";
	case KeyCode::J:         return "J";
	case KeyCode::K:         return "K";
	case KeyCode::L:         return "L";
	case KeyCode::M:         return "M";
	case KeyCode::N:         return "N";
	case KeyCode::O:         return "O";
	case KeyCode::P:         return "P";
	case KeyCode::Q:         return "Q";
	case KeyCode::R:         return "R";
	case KeyCode::S:         return "S";
	case KeyCode::T:         return "T";
	case KeyCode::U:         return "U";
	case KeyCode::V:         return "V";
	case KeyCode::W:         return "W";
	case KeyCode::X:         return "X";
	case KeyCode::Y:         return "Y";
	case KeyCode::Z:         return "Z";
	case KeyCode::Backspace: return "Backspace";
	case KeyCode::Tab:       return "Tab";
	case KeyCode::Enter:     return "Enter";
	case KeyCode::Shift:     return "Shift";
	case KeyCode::Ctrl:      return "Ctrl";
	case KeyCode::Alt:       return "Alt";
	case KeyCode::Escape:    return "Escape";
	case KeyCode::Space:     return "Space";
	case KeyCode::Backquote: return "`";
	case KeyCode::Semicolon: return ";";
	case KeyCode::Comma:     return ",";
	case KeyCode::Period:    return ".";
	case KeyCode::Minus:     return "-";
	case KeyCode::Slash:     return "/";
	case KeyCode::Left:      return "Left";
	case KeyCode::Up:        return "Up";
	case KeyCode::Right:     return "Right";
	case KeyCode::Down:      return "Down";
	case KeyCode::F1:        return "F1";
	case KeyCode::F2:        return "F2";
	case KeyCode::F3:        return "F3";
	case KeyCode::F4:        return "F4";
	case KeyCode::F5:        return "F5";
	case KeyCode::F6:        return "F6";
	case KeyCode::F7:        return "F7";
	case KeyCode::F8:        return "F8";
	case KeyCode::F9:        return "F9";
	case KeyCode::F10:       return "F10";
	case KeyCode::F11:       return "F11";
	case KeyCode::F12:       return "F12";
	case KeyCode::Delete:    return "Delete";
	case KeyCode::Insert:    return "Insert";
	case KeyCode::Home:      return "Home";
	case KeyCode::End:       return "End";
	case KeyCode::PageUp:    return "PageUp";
	case KeyCode::PageDown:  return "PageDown";
	case KeyCode::CapsLock:  return "CapsLock";
	default:                 return "Unknown";
	}
}

/// @brief 文字列からキーコードに変換する
/// @param name キーコード名
/// @return 対応するキーコード（見つからなければ Unknown）
[[nodiscard]] inline KeyCode stringToKeyCode(std::string_view name) noexcept
{
	/// 文字キー（1文字）
	if (name.size() == 1)
	{
		const char ch = name[0];
		if (ch >= 'A' && ch <= 'Z')
		{
			return static_cast<KeyCode>(ch);
		}
		if (ch >= 'a' && ch <= 'z')
		{
			return static_cast<KeyCode>(ch - 'a' + 'A');
		}
		if (ch >= '0' && ch <= '9')
		{
			return static_cast<KeyCode>(ch);
		}
	}

	/// 名前付きキー
	if (name == "Backspace")  return KeyCode::Backspace;
	if (name == "Tab")        return KeyCode::Tab;
	if (name == "Enter")      return KeyCode::Enter;
	if (name == "Shift")      return KeyCode::Shift;
	if (name == "Ctrl")       return KeyCode::Ctrl;
	if (name == "Alt")        return KeyCode::Alt;
	if (name == "Escape")     return KeyCode::Escape;
	if (name == "Space")      return KeyCode::Space;
	if (name == "Left")       return KeyCode::Left;
	if (name == "Up")         return KeyCode::Up;
	if (name == "Right")      return KeyCode::Right;
	if (name == "Down")       return KeyCode::Down;
	if (name == "Delete")     return KeyCode::Delete;
	if (name == "Insert")     return KeyCode::Insert;
	if (name == "Home")       return KeyCode::Home;
	if (name == "End")        return KeyCode::End;
	if (name == "PageUp")     return KeyCode::PageUp;
	if (name == "PageDown")   return KeyCode::PageDown;
	if (name == "CapsLock")   return KeyCode::CapsLock;
	if (name == "F1")         return KeyCode::F1;
	if (name == "F2")         return KeyCode::F2;
	if (name == "F3")         return KeyCode::F3;
	if (name == "F4")         return KeyCode::F4;
	if (name == "F5")         return KeyCode::F5;
	if (name == "F6")         return KeyCode::F6;
	if (name == "F7")         return KeyCode::F7;
	if (name == "F8")         return KeyCode::F8;
	if (name == "F9")         return KeyCode::F9;
	if (name == "F10")        return KeyCode::F10;
	if (name == "F11")        return KeyCode::F11;
	if (name == "F12")        return KeyCode::F12;

	return KeyCode::Unknown;
}

} // namespace mitiru
