#pragma once

/// @file InputBuffer.hpp
/// @brief 先行入力 (押した瞬間を覚え、行動ごとの猶予の間なら後から使える) と、技の取り消し窓。
/// @details 時刻は tick の整数。行動の番号 (0-31) の意味はゲームが決める。状態は POD で GameMemory に置く。
///          毎 tick の使い方: 押されたボタンを press で積み、行動を始めるときに consume か tryCancel で消費する。

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace mitiru::action
{

inline constexpr std::uint8_t kNoAction = 0xFFu;

/// @brief 行動ごとの先行入力の猶予 (tick)。押してからこの tick 数までは使える
struct ActionWindows
{
	std::uint8_t frames[32]{};
};

/// @brief 押した記録の輪。いっぱいになると一番古い記録を上書きする
template <std::size_t N = 16>
struct InputBuffer
{
	struct Entry
	{
		std::uint32_t frame    = 0;
		std::uint8_t  action   = kNoAction;
		std::uint8_t  consumed = 0;
		std::uint8_t  pad[2]{};
	};

	Entry         entries[N]{};
	std::uint32_t head  = 0;  ///< 次に書く位置
	std::uint32_t count = 0;

	constexpr void press(std::uint8_t action, std::uint32_t frame) noexcept
	{
		entries[head] = Entry{frame, action, 0, {}};
		head          = (head + 1) % static_cast<std::uint32_t>(N);
		if (count < N) { ++count; }
	}

	/// @brief bit i が立っていれば行動 i を押した、として積む (番号の小さい順)
	constexpr void pressMask(std::uint32_t mask, std::uint32_t frame) noexcept
	{
		for (std::uint8_t a = 0; a < 32; ++a)
		{
			if (((mask >> a) & 1u) != 0u) { press(a, frame); }
		}
	}

	/// @brief 猶予 window の中に、まだ使っていない action の押下があれば、一番古いものを使って true
	constexpr bool consume(std::uint8_t action, std::uint32_t now, std::uint32_t window) noexcept
	{
		const int slot = find(action, now, window);
		if (slot < 0) { return false; }
		entries[slot].consumed = 1;
		return true;
	}

	[[nodiscard]] constexpr bool peek(std::uint8_t action, std::uint32_t now, std::uint32_t window) const noexcept
	{
		return find(action, now, window) >= 0;
	}

	constexpr bool consume(std::uint8_t action, std::uint32_t now, const ActionWindows& windows) noexcept
	{
		return action < 32 && consume(action, now, windows.frames[action]);
	}

	constexpr void clear() noexcept { head = count = 0; }

private:
	/// @brief 古い順に探し、見つかった場所を返す (無ければ -1)
	[[nodiscard]] constexpr int find(std::uint8_t action, std::uint32_t now, std::uint32_t window) const noexcept
	{
		const std::uint32_t start = (head + static_cast<std::uint32_t>(N) - count) % static_cast<std::uint32_t>(N);
		for (std::uint32_t k = 0; k < count; ++k)
		{
			const std::uint32_t slot = (start + k) % static_cast<std::uint32_t>(N);
			const Entry&        e    = entries[slot];
			if (e.action == action && e.consumed == 0 && now >= e.frame && now - e.frame <= window)
			{
				return static_cast<int>(slot);
			}
		}
		return -1;
	}
};

/// @brief 技の [begin, end) tick の間、actions のビットの行動で技を取り消せる
struct CancelWindow
{
	std::uint16_t begin   = 0;
	std::uint16_t end     = 0;
	std::uint32_t actions = 0;
};

/// @brief 今の技の moveFrame で開いている取り消し窓に入っていて、先行入力にある行動を priority の順に探し、
///        見つかれば消費してその行動を返す。無ければ kNoAction
template <std::size_t N>
std::uint8_t tryCancel(InputBuffer<N>& buffer, const ActionWindows& windows, std::uint32_t now,
                       std::uint16_t moveFrame, std::span<const CancelWindow> cancels,
                       std::span<const std::uint8_t> priority) noexcept
{
	std::uint32_t open = 0;
	for (const CancelWindow& w : cancels)
	{
		if (moveFrame >= w.begin && moveFrame < w.end) { open |= w.actions; }
	}
	for (const std::uint8_t a : priority)
	{
		if (a < 32 && ((open >> a) & 1u) != 0u && buffer.consume(a, now, windows)) { return a; }
	}
	return kNoAction;
}

static_assert(std::is_trivially_copyable_v<ActionWindows>);
static_assert(std::is_trivially_copyable_v<InputBuffer<>>);
static_assert(std::is_trivially_copyable_v<CancelWindow>);

} // namespace mitiru::action
