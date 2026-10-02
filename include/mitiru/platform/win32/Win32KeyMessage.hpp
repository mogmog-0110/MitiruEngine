#pragma once

/// @file Win32KeyMessage.hpp
/// @brief 窓が受けたキーボードのメッセージを、受けた時の修飾キーの状態と一緒に 1 フレーム分ためる
/// @details UI (RmlUi) へは、ゲームが読む InputState (押しているかどうか) ではなく、
///          WM_KEYDOWN / WM_CHAR をそのままの順で渡す必要がある。文字はキーの状態からは決まらない
///          (Shift、キーボード配列、IME で変わる) ため。Windows.h に依存しないので、RmlUi 無しの
///          ビルドでもこの型だけは見える。

#include <cstddef>
#include <cstdint>
#include <span>

namespace mitiru::platform
{

/// WM_* の値。Windows.h を引かずに使うために写してある (Win32Window で static_assert している)。
inline constexpr std::uint32_t kWmKeyDown    = 0x0100;
inline constexpr std::uint32_t kWmKeyUp      = 0x0101;
inline constexpr std::uint32_t kWmChar       = 0x0102;
inline constexpr std::uint32_t kWmSysKeyDown = 0x0104;
inline constexpr std::uint32_t kWmSysKeyUp   = 0x0105;
inline constexpr std::uint32_t kWmSysChar    = 0x0106;

/// メッセージを受けた瞬間の修飾キー。取り出す時に GetKeyState を読むと、同じフレームの
/// 後のキーの状態が混ざる。
namespace keystate
{
inline constexpr std::uint32_t kShift    = 1u << 0;
inline constexpr std::uint32_t kControl  = 1u << 1;
inline constexpr std::uint32_t kAlt      = 1u << 2;
inline constexpr std::uint32_t kCapsLock = 1u << 3;
inline constexpr std::uint32_t kNumLock  = 1u << 4;
/// この文字は AltGr (右 Alt = Ctrl+Alt) で打たれた。Ctrl+Alt のまま渡すとショートカット扱いになる
inline constexpr std::uint32_t kAltGr    = 1u << 5;
}  // namespace keystate

struct Win32KeyMessage
{
	std::uint32_t message  = 0;  ///< kWm* のどれか
	std::uint32_t wParam   = 0;  ///< キーなら仮想キーコード、文字なら UTF-16 の 1 単位
	std::int32_t  lParam   = 0;  ///< キーストロークの情報 (スキャンコード、拡張キー、リピート)
	std::uint32_t keyState = 0;  ///< keystate::k*
};

/// @brief 1 フレーム分のキーボードのメッセージ
/// @details 窓のメッセージを汲む間に積み、読む側が take で受け取る。次の汲み出しの頭で空にするので、
///          誰も読まないフレームの分が後のフレームへ持ち越されることはない。容量を超えた分は捨てる
///          (メッセージ処理の中で確保しないため)。
class Win32KeyMessageQueue
{
public:
	static constexpr std::size_t kCapacity = 256;

	void beginPump() noexcept
	{
		m_count = 0;
		m_taken = 0;
	}

	void push(const Win32KeyMessage& m) noexcept
	{
		if (m_count < kCapacity)
		{
			m_items[m_count++] = m;
		}
	}

	/// IME が確定した文字列を、1 単位ずつ WM_CHAR として積む。サロゲートペアは 2 件になる
	void pushImeCommit(std::span<const char16_t> text) noexcept
	{
		for (const char16_t unit : text)
		{
			push(Win32KeyMessage{kWmChar, static_cast<std::uint32_t>(unit), 0, 0});
		}
	}

	/// まだ受け取っていない分を返す。中身は次の beginPump まで有効
	[[nodiscard]] std::span<const Win32KeyMessage> take() noexcept
	{
		const std::span<const Win32KeyMessage> out(m_items + m_taken, m_count - m_taken);
		m_taken = m_count;
		return out;
	}

private:
	Win32KeyMessage m_items[kCapacity] = {};
	std::size_t     m_count = 0;
	std::size_t     m_taken = 0;
};

}  // namespace mitiru::platform
