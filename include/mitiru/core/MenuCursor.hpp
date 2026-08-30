#pragma once

/// @file MenuCursor.hpp
/// @brief メニューのスティック/十字ナビ用カーソル POD (エッジ + キーリピート)。
/// @details
/// InputSnapshot は stateless なので「スティックを倒し続けたら一定間隔で送る」が
/// 単体では作れない。この POD を GameMemory に置くとリピートタイマーごと記録・
/// 巻き戻し・リプレイ対象になる (flat POD)。
///
/// @code
///   struct GameMemory { mitiru::MenuCursor cur; };
///   // update 内:
///   if (int mv = mem.cur.updateVertical(in, kItemCount, dt)) {
///       hud.playSound(mv > 0 ? "se/down" : "se/up");   // 動いたフレームだけ ±1
///   }
///   drawHighlight(mem.cur.index);
/// @endcode
///
/// module/Game.hpp がこの header を include する (逆は循環するので不可)。そのため
/// Input 型を名指しせずテンプレートで受ける (duck typing: down / pressed / padDown /
/// padPressed / leftStick を呼ぶ)。Key / Pad は opaque enum 前方宣言 + VK/ビット値の
/// 直書きで参照する (値は Win32 VK / XInput ビット由来の境界 ABI で不変)。
///
/// 左スティック生値は +y=上・+x=右 (エンジン規約 = gamepadAxes)。updateVertical は
/// 内部で Y を符号反転し「スティック上 = カーソル上 (-1)」にする。
/// 1 個の MenuCursor は縦か横どちらか一方を回すこと (タイマー共有のため)。
/// グリッドメニューは縦横 1 個ずつ置く。

#include <cstdint>
#include <type_traits>

namespace mitiru
{

enum class Key : int;            // module/Game.hpp の定義と一致する opaque 宣言
enum class Pad : std::uint32_t;  // 同上

namespace detail::menu
{
// module/Game.hpp の Key / Pad と同値 (Win32 VK / XInput ビット)
inline constexpr int kVkLeft = 0x25, kVkUp = 0x26, kVkRight = 0x27, kVkDown = 0x28;
inline constexpr int kVkW = 'W', kVkA = 'A', kVkS = 'S', kVkD = 'D';
inline constexpr std::uint32_t kPadUp = 0x0001, kPadDown = 0x0002;
inline constexpr std::uint32_t kPadLeft = 0x0004, kPadRight = 0x0008;
inline constexpr float kStickThreshold = 0.5f;  // スティックをエッジ扱いする閾値
}  // namespace detail::menu

/// @brief メニュー選択カーソル。十字/矢印/WASD はエッジ即時、スティックは閾値跨ぎで
///        エッジ + 倒しっぱなしで repeatDelay 後 repeatRate 間隔リピート (長押しキーも同様)。
struct MenuCursor
{
	int   index = 0;        ///< 現在の選択 (0..count-1)
	float repeatT = 0.0f;   ///< リピートタイマー (内部)
	float prevAxisY = 0.0f; ///< 前回の縦軸値 (スティックエッジ検出用、内部)
	float prevAxisX = 0.0f; ///< 同・横軸

	/// @brief 縦メニュー。毎フレーム呼ぶ。index を 0..count-1 で wrap 移動。
	/// @return 動いたフレームは +1 (下) / -1 (上)、それ以外 0 (SE 鳴らし分け用)。
	template <typename InputT>
	int updateVertical(const InputT& in, int count, float dt,
	                   float repeatDelay = 0.4f, float repeatRate = 0.12f)
	{
		namespace dm = detail::menu;
		const float axis = -in.leftStick().y;   // 生値 +y=上 → 内部の +y=下 系へ反転
		const int step = stepOf(in, axis, prevAxisY, dt, repeatDelay, repeatRate,
		                        dm::kVkUp, dm::kVkW, dm::kPadUp,
		                        dm::kVkDown, dm::kVkS, dm::kPadDown);
		prevAxisY = axis;
		return apply(step, count);
	}

