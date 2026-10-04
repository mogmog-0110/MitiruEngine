#pragma once

/// @file Combat.hpp
/// @brief 攻撃判定と食らい判定の突き合わせ、1 振りの多段ヒット除外、ヒットストップと無敵時間。
/// @details 時間はすべて tick (フレーム) の整数で数える。技のフレームデータをそのまま書け、
///          浮動小数の端数で判定がずれない。状態はすべて POD で GameMemory に置く。
///          resolveHits は攻撃判定の並び、食らい判定の並びの順に調べるので、同じ入力なら同じ順のイベントになる。

#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/action/HitVolumes.hpp>

namespace mitiru::action
{

/// @brief 攻撃判定。shape はボーンの座標系の形で、前の tick のボーンの姿勢から今の姿勢までを掃引する。
///        ボーンを持たない弾は、shape を原点の球にして bone に弾の位置を入れる
struct Hitbox
{
	HitVolume     shape{};
	Pose          bone{};
	Pose          previousBone{};
	std::uint32_t owner     = 0;
	std::uint32_t attack    = 0;            ///< resolveHits に渡す除外リストの添字 (攻撃者ごとの振り)
	std::uint32_t hitsTeams = 0xFFFFFFFFu;  ///< 当たる相手のチームのビット
	std::uint32_t tag       = 0;            ///< ゲームが決める (技の種類など)。HitEvent にそのまま返る
	std::uint16_t rehitFrames = 0;          ///< 0 なら 1 振りで同じ相手に 1 回。正なら、この間隔で当たり直す
	std::uint16_t pad = 0;
};

/// @brief 食らい判定 (この tick のワールド座標)
struct Hurtbox
{
	HitVolume     volume{};
	std::uint32_t owner = 0;
	std::uint32_t team  = 1;  ///< チームのビット (1 つ)
	std::uint32_t part  = 0;  ///< 頭や胴など、ゲームが決める部位の番号
	std::uint8_t  invulnerable = 0;
	std::uint8_t  pad[3]{};
};

struct HitEvent
{
	Vec3          point{};
	std::uint32_t attacker = 0;
	std::uint32_t victim   = 0;
	std::uint32_t attack   = 0;
	std::uint32_t tag      = 0;
	std::uint32_t part     = 0;
	std::uint16_t hitbox   = 0;
	std::uint16_t hurtbox  = 0;
};

/// @brief 1 振りで当てた相手の記録。振りを始めるたびに begin で空にする
template <std::size_t N = 16>
struct HitExclusion
{
	std::uint32_t count = 0;
	std::uint32_t victims[N]{};
	std::uint32_t frames[N]{};  ///< 当てた tick

	constexpr void begin() noexcept { count = 0; }

	[[nodiscard]] constexpr bool canHit(std::uint32_t victim, std::uint32_t now, std::uint16_t rehitFrames) const noexcept
	{
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (victims[i] == victim) { return rehitFrames > 0 && now - frames[i] >= rehitFrames; }
		}
		return true;
	}

	/// @brief 満杯なら記録できず false。その相手には次の tick にもう一度当たりうる
	constexpr bool record(std::uint32_t victim, std::uint32_t now) noexcept
	{
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (victims[i] == victim) { frames[i] = now; return true; }
		}
		if (count >= N) { return false; }
		victims[count] = victim;
		frames[count]  = now;
		++count;
		return true;
	}
};

/// @brief 攻撃判定と食らい判定を突き合わせて out に積み、積んだ件数を返す
/// @details 当たらない条件: 食らい判定が無敵、持ち主が同じ、チームが合わない、除外リストにいる。
///          1 振り (attack) は同じ相手に 1 tick に 1 回まで (部位が複数重なっても最初の 1 つ)。
///          out が満杯になったらそこで止める。記録もしないので、残りは次の tick に当たる。
template <std::size_t N>
int resolveHits(std::span<const Hitbox> hitboxes, std::span<const Hurtbox> hurtboxes,
                std::span<HitExclusion<N>> exclusions, std::uint32_t now, HitEvent* out, int capacity) noexcept
{
	int count = 0;
	for (std::size_t i = 0; i < hitboxes.size(); ++i)
	{
		const Hitbox& hb = hitboxes[i];
		if (hb.attack >= exclusions.size()) { continue; }
		HitExclusion<N>& ex = exclusions[hb.attack];
		for (std::size_t j = 0; j < hurtboxes.size(); ++j)
		{
			const Hurtbox& hu = hurtboxes[j];
			if (hu.invulnerable != 0 || hu.owner == hb.owner || (hb.hitsTeams & hu.team) == 0u) { continue; }
			if (!ex.canHit(hu.owner, now, hb.rehitFrames)) { continue; }
			Vec3 point{};
			if (!sweptOverlap(hb.shape, hb.previousBone, hb.bone, hu.volume, &point)) { continue; }
			if (count >= capacity || out == nullptr) { return count; }
			out[count++] = HitEvent{point, hb.owner, hu.owner, hb.attack, hb.tag, hu.part,
			                        static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(j)};
			ex.record(hu.owner, now);
		}
	}
	return count;
}

/// @brief 持ち主ごとのヒットストップ。当てた側と当てられた側だけを止め、ほかは動かし続けるときに使う。
///        画面全体を止めるなら hud.hitStop (update は dt = 0 で呼ばれ続ける)
struct HitStop
{
	std::uint16_t frames  = 0;
	std::uint16_t elapsed = 0;  ///< 止まってからの tick (震えの左右に使う)

	/// @brief 止まっている間に重ねて当たったら、長い方を残す
	constexpr void trigger(std::uint16_t f) noexcept
	{
		if (frames == 0) { elapsed = 0; }
		frames = f > frames ? f : frames;
	}
	[[nodiscard]] constexpr bool active() const noexcept { return frames > 0; }
	/// @brief tick の頭で呼ぶ。この tick を止めるなら true
	constexpr bool consume() noexcept
	{
		if (frames == 0) { return false; }
		--frames;
		++elapsed;
		return true;
	}
	/// @brief 止まっている間の震え (-1 か +1、止まっていなければ 0)。描画の位置に掛ける
	[[nodiscard]] constexpr float shakeSign() const noexcept
	{
		if (frames == 0) { return 0.0f; }
		return (elapsed & 1u) != 0u ? 1.0f : -1.0f;
	}
};

/// @brief 無敵時間 (被弾直後、回避中など)
struct Invulnerability
{
	std::uint16_t frames = 0;
	std::uint16_t pad    = 0;

	constexpr void grant(std::uint16_t f) noexcept { frames = f > frames ? f : frames; }
	[[nodiscard]] constexpr bool active() const noexcept { return frames > 0; }
	constexpr void tick() noexcept { if (frames > 0) { --frames; } }
};

static_assert(std::is_trivially_copyable_v<Hitbox>);
static_assert(std::is_trivially_copyable_v<Hurtbox>);
static_assert(std::is_trivially_copyable_v<HitEvent>);
static_assert(std::is_trivially_copyable_v<HitExclusion<>>);
static_assert(std::is_trivially_copyable_v<HitStop>);
static_assert(std::is_trivially_copyable_v<Invulnerability>);

} // namespace mitiru::action
