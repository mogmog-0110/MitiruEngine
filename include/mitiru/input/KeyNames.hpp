#pragma once

/// @file KeyNames.hpp
/// @brief キーの名前と仮想キーコード (Windows の VK、InputSnapshot::keysDown の添字) の表。
/// @details `mitiru::Key` の列挙子、入力台本 (--input-script) のキー名、キー割り当ての設定ファイルは
///          どれもこの表から作る。表が 1 つなので、`mitiru::Key` にあるキーは台本と設定ファイルにも
///          同じ名前で書ける。名前の大文字小文字は区別しない。

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// KEY(名前, 仮想キーコード)。KEY_AS(列挙子, 文字の名前, 仮想キーコード) は列挙子と文字の名前が違うもの。
/// 英字と数字は 1 文字 ("A", "7") でも書ける。文字キーの段の数字は Digit0..Digit9 (列挙子は Key::Num0..Num9)、
/// テンキーは Numpad0..Numpad9。名前は Web の KeyboardEvent.code に揃えてある。
#define MITIRU_KEY_TABLE(KEY, KEY_AS)                                                                                               \
	KEY(Left, 0x25) KEY(Up, 0x26) KEY(Right, 0x27) KEY(Down, 0x28)                                                                  \
	KEY(Space, 0x20) KEY(Enter, 0x0D) KEY(Escape, 0x1B) KEY(Tab, 0x09) KEY(Shift, 0x10) KEY(Ctrl, 0x11)                             \
	KEY(Alt, 0x12) KEY(CapsLock, 0x14) KEY(Backspace, 0x08) KEY(Delete, 0x2E) KEY(Insert, 0x2D)                                     \
	KEY(Home, 0x24) KEY(End, 0x23) KEY(PageUp, 0x21) KEY(PageDown, 0x22) KEY(Pause, 0x13)                                           \
	KEY(F1, 0x70) KEY(F2, 0x71) KEY(F3, 0x72) KEY(F4, 0x73) KEY(F5, 0x74) KEY(F6, 0x75)                                             \
	KEY(F7, 0x76) KEY(F8, 0x77) KEY(F9, 0x78) KEY(F10, 0x79) KEY(F11, 0x7A) KEY(F12, 0x7B)                                          \
	KEY(A, 'A') KEY(B, 'B') KEY(C, 'C') KEY(D, 'D') KEY(E, 'E') KEY(F, 'F') KEY(G, 'G') KEY(H, 'H') KEY(I, 'I')                     \
	KEY(J, 'J') KEY(K, 'K') KEY(L, 'L') KEY(M, 'M') KEY(N, 'N') KEY(O, 'O') KEY(P, 'P') KEY(Q, 'Q') KEY(R, 'R')                     \
	KEY(S, 'S') KEY(T, 'T') KEY(U, 'U') KEY(V, 'V') KEY(W, 'W') KEY(X, 'X') KEY(Y, 'Y') KEY(Z, 'Z')                                 \
	KEY_AS(Num0, Digit0, '0') KEY_AS(Num1, Digit1, '1') KEY_AS(Num2, Digit2, '2') KEY_AS(Num3, Digit3, '3') KEY_AS(Num4, Digit4, '4') \
	KEY_AS(Num5, Digit5, '5') KEY_AS(Num6, Digit6, '6') KEY_AS(Num7, Digit7, '7') KEY_AS(Num8, Digit8, '8') KEY_AS(Num9, Digit9, '9') \
	KEY(Numpad0, 0x60) KEY(Numpad1, 0x61) KEY(Numpad2, 0x62) KEY(Numpad3, 0x63) KEY(Numpad4, 0x64)                                  \
	KEY(Numpad5, 0x65) KEY(Numpad6, 0x66) KEY(Numpad7, 0x67) KEY(Numpad8, 0x68) KEY(Numpad9, 0x69)                                  \
	KEY(LShift, 0xA0) KEY(RShift, 0xA1) KEY(LCtrl, 0xA2) KEY(RCtrl, 0xA3) KEY(LAlt, 0xA4) KEY(RAlt, 0xA5)                           \
	KEY(Semicolon, 0xBA) KEY(Comma, 0xBC) KEY(Minus, 0xBD) KEY(Period, 0xBE) KEY(Slash, 0xBF)                                       \
	KEY(Backquote, 0xC0)

namespace mitiru::input
{

struct KeyName
{
	std::string_view name;
	int vk;
};

#define MITIRU_KEY_NAME_ENTRY(name, vk) KeyName{#name, vk},
#define MITIRU_KEY_NAME_ENTRY_AS(enumerator, name, vk) KeyName{#name, vk},
inline constexpr KeyName kNamedKeys[] = {MITIRU_KEY_TABLE(MITIRU_KEY_NAME_ENTRY, MITIRU_KEY_NAME_ENTRY_AS)};
#undef MITIRU_KEY_NAME_ENTRY
#undef MITIRU_KEY_NAME_ENTRY_AS

/// 読むときだけ通す別名。書き出し (keyNameFromVk) は表の名前を使う。
inline constexpr KeyName kKeyNameAliases[] = {
	{"Return", 0x0D}, {"Esc", 0x1B}, {"Control", 0x11}, {"Back", 0x08},
};

[[nodiscard]] inline bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size()) { return false; }
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) { return false; }
	}
	return true;
}

