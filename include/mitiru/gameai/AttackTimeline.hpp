#pragma once

/// @file AttackTimeline.hpp
/// @brief 敵の攻撃の時間割。予備動作 (予告)、攻撃判定、硬直を tick の整数で表す。
/// @details AttackTimeline は GameMemory に置き、AttackFrames は技ごとの定数とする。予告の演出は予備動作中に出す。

#include <cstdint>
#include <type_traits>

namespace mitiru::gameai
{

struct AttackFrames
{
	std::uint16_t windup   = 20;  ///< 予備動作 (予告)
	std::uint16_t active   = 6;   ///< 判定が出る
	std::uint16_t recovery = 24;  ///< 硬直 (反撃の隙)
	std::uint16_t pad      = 0;

	[[nodiscard]] constexpr std::uint32_t total() const noexcept
	{
		return static_cast<std::uint32_t>(windup) + active + recovery;
	}
};

enum class AttackPhase : std::uint8_t
{
	None,
	Windup,
	Active,
	Recovery,
	Done,
};

struct AttackTimeline
{
	std::uint32_t start   = 0;
	std::uint16_t attack  = 0;  ///< ゲームが決める技の番号
	std::uint8_t  running = 0;
	std::uint8_t  pad     = 0;

	constexpr void begin(std::uint16_t attackId, std::uint32_t now) noexcept
	{
		attack  = attackId;
		start   = now;
		running = 1;
	}
	constexpr void cancel() noexcept { running = 0; }

	/// @brief 開始した tick を 0 とした経過 tick 数
	[[nodiscard]] constexpr std::uint32_t elapsed(std::uint32_t now) const noexcept { return now - start; }

	[[nodiscard]] constexpr AttackPhase phase(std::uint32_t now, const AttackFrames& f) const noexcept
	{
		if (running == 0) { return AttackPhase::None; }
		const std::uint32_t t = elapsed(now);
		if (t < f.windup) { return AttackPhase::Windup; }
		if (t < static_cast<std::uint32_t>(f.windup) + f.active) { return AttackPhase::Active; }
		if (t < f.total()) { return AttackPhase::Recovery; }
		return AttackPhase::Done;
	}

	/// @brief 指定した区間に入った最初の tick だけ true (判定を出す、音を鳴らす合図に使う)
	[[nodiscard]] constexpr bool entered(std::uint32_t now, const AttackFrames& f, AttackPhase p) const noexcept
	{
		if (running == 0) { return false; }
		const std::uint32_t t = elapsed(now);
		switch (p)
		{
		case AttackPhase::Windup:   return t == 0u;
		case AttackPhase::Active:   return t == f.windup;
		case AttackPhase::Recovery: return t == static_cast<std::uint32_t>(f.windup) + f.active;
		case AttackPhase::Done:     return t == f.total();
		default:                    return false;
		}
	}

	/// @brief 予備動作の進行度。0 から 1。区間外では 0 または 1。
	[[nodiscard]] constexpr float windupProgress(std::uint32_t now, const AttackFrames& f) const noexcept
	{
		if (running == 0 || f.windup == 0) { return running != 0 ? 1.0f : 0.0f; }
		const std::uint32_t t = elapsed(now);
		return t >= f.windup ? 1.0f : static_cast<float>(t) / static_cast<float>(f.windup);
	}
};

static_assert(std::is_trivially_copyable_v<AttackFrames>);
static_assert(std::is_trivially_copyable_v<AttackTimeline>);

} // namespace mitiru::gameai
