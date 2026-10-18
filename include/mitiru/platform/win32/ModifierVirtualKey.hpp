#pragma once

/// @file ModifierVirtualKey.hpp
/// @brief WM_KEYDOWN / WM_KEYUP で左右の区別なく届く Shift・Ctrl・Alt を、左右の付いたキーへ分ける

namespace mitiru::platform
{

/// @param vk     WM_KEYDOWN / WM_KEYUP の wParam
/// @param lParam 同じメッセージの lParam (16〜23 ビットがスキャンコード、24 ビットが拡張キー)
/// @return 左右の付いたキー (VK_LSHIFT など)。Shift・Ctrl・Alt 以外は 0
/// @details Windows は Shift・Ctrl・Alt を VK_SHIFT・VK_CONTROL・VK_MENU で送り、VK_LSHIFT などでは送らない。
///          そのまま記録すると Key::LShift や Key::LCtrl は実際の窓では一度も押されない (入力の台本は
///          キーの番号を直接入れるので通ってしまう)。Shift はスキャンコードで、Ctrl と Alt は拡張キーの
///          ビットで左右を分ける。
[[nodiscard]] constexpr int sideOfModifierKey(unsigned vk, long long lParam) noexcept
{
	constexpr unsigned kVkShift = 0x10, kVkControl = 0x11, kVkMenu = 0x12;
	constexpr unsigned kScanRightShift = 0x36;
	const unsigned scan     = static_cast<unsigned>((lParam >> 16) & 0xFF);
	const bool     extended = ((lParam >> 24) & 1) != 0;
	switch (vk)
	{
	case kVkShift:   return scan == kScanRightShift ? 0xA1 : 0xA0;
	case kVkControl: return extended ? 0xA3 : 0xA2;
	case kVkMenu:    return extended ? 0xA5 : 0xA4;
	default:         return 0;
	}
}

/// @brief 左右の付いたキーの、もう片方
[[nodiscard]] constexpr int otherSideOf(int sideKey) noexcept
{
	return (sideKey & 1) != 0 ? sideKey - 1 : sideKey + 1;
}

}  // namespace mitiru::platform
