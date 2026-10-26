#pragma once

/// @file InputTriggers.hpp
/// @brief 長押し、タップ、ダブルタップ、同時押し、リピートの判定部品の宣言。
/// @details ゲームが GameMemory に置いて使う平らな POD で、状態は各部品の中だけに持つ。
/// host は状態を持たないため、巻き戻しやセーブでは GameMemory とともに戻り、記録した InputSnapshot からの再生でも同じ結果になる。
/// 時間は秒で渡し、update に渡された実効 dt だけを数える。
/// 一時停止とヒットストップ中は dt が 0 になるため進まない。
/// 実時間を使うと、停止中も時間が進んで再生がずれる。
/// 割り当て後の押下状態 (Input::down) を渡す。
/// スティックの円形デッドゾーンなどの値の整形は、host が InputSnapshot を作る前に済ませ、ここでは掛け直さない。
/// 秒数の既定値は InputTimingSettings にある。
/// 利用者が設定した秒数は ABI v62 の Input::holdSec(fallback) などで読める (0 のときは fallback。host の中身は後続の担当が入れる)。

#include <cstdint>
#include <type_traits>

namespace mitiru::input
{

struct HoldEvents
{
	bool pressed = false;       ///< 押した瞬間
	bool holdReached = false;   ///< 押し続けて長押しの秒数に届いた瞬間 (1 回だけ)
	bool releasedTap = false;   ///< 長押しに届く前に離した
	bool releasedHold = false;  ///< 長押しに届いた後に離した
};

/// @brief 長押し。離したときに、タップか長押しかが分かる。
struct HoldTracker
{
	float heldSec = 0.0f;
	std::uint8_t wasDown = 0;
	std::uint8_t reached = 0;
	std::uint8_t _pad[2] = {};

	/// heldSec は押している秒数を表し、heldSec / holdSec で長押しの進み具合を出せる。
	HoldEvents update(bool down, float dt, float holdSec) noexcept
	{
		HoldEvents e;
		if (down && !wasDown)
		{
			e.pressed = true;
			heldSec = 0.0f;
			reached = 0;
		}
		if (down)
		{
			heldSec += dt;
			if (!reached && heldSec >= holdSec)
			{
				reached = 1;
				e.holdReached = true;
			}
		}
		else if (wasDown)
		{
			if (reached) { e.releasedHold = true; }
			else { e.releasedTap = true; }
			heldSec = 0.0f;
			reached = 0;
		}
		wasDown = down ? 1 : 0;
		return e;
	}

	[[nodiscard]] bool holding() const noexcept { return wasDown != 0 && reached != 0; }
};

struct MultiTapEvents
{
	bool pressed = false;      ///< 押した瞬間
	int tapCount = 0;          ///< この押下が猶予の中で何回目か (pressed の時だけ 1 以上)
	bool multiTap = false;     ///< tapCount が必要な回数に届いた瞬間
	int settledCount = 0;      ///< 猶予が切れて、続けて押された回数が確定した瞬間の回数 (その他は 0)
};

/// @brief 連打。押した瞬間に tapCount を返すため、1 回目の反応は猶予の分だけ遅れない。
/// 1 回だけだったと確定してから反応する操作には settledCount を使うが、その分だけ遅れる。
struct MultiTapTracker
{
	float sinceReleaseSec = 0.0f;   ///< 最後に離してからの秒数
	std::int32_t count = 0;         ///< 猶予の中で押した回数
	std::uint8_t wasDown = 0;
	std::uint8_t _pad[3] = {};

