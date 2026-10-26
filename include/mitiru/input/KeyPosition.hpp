#pragma once

/// @file KeyPosition.hpp
/// @brief キーを文字ではなく物理位置で割り当てるための、スキャンコードと US 配列の VK の対応表。
/// @details InputSnapshot::keysDown の添字は VK のまま変えない。Position では、文字キーの添字に「US 配列でその位置が出す VK」を使う。AZERTY の PC でも既定の WASD が左手の同じ並びに来る。Windows.h に依存しないため、配列を偽る単体試験からも使える。

#include <cstdint>
#include <string_view>

namespace mitiru::input
{

enum class KeyMode : std::uint8_t
{
	Position,   ///< 物理の位置 (既定)
	Character,  ///< OS が配列から決めた VK (従来の動作)
};

[[nodiscard]] constexpr std::string_view keyModeName(KeyMode m) noexcept
{
	return m == KeyMode::Position ? "position" : "character";
}

namespace detail
{
struct PositionKey
{
	std::uint8_t scan;  ///< スキャンコードセット 1 (WM_KEYDOWN の lParam bit 16-23)
	std::uint8_t vk;    ///< US 配列でその位置が出す VK
};

/// 拡張キーでない文字キーだけを含む。矢印、F キー、Enter などは配列で動かないため、表に入れず OS の VK のままにする。
/// テンキーも含めない。NumLock による VK の変化は、配列ではなく状態の違いとして扱う。
inline constexpr PositionKey kPositionKeys[] = {
	{0x02, '1'}, {0x03, '2'}, {0x04, '3'}, {0x05, '4'}, {0x06, '5'},
	{0x07, '6'}, {0x08, '7'}, {0x09, '8'}, {0x0A, '9'}, {0x0B, '0'},
	{0x0C, 0xBD}, {0x0D, 0xBB},                                                    // - =
	{0x10, 'Q'}, {0x11, 'W'}, {0x12, 'E'}, {0x13, 'R'}, {0x14, 'T'},
	{0x15, 'Y'}, {0x16, 'U'}, {0x17, 'I'}, {0x18, 'O'}, {0x19, 'P'},
	{0x1A, 0xDB}, {0x1B, 0xDD},                                                    // [ ]
	{0x1E, 'A'}, {0x1F, 'S'}, {0x20, 'D'}, {0x21, 'F'}, {0x22, 'G'},
	{0x23, 'H'}, {0x24, 'J'}, {0x25, 'K'}, {0x26, 'L'},
	{0x27, 0xBA}, {0x28, 0xDE}, {0x29, 0xC0}, {0x2B, 0xDC},                       // ; ' ` バックスラッシュ
	{0x2C, 'Z'}, {0x2D, 'X'}, {0x2E, 'C'}, {0x2F, 'V'}, {0x30, 'B'},
	{0x31, 'N'}, {0x32, 'M'}, {0x33, 0xBC}, {0x34, 0xBE}, {0x35, 0xBF},           // , . /
	{0x56, 0xE2},                                                                  // ISO の追加キー
};
}  // namespace detail

/// @brief 拡張でないスキャンコードを位置の VK に変換する。表にない場合は 0。
[[nodiscard]] constexpr int positionVkFromScancode(unsigned scan) noexcept
{
	for (const detail::PositionKey& k : detail::kPositionKeys)
	{
		if (k.scan == scan) { return k.vk; }
	}
	return 0;
}

/// @brief 位置の VK をスキャンコードに変換する。画面に出すキー名を現在の配列から引くために使う。表にない場合は 0。
[[nodiscard]] constexpr unsigned scancodeFromPositionVk(int vk) noexcept
{
	for (const detail::PositionKey& k : detail::kPositionKeys)
	{
		if (k.vk == vk) { return k.scan; }
	}
	return 0;
}

/// 英数字と記号キーの VK。日本語配列の半角／全角（0xF3／0xF4）など、配列固有のキーは含まない。
[[nodiscard]] constexpr bool isCharacterKeyVk(int vk) noexcept
{
	return (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || (vk >= 0xBA && vk <= 0xC0)
		|| (vk >= 0xDB && vk <= 0xDF) || vk == 0xE2;
}

/// @brief ウィンドウが受け取ったキーを、InputSnapshot に書く VK に変換する。
/// @param osVk WM_KEYDOWN の wParam。IME の変換中は VK_PROCESSKEY（0xE5）でもよい
/// @param scan lParam の bit 16 から 23
/// @param extended lParam の bit 24
/// @details 位置を決められない場合は osVk を返す。Position でも、文字キーでない osVk（半角／全角、F キーなど）は変えない。IME の変換中の 0xE5 は文字キーとして扱う。スキャンコードが 0 の注入入力は VK のまま通す。
[[nodiscard]] constexpr int resolveKeyVk(KeyMode mode, int osVk, unsigned scan, bool extended) noexcept
{
	if (mode != KeyMode::Position || extended) { return osVk; }
	const int pos = positionVkFromScancode(scan);
	if (pos == 0) { return osVk; }
	constexpr int kVkProcessKey = 0xE5;
	return (osVk == kVkProcessKey || isCharacterKeyVk(osVk)) ? pos : osVk;
}

}  // namespace mitiru::input
