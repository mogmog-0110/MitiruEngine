#pragma once

/// @file ImeVirtualKey.hpp
/// @brief IME が ON のときに WM_KEYDOWN で届く VK_PROCESSKEY を元のキーへ戻す

namespace mitiru::platform
{

/// @param vk            WM_KEYDOWN / WM_KEYUP の wParam
/// @param immVirtualKey vk が VK_PROCESSKEY のときだけ意味を持つ ImmGetVirtualKey の値
/// @return 押下として記録するキー。0 は「記録しない」
/// @details 日本語 IME の変換中は押したキーが VK_PROCESSKEY に化けて届くが、離した側は元のキーで届く。
///          化けたまま記録すると、ゲームからは押したキーが見えず、229 だけが押しっぱなしになる。
///          元のキーが分からないときは、押しっぱなしを作らないよう捨てる。
[[nodiscard]] constexpr int resolveImeVirtualKey(unsigned vk, unsigned immVirtualKey) noexcept
{
	constexpr unsigned kVkProcessKey = 0xE5;
	if (vk != kVkProcessKey)
	{
		return static_cast<int>(vk);
	}
	if (immVirtualKey == 0 || immVirtualKey == kVkProcessKey)
	{
		return 0;
	}
	return static_cast<int>(immVirtualKey);
}

}  // namespace mitiru::platform