	MultiTapEvents update(bool down, float dt, float windowSec, int requiredTaps = 2) noexcept
	{
		MultiTapEvents e;
		if (down && !wasDown)
		{
			e.pressed = true;
			// 押したフレームの dt も離してからの時間に入れる。入れないと、長いフレームの分だけ猶予が延びる
			if (count > 0) { sinceReleaseSec += dt; }
			count = (count > 0 && sinceReleaseSec <= windowSec) ? count + 1 : 1;
			e.tapCount = count;
			e.multiTap = count == requiredTaps;
		}
		else if (!down && wasDown)
		{
			sinceReleaseSec = 0.0f;
		}
		else if (!down && count > 0)
		{
			sinceReleaseSec += dt;
			if (sinceReleaseSec > windowSec)
			{
				e.settledCount = count;
				count = 0;
			}
		}
		wasDown = down ? 1 : 0;
		return e;
	}
};

struct ChordEvents
{
	bool formed = false;    ///< 全部が揃った瞬間
	bool broken = false;    ///< 揃っていたのが崩れた瞬間
	bool active = false;    ///< 揃っている間
	bool pending = false;   ///< 一部だけ押していて、猶予の中で残りが押されれば chord になる
};

/// @brief 同時押し。downMask の bit i は入力 i の押下を表す。
/// 最初の 1 つを押してから猶予内に requiredMask の全入力が揃うと chord になる。
/// 猶予が切れた後に残りを押しても chord にならず、単独の操作として扱われる。
/// pending の間は単独の操作を確定させずに待てる。
struct ChordTracker
{
	float sinceFirstSec = 0.0f;
	std::uint8_t armed = 0;     ///< 1 つ以上押されて猶予を数えている
	std::uint8_t expired = 0;   ///< 猶予が切れた。全部離すまで chord にならない
	std::uint8_t active = 0;
	std::uint8_t _pad = 0;

	ChordEvents update(std::uint32_t downMask, std::uint32_t requiredMask, float dt, float windowSec) noexcept
	{
		ChordEvents e;
		const std::uint32_t held = downMask & requiredMask;
		const bool allHeld = requiredMask != 0 && held == requiredMask;
		if (held == 0)
		{
			e.broken = active != 0;
			armed = 0;
			expired = 0;
			active = 0;
			sinceFirstSec = 0.0f;
			return e;
		}
		if (!armed)
		{
			armed = 1;
			sinceFirstSec = 0.0f;
		}
		else if (!active)
		{
			sinceFirstSec += dt;
		}
		if (active)
		{
			if (!allHeld)
			{
				e.broken = true;
				active = 0;
				expired = 1;
			}
		}
		else if (allHeld && !expired && sinceFirstSec <= windowSec)
		{
			active = 1;
			e.formed = true;
		}
		else if (!allHeld && sinceFirstSec > windowSec)
		{
			expired = 1;
		}
		e.active = active != 0;
		e.pending = !active && !expired && held != 0 && !allHeld;
		return e;
	}
};

struct RepeatEvents
{
	bool pressed = false;   ///< 押した瞬間
	int pulses = 0;         ///< このフレームに起きた繰り返しの数 (押した瞬間を含む)
};

/// @brief 押し続けたときの繰り返し。最初は delaySec だけ待ち、その後は intervalSec ごとに起きる。
/// dt が長いフレームでは複数回起きるため、回数を pulses で返す。
struct RepeatTracker
{
	float heldSec = 0.0f;
	std::int32_t emitted = 0;   ///< 押した瞬間を除く繰り返しの数
	std::uint8_t wasDown = 0;
	std::uint8_t _pad[3] = {};

	RepeatEvents update(bool down, float dt, float delaySec, float intervalSec) noexcept
	{
		RepeatEvents e;
		if (!down)
		{
			heldSec = 0.0f;
			emitted = 0;
			wasDown = 0;
			return e;
		}
		if (!wasDown)
		{
			e.pressed = true;
			e.pulses = 1;
			heldSec = 0.0f;
			emitted = 0;
		}
		heldSec += dt;
		const float interval = intervalSec > 0.0001f ? intervalSec : 0.0001f;
		if (heldSec >= delaySec)
		{
			// 越えた区間の数を式で求める。積算すると、dt の刻みによって回数が変わる。
			// delaySec が 0 のときの最初の繰り返しは押した瞬間と同じ時刻なので、数えない
			const auto due = static_cast<std::int32_t>((heldSec - delaySec) / interval) + (delaySec > 0.0f ? 1 : 0);
			if (due > emitted)
			{
				e.pulses += due - emitted;
				emitted = due;
			}
		}
		wasDown = 1;
		return e;
	}
};

static_assert(std::is_trivially_copyable_v<HoldTracker> && std::is_standard_layout_v<HoldTracker>);
static_assert(std::is_trivially_copyable_v<MultiTapTracker> && std::is_standard_layout_v<MultiTapTracker>);
static_assert(std::is_trivially_copyable_v<ChordTracker> && std::is_standard_layout_v<ChordTracker>);
static_assert(std::is_trivially_copyable_v<RepeatTracker> && std::is_standard_layout_v<RepeatTracker>);

}  // namespace mitiru::input
