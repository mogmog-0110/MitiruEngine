#pragma once

/// @file RawMouse.hpp
/// @brief WM_INPUT のマウスの 1 件から、視点操作に使う相対移動を取り出す判断 (OS を呼ばない)

namespace mitiru::platform
{

/// RAWMOUSE::usFlags の MOUSE_MOVE_ABSOLUTE (Windows.h を読まずに済ませるための写し。Win32Window が static_assert で突き合わせる)
inline constexpr unsigned short kRawMouseMoveAbsolute = 0x01;

struct RawMouseMotion
{
	float dx = 0.0f;
	float dy = 0.0f;
	bool usable = false;   ///< false なら dx, dy は 0。呼ぶ側は従来の経路 (カーソルの戻し) に任せる
};

/// @brief 生のマウスが「相対移動の機器」か「絶対座標の機器」かを見分け、絶対座標しか来ない環境では従来の経路へ落とす
/// @details リモートデスクトップや一部の仮想マシンは絶対座標でしか届けず、その座標から視点の差分は作れない。
///          相対の機器 (本物のマウス) が一度でも来ていれば、同時に繋がったペンタブレットなどの絶対座標は
///          読み捨てるだけにして、落とさない。落とすのはプロセスの間ずっと (機器が替わるたびに往復しない)。
class RawMouseSelector
{
public:
	[[nodiscard]] RawMouseMotion onPacket(unsigned short flags, long lastX, long lastY) noexcept
	{
		if ((flags & kRawMouseMoveAbsolute) != 0)
		{
			++m_absoluteCount;
			return {};
		}
		m_relativeSeen = true;
		return { static_cast<float>(lastX), static_cast<float>(lastY), true };
	}

	/// 絶対座標の機器しか来ていない。生の入力をやめ、カーソルの戻しの経路で差分を取る
	[[nodiscard]] bool fellBack() const noexcept { return !m_relativeSeen && m_absoluteCount >= kFallbackAfterAbsolute; }

private:
	// ペンが先に触れただけで本物のマウスの生の入力を捨てないよう、数件は様子を見る
	static constexpr int kFallbackAfterAbsolute = 4;

	bool m_relativeSeen = false;
	int m_absoluteCount = 0;
};

} // namespace mitiru::platform