namespace detail
{
	/// "VK186" / "186" / "0xBA" を 0..255 の VK にする。数でなければ -1。
	[[nodiscard]] inline int vkFromNumber(std::string_view s) noexcept
	{
		if (s.size() > 2 && equalsIgnoreCase(s.substr(0, 2), "VK")) { s.remove_prefix(2); }
		int base = 10;
		if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s.remove_prefix(2); base = 16; }
		if (s.empty() || s.size() > 3) { return -1; }
		int vk = 0;
		for (const char c : s)
		{
			const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
				: (base == 16 && std::isxdigit(static_cast<unsigned char>(c))) ? std::tolower(c) - 'a' + 10 : -1;
			if (d < 0) { return -1; }
			vk = vk * base + d;
		}
		return vk < 256 ? vk : -1;
	}

	/// 大文字小文字を区別しない編集距離。
	[[nodiscard]] inline std::size_t editDistance(std::string_view a, std::string_view b)
	{
		std::vector<std::size_t> row(b.size() + 1, 0), last(b.size() + 1, 0);
		for (std::size_t j = 0; j <= b.size(); ++j) { last[j] = j; }
		for (std::size_t i = 1; i <= a.size(); ++i)
		{
			row[0] = i;
			for (std::size_t j = 1; j <= b.size(); ++j)
			{
				const bool same = std::tolower(static_cast<unsigned char>(a[i - 1]))
					== std::tolower(static_cast<unsigned char>(b[j - 1]));
				const std::size_t replace = last[j - 1] + (same ? std::size_t{0} : std::size_t{1});
				row[j] = std::min({last[j] + 1, row[j - 1] + 1, replace});
			}
			std::swap(row, last);
		}
		return last[b.size()];
	}
}  // namespace detail

/// @brief 名前 → VK。表の名前・別名・英数字 1 文字・数 ("VK186" / "186" / "0xBA") を受ける。分からなければ -1。
[[nodiscard]] inline int vkFromKeyName(std::string_view name) noexcept
{
	if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])))
	{
		return std::toupper(static_cast<unsigned char>(name[0]));
	}
	for (const KeyName& k : kNamedKeys) { if (equalsIgnoreCase(k.name, name)) { return k.vk; } }
	for (const KeyName& k : kKeyNameAliases) { if (equalsIgnoreCase(k.name, name)) { return k.vk; } }
	return detail::vkFromNumber(name);
}

/// @brief "Num0".."Num9" なら数字 (0..9)、そうでなければ -1。
/// @details v0.38 までの設定ファイルではテンキー、Key::Num0 では文字キーの段で、同じ名前が別のキーを指していた。
///          どちらか決められないので、表の名前にはせず、読む側が扱いを決める (設定ファイルだけは古い意味で読む)。
[[nodiscard]] inline int ambiguousDigitName(std::string_view name) noexcept
{
	if (name.size() != 4 || !equalsIgnoreCase(name.substr(0, 3), "Num") || !std::isdigit(static_cast<unsigned char>(name[3])))
	{
		return -1;
	}
	return name[3] - '0';
}

/// @brief "Num0" などを書いた所へ出す文。どちらの名前で書けばよいかを添える
[[nodiscard]] inline std::string ambiguousDigitMessage(std::string_view name)
{
	const std::string d(1, name.back());
	return "'" + std::string(name) + "' はどちらの " + d + " か分かりません。文字キーの段なら Digit" + d
		+ "、テンキーなら Numpad" + d + " と書いてください。";
}

/// @brief VK → 名前。英字と数字は 1 文字、表に無いキーは "VK<10 進>"。
[[nodiscard]] inline std::string keyNameFromVk(int vk)
{
	if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { return std::string(1, static_cast<char>(vk)); }
	for (const KeyName& k : kNamedKeys) { if (k.vk == vk) { return std::string(k.name); } }
	return "VK" + std::to_string(vk);
}

/// @brief 分からなかった名前にいちばん近い表の名前。誤りの文に「もしかして」として出す。
[[nodiscard]] inline std::string_view nearestKeyName(std::string_view name)
{
	// 距離が同じなら長さの近い方 ("F13" には F1 より F12)。
	const auto score = [&](std::string_view cand) {
		const std::size_t lenGap = cand.size() > name.size() ? cand.size() - name.size() : name.size() - cand.size();
		return detail::editDistance(name, cand) * 64 + lenGap;
	};
	std::string_view best = kNamedKeys[0].name;
	std::size_t bestScore = score(best);
	for (const KeyName& k : kNamedKeys)
	{
		const std::size_t s = score(k.name);
		if (s < bestScore) { bestScore = s; best = k.name; }
	}
	return best;
}

}  // namespace mitiru::input
