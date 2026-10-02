#pragma once

/// @file AttackTokens.hpp
/// @brief 同時に攻撃できる敵を制限する攻撃トークン。標的ごとに 1 つ GameMemory に置く固定長の POD。
/// @details tryAcquire は呼んだ順に取得する。request と arbitrate は tick ごとの希望を集め、点数の高い順に
///          割り当てるので、敵の更新順に依らない。同点なら id の小さい敵を優先する。

#include <cstdint>
#include <type_traits>

namespace mitiru::gameai
{

/// @tparam Holders 同時に保持できる数の上限。実際の上限は capacity で決める
/// @tparam Requests 1 tick に受け付ける希望の上限。超えた分は点数の低い希望から捨てる
template <int Holders = 4, int Requests = 16>
struct AttackTokenPool
{
	std::uint32_t holder[Holders]{};      ///< 持っている敵の id (0 は空き)
	std::uint32_t cost[Holders]{};
	std::uint32_t since[Holders]{};       ///< 取った tick
	std::uint32_t requester[Requests]{};
	std::uint32_t requestCost[Requests]{};
	float         requestScore[Requests]{};
	std::int32_t  capacity     = 2;       ///< 同時に使える量 (cost の合計)
	std::int32_t  requestCount = 0;

	[[nodiscard]] constexpr bool holds(std::uint32_t id) const noexcept { return slotOf(id) >= 0; }

	[[nodiscard]] constexpr std::int32_t used() const noexcept
	{
		std::int32_t sum = 0;
		for (int i = 0; i < Holders; ++i) { sum += holder[i] != 0u ? static_cast<std::int32_t>(cost[i]) : 0; }
		return sum;
	}

	[[nodiscard]] constexpr int holderCount() const noexcept
	{
		int n = 0;
		for (int i = 0; i < Holders; ++i) { n += holder[i] != 0u ? 1 : 0; }
		return n;
	}

	/// @brief 空きがあれば取得する。すでに保持している場合も true を返す
	constexpr bool tryAcquire(std::uint32_t id, std::uint32_t now, std::uint32_t tokenCost = 1) noexcept
	{
		if (id == 0u) { return false; }
		if (holds(id)) { return true; }
		const int free = freeSlot();
		if (free < 0 || used() + static_cast<std::int32_t>(tokenCost) > capacity) { return false; }
		holder[free] = id;
		cost[free]   = tokenCost;
		since[free]  = now;
		return true;
	}

	constexpr void release(std::uint32_t id) noexcept
	{
		const int s = slotOf(id);
		if (s >= 0) { holder[s] = 0u; cost[s] = 0u; since[s] = 0u; }
	}

	/// @brief maxFrames を超えて保持されたトークンを解放する。敵が倒れて解放されなくても残り続けない
	constexpr void releaseExpired(std::uint32_t now, std::uint32_t maxFrames) noexcept
	{
		for (int i = 0; i < Holders; ++i)
		{
			if (holder[i] != 0u && now - since[i] > maxFrames) { release(holder[i]); }
		}
	}

	/// @brief この tick の希望を出す。同じ id から複数回出された場合は最も高い点数の希望 (点数と cost の組) を残す
	constexpr void request(std::uint32_t id, float score, std::uint32_t tokenCost = 1) noexcept
	{
		if (id == 0u || holds(id)) { return; }
		for (int i = 0; i < requestCount; ++i)
		{
			if (requester[i] != id) { continue; }
			if (score > requestScore[i]) { requestScore[i] = score; requestCost[i] = tokenCost; }
			return;
		}
		int at = requestCount;
		if (requestCount >= Requests)
		{
			at = worstRequest();
			if (!better(score, id, requestScore[at], requester[at])) { return; }
		}
		else { ++requestCount; }
		requester[at] = id; requestCost[at] = tokenCost; requestScore[at] = score;
	}

	/// @brief 希望を点数の高い順に割り当ててから空にする。cost 分の空きがなければ次の希望を見る
	constexpr void arbitrate(std::uint32_t now) noexcept
	{
		bool taken[Requests]{};
		for (int round = 0; round < requestCount; ++round)
		{
			int best = -1;
			for (int i = 0; i < requestCount; ++i)
			{
				if (!taken[i] && (best < 0 || better(requestScore[i], requester[i], requestScore[best], requester[best]))) { best = i; }
			}
			taken[best] = true;
			tryAcquire(requester[best], now, requestCost[best]);
		}
		requestCount = 0;
	}

private:
	[[nodiscard]] static constexpr bool better(float s, std::uint32_t id, float os, std::uint32_t oid) noexcept
	{
		return s > os || (s == os && id < oid);
	}
	[[nodiscard]] constexpr int slotOf(std::uint32_t id) const noexcept
	{
		for (int i = 0; i < Holders; ++i) { if (id != 0u && holder[i] == id) { return i; } }
		return -1;
	}
	[[nodiscard]] constexpr int freeSlot() const noexcept
	{
		for (int i = 0; i < Holders; ++i) { if (holder[i] == 0u) { return i; } }
		return -1;
	}
	[[nodiscard]] constexpr int worstRequest() const noexcept
	{
		int w = 0;
		for (int i = 1; i < requestCount; ++i)
		{
			if (better(requestScore[w], requester[w], requestScore[i], requester[i])) { w = i; }
		}
		return w;
	}
};

static_assert(std::is_trivially_copyable_v<AttackTokenPool<>>);

} // namespace mitiru::gameai
