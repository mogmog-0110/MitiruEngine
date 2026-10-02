#pragma once

/// @file GlyphSlotAtlas.hpp
/// @brief 同じ大きさの升へ字形を割り当てる表。満杯になると最も長く使われていない升を空ける。画素は持たない。

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace mitiru::text
{

class GlyphSlotAtlas
{
public:
	static constexpr std::uint64_t kNoKey = std::numeric_limits<std::uint64_t>::max();

	GlyphSlotAtlas(int width, int height, int cell)
		: m_cell(cell)
		, m_columns(cell > 0 ? width / cell : 0)
		, m_rows(cell > 0 ? height / cell : 0)
		, m_slots(static_cast<std::size_t>(m_columns) * m_rows)
	{
		m_lookup.reserve(m_slots.size());
	}

	/// @brief 使用中の升番号。無ければ -1。見つかった升は使用時刻を更新する。
	[[nodiscard]] int find(std::uint64_t key) noexcept
	{
		const auto it = m_lookup.find(key);
		if (it == m_lookup.end()) { return -1; }
		m_slots[static_cast<std::size_t>(it->second)].lastUse = m_clock;
		return it->second;
	}

	/// @brief key に升を割り当てる。満杯なら最も長く使われていない升を空ける。
	/// @return 升番号。升が 0 個なら -1。
	int allocate(std::uint64_t key)
	{
		if (m_slots.empty()) { return -1; }
		int slot = -1;
		if (m_used < static_cast<int>(m_slots.size()))
		{
			slot = m_used++;
		}
		else
		{
			slot = leastRecentlyUsed();
			Slot& victim = m_slots[static_cast<std::size_t>(slot)];
			if (victim.lastUse == m_clock) { ++m_evictedInUse; }
			m_lookup.erase(victim.key);
			++m_evictions;
		}
		Slot& s = m_slots[static_cast<std::size_t>(slot)];
		s.key = key;
		s.lastUse = m_clock;
		m_lookup[key] = slot;
		return slot;
	}

	/// @brief 時刻を 1 進める。同じ時刻に使った升は取り上げ候補の最後に回る。
	void tick() noexcept { ++m_clock; }

	[[nodiscard]] int slotX(int slot) const noexcept { return (slot % m_columns) * m_cell; }
	[[nodiscard]] int slotY(int slot) const noexcept { return (slot / m_columns) * m_cell; }
	[[nodiscard]] int cell() const noexcept { return m_cell; }
	[[nodiscard]] int capacity() const noexcept { return static_cast<int>(m_slots.size()); }
	[[nodiscard]] int size() const noexcept { return m_used; }
	[[nodiscard]] std::uint64_t evictions() const noexcept { return m_evictions; }
	/// @brief 同じ時刻に使った升を取り上げた回数。同じ時刻の字形数が升数を超えた印。
	[[nodiscard]] std::uint64_t evictedInUse() const noexcept { return m_evictedInUse; }

private:
	struct Slot
	{
		std::uint64_t key = kNoKey;
		std::uint64_t lastUse = 0;
	};

	[[nodiscard]] int leastRecentlyUsed() const noexcept
	{
		int best = 0;
		for (int i = 1; i < static_cast<int>(m_slots.size()); ++i)
		{
			if (m_slots[static_cast<std::size_t>(i)].lastUse < m_slots[static_cast<std::size_t>(best)].lastUse)
			{
				best = i;
			}
		}
		return best;
	}

	int m_cell = 0;
	int m_columns = 0;
	int m_rows = 0;
	int m_used = 0;
	std::uint64_t m_clock = 1;
	std::uint64_t m_evictions = 0;
	std::uint64_t m_evictedInUse = 0;
	std::vector<Slot> m_slots;
	std::unordered_map<std::uint64_t, int> m_lookup;
};

} // namespace mitiru::text