	/// @brief 横メニュー (updateVertical と同型。負方向 = 左、正方向 = 右)。
	template <typename InputT>
	int updateHorizontal(const InputT& in, int count, float dt,
	                     float repeatDelay = 0.4f, float repeatRate = 0.12f)
	{
		namespace dm = detail::menu;
		const float axis = in.leftStick().x;
		const int step = stepOf(in, axis, prevAxisX, dt, repeatDelay, repeatRate,
		                        dm::kVkLeft, dm::kVkA, dm::kPadLeft,
		                        dm::kVkRight, dm::kVkD, dm::kPadRight);
		prevAxisX = axis;
		return apply(step, count);
	}

	/// @brief カーソルを idx へ戻し、タイマー/軸履歴も初期化 (メニュー開閉時に呼ぶ)。
	void reset(int idx = 0) noexcept
	{
		index = idx;
		repeatT = 0.0f;
		prevAxisY = 0.0f;
		prevAxisX = 0.0f;
	}

private:
	// 1 方向ぶんの保持判定 (キー 2 つ + パッド十字の OR)
	template <typename InputT>
	static bool dirDown(const InputT& in, int vk1, int vk2, std::uint32_t pad)
	{
		return in.down(static_cast<Key>(vk1)) || in.down(static_cast<Key>(vk2)) ||
		       in.padDown(static_cast<Pad>(pad));
	}
	// 1 方向ぶんのエッジ判定 (押した瞬間)
	template <typename InputT>
	static bool dirPressed(const InputT& in, int vk1, int vk2, std::uint32_t pad)
	{
		return in.pressed(static_cast<Key>(vk1)) || in.pressed(static_cast<Key>(vk2)) ||
		       in.padPressed(static_cast<Pad>(pad));
	}

	/// エッジ + 長押しリピートを単一タイマーで合成し、動く方向 (-1/0/+1) を返す。
	template <typename InputT>
	int stepOf(const InputT& in, float axis, float prevAxis, float dt,
	           float repeatDelay, float repeatRate,
	           int vkNeg1, int vkNeg2, std::uint32_t padNeg,
	           int vkPos1, int vkPos2, std::uint32_t padPos)
	{
		namespace dm = detail::menu;
		const bool negStick = axis < -dm::kStickThreshold;
		const bool posStick = axis > dm::kStickThreshold;
		const bool negStickEdge = negStick && !(prevAxis < -dm::kStickThreshold);
		const bool posStickEdge = posStick && !(prevAxis > dm::kStickThreshold);

		const int edge = ((dirPressed(in, vkPos1, vkPos2, padPos) || posStickEdge) ? 1 : 0)
		               - ((dirPressed(in, vkNeg1, vkNeg2, padNeg) || negStickEdge) ? 1 : 0);
		const int held = ((dirDown(in, vkPos1, vkPos2, padPos) || posStick) ? 1 : 0)
		               - ((dirDown(in, vkNeg1, vkNeg2, padNeg) || negStick) ? 1 : 0);

		if (edge != 0)  // エッジは即時。リピートはここを起点に repeatDelay 後から
		{
			repeatT = 0.0f;
			return edge;
		}
		if (held == 0)  // 離した (or 上下相殺) → タイマー破棄
		{
			repeatT = 0.0f;
			return 0;
		}
		repeatT += dt;  // 長押し: delay 到達で発火 → rate ぶん戻して等間隔継続
		if (repeatT >= repeatDelay)
		{
			repeatT -= repeatRate;
			return held;
		}
		return 0;
	}

	/// step を index に適用 (0..count-1 で wrap)。count 変化で範囲外なら末尾へ寄せる。
	int apply(int step, int count)
	{
		if (count <= 0) { return 0; }
		if (index < 0) { index = 0; }
		else if (index >= count) { index = count - 1; }
		if (step == 0) { return 0; }
		index = ((index + step) % count + count) % count;
		return step;
	}
};

static_assert(std::is_trivially_copyable_v<MenuCursor>,
              "MenuCursor は flat POD (GameMemory に置いて記録・巻き戻し可) であること");

}  // namespace mitiru
