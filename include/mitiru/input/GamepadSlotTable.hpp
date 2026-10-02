#pragma once

/// @file GamepadSlotTable.hpp
/// @brief 接続したパッドを InputSnapshot::gamepads[4] のどの枠に置くかを決める表。
/// @details 新しいパッドは空いている一番若い枠に入り、抜けるまで同じ枠に居る。1 台抜いても他の人の枠は動かない。
///          満員の時に来たパッドは表に載せない。枠が空いた時に、呼び出し側がつながっているパッドを数え直して入れる。

#include <array>
#include <cstdint>

#include <mitiru/input/GamepadSlots.hpp>

namespace mitiru::input
{

class GamepadSlotTable
{
public:
	/// SDL_JoystickID は 0 を使わないので、0 を空きの印にできる
	static constexpr std::uint32_t kEmpty = 0;

	/// @return 入った枠。満員なら -1
	int add(std::uint32_t id) noexcept
	{
		if (id == kEmpty) { return -1; }
		if (const int s = slotOf(id); s >= 0) { return s; }
		for (int i = 0; i < kGamepadSlots; ++i)
		{
			if (m_slots[static_cast<std::size_t>(i)] == kEmpty)
			{
				m_slots[static_cast<std::size_t>(i)] = id;
				return i;
			}
		}
		return -1;
	}

	/// @return 空いた枠。持っていない id なら -1
	int remove(std::uint32_t id) noexcept
	{
		const int s = slotOf(id);
		if (s >= 0) { m_slots[static_cast<std::size_t>(s)] = kEmpty; }
		return s;
	}

	[[nodiscard]] int slotOf(std::uint32_t id) const noexcept
	{
		if (id == kEmpty) { return -1; }
		for (int i = 0; i < kGamepadSlots; ++i)
		{
			if (m_slots[static_cast<std::size_t>(i)] == id) { return i; }
		}
		return -1;
	}

	[[nodiscard]] std::uint32_t idAt(int slot) const noexcept
	{
		return (slot >= 0 && slot < kGamepadSlots) ? m_slots[static_cast<std::size_t>(slot)] : kEmpty;
	}

	[[nodiscard]] int count() const noexcept
	{
		int n = 0;
		for (const std::uint32_t id : m_slots) { n += id != kEmpty ? 1 : 0; }
		return n;
	}

	void clear() noexcept { m_slots.fill(kEmpty); }

private:
	std::array<std::uint32_t, kGamepadSlots> m_slots{};
};

}  // namespace mitiru::input
