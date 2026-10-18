#pragma once

/// @file HostBoundary55.hpp
/// @brief ABI v55 (ADR 0080) の host 側の小さな関数。Engine を持ち込まずにテストする。使うのは core/detail/Engine_Module_*.hpp

#include <chrono>
#include <cstdint>
#include <random>

namespace mitiru::module::detail
{

/// @brief InputSnapshot::sessionId に入れる値。fixed が 0 でなければそのまま、0 なら起動ごとに違う 0 でない値
[[nodiscard]] inline std::uint32_t pickSessionId(std::uint32_t fixed)
{
	if (fixed != 0) { return fixed; }
	std::random_device rd;
	const auto ticks = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	std::uint32_t id = rd() ^ static_cast<std::uint32_t>(ticks) ^ static_cast<std::uint32_t>(ticks >> 32);
	id = (id ^ (id >> 16)) * 0x45d9f3bu;
	id ^= id >> 16;
	return (id != 0) ? id : 1u;
}

}  // namespace mitiru::module::detail
