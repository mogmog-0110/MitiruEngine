#pragma once

/// @file Perception.hpp
/// @brief 敵の知覚。視界 (距離、視野の円錐、壁による遮蔽)、物音 (半径と時間で弱まる)、最後に知った位置の記憶。
/// @details 壁は mitiru/action/ の CollisionWorld へのレイで判定し、同じフレーム内に結果が出る。
///          NoiseBoard と PerceptionMemory は GameMemory に置く POD。時間は整数の tick で数える。

#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/action/CollisionWorld.hpp>

namespace mitiru::gameai
{

using Vec3 = action::Vec3;

struct SightConfig
{
	float         range        = 15.0f;
	float         halfAngleDeg = 60.0f;  ///< 視野の円錐の半角
	float         nearRange    = 1.5f;   ///< この距離までは後ろでも気づく
	std::uint32_t occluderMask = action::kAllLayers;
};

struct SightResult
{
	float        distance = 0.0f;
	std::uint8_t inRange  = 0;
	std::uint8_t inCone   = 0;
	std::uint8_t visible  = 0;  ///< 距離と円錐に入り、間に壁が無い
	std::uint8_t pad      = 0;
};

/// @brief eye から target が見えるか。forward は正規化しなくてよい。
[[nodiscard]] inline SightResult checkSight(const action::CollisionWorld& world, const Vec3& eye, const Vec3& forward,
                                            const Vec3& target, const SightConfig& c)
{
	SightResult r;
	const Vec3 to = target - eye;
	r.distance = action::length(to);
	r.inRange  = r.distance <= c.range ? 1 : 0;
	if (r.inRange == 0) { return r; }
	const Vec3  dir     = action::normalizeOr(to, Vec3{0.0f, 0.0f, 1.0f});
	const float cosHalf = std::cos(c.halfAngleDeg * action::kDegToRad);
	r.inCone = (r.distance <= c.nearRange || action::dot(dir, action::normalizeOr(forward, dir)) >= cosHalf) ? 1 : 0;
	if (r.inCone == 0) { return r; }
	// 標的の表面に当たらないよう、少し手前で止める
	const float reach = action::maxf(0.0f, r.distance - 0.05f);
	r.visible = world.raycast(eye, dir, reach, c.occluderMask).hit ? 0 : 1;
	return r;
}

/// @brief 1 つの物音。半径の外には届かず、lifetime の間に 0 まで弱まる。
struct NoiseEvent
{
	Vec3          position{};
	float         radius   = 8.0f;
	float         loudness = 1.0f;
	std::uint32_t frame    = 0;
	std::uint32_t source   = 0;  ///< 出したもの (ゲームが決める id)
};

/// @brief 最近の物音を古い順に上書きする輪。
template <int N = 16>
struct NoiseBoard
{
	NoiseEvent    events[N]{};
	std::uint32_t next  = 0;
	std::uint32_t count = 0;

	constexpr void emit(const NoiseEvent& e) noexcept
	{
		events[next] = e;
		next  = (next + 1u) % static_cast<std::uint32_t>(N);
		count = count < static_cast<std::uint32_t>(N) ? count + 1u : count;
	}

	/// @brief 古い順で i 番目。
	[[nodiscard]] constexpr const NoiseEvent& at(std::uint32_t i) const noexcept
	{
		return events[(next + static_cast<std::uint32_t>(N) - count + i) % static_cast<std::uint32_t>(N)];
	}
};

struct HearingConfig
{
	std::uint32_t lifetimeFrames = 30;
	float         occlusion      = 0.4f;  ///< 壁越しの物音に掛ける倍率
	float         threshold      = 0.05f; ///< これ以下は聞こえない
	std::uint32_t occluderMask   = action::kAllLayers;
};

struct HeardNoise
{
	Vec3          position{};
	float         intensity = 0.0f;
	std::uint32_t source    = 0;
	std::uint8_t  heard     = 0;
	std::uint8_t  pad[3]{};
};

/// @brief ear に最も強く届く物音。同じ強さなら新しい方を選ぶ。world が nullptr なら壁で弱めない。
template <int N>
[[nodiscard]] HeardNoise listen(const NoiseBoard<N>& board, const action::CollisionWorld* world, const Vec3& ear,
                                std::uint32_t now, const HearingConfig& c)
{
	HeardNoise best;
	for (std::uint32_t i = 0; i < board.count; ++i)
	{
		const NoiseEvent& e   = board.at(i);
		const std::uint32_t age = now - e.frame;
		const float d = action::length(e.position - ear);
		if (age >= c.lifetimeFrames || !(d < e.radius)) { continue; }
		float v = e.loudness * (1.0f - d / e.radius) * (1.0f - static_cast<float>(age) / static_cast<float>(c.lifetimeFrames));
		if (world != nullptr && d > 1e-4f && world->raycast(ear, e.position - ear, d, c.occluderMask).hit) { v *= c.occlusion; }
		if (v > c.threshold && v >= best.intensity) { best = HeardNoise{e.position, v, e.source, 1, {}}; }
	}
	return best;
}

enum class AwarenessLevel : std::uint8_t
{
	Unaware,
	Suspicious,  ///< 気配はある。最後に知った位置を見に行く
	Alert,       ///< 標的を捉えている
};

struct MemoryConfig
{
	float         seeGainPerSecond = 3.0f;  ///< 近くで見えたときの気づきの増え方。遠いほど半分まで落ちる
	float         hearGain         = 0.6f;  ///< 物音の強さ 1 で足す量
	float         decayPerSecond   = 0.35f;
	float         suspiciousAt     = 0.25f;
	float         alertAt          = 0.9f;
	std::uint32_t holdFrames       = 90;    ///< 最後の手がかりからこの間は気づきを下げない
	std::uint32_t forgetFrames     = 600;   ///< 最後の手がかりからこの間を過ぎると位置を忘れる
};

struct PerceptionMemory
{
	Vec3           lastKnown{};
	float          awareness     = 0.0f;  ///< 0..1
	std::uint32_t  lastSeenFrame = 0;
	std::uint32_t  lastCueFrame  = 0;     ///< 見えたか聞こえた最後の tick
	AwarenessLevel level         = AwarenessLevel::Unaware;
	std::uint8_t   hasLastKnown  = 0;
	std::uint8_t   seenNow       = 0;     ///< この tick に見えた
	std::uint8_t   pad           = 0;
};

static_assert(std::is_trivially_copyable_v<SightConfig>);
static_assert(std::is_trivially_copyable_v<SightResult>);
static_assert(std::is_trivially_copyable_v<NoiseBoard<>>);
static_assert(std::is_trivially_copyable_v<HeardNoise>);
static_assert(std::is_trivially_copyable_v<PerceptionMemory>);

/// @brief 記憶を 1 tick 進める。見えた標的か聞こえた物音の位置を最後に知った位置にする。
inline void updatePerceptionMemory(PerceptionMemory& m, const MemoryConfig& c, const SightResult& sight,
                                   const Vec3& targetPos, float sightRange, const HeardNoise& noise,
                                   std::uint32_t now, float dt) noexcept
{
	m.seenNow = sight.visible;
	if (sight.visible != 0)
	{
		const float near01 = 1.0f - 0.5f * action::saturate(sight.distance / action::maxf(1e-3f, sightRange));
		m.awareness += c.seeGainPerSecond * near01 * dt;
		m.lastKnown = targetPos;
		m.lastSeenFrame = m.lastCueFrame = now;
		m.hasLastKnown = 1;
	}
	else if (noise.heard != 0)
	{
		m.awareness += c.hearGain * noise.intensity;
		m.lastKnown = noise.position;
		m.lastCueFrame = now;
		m.hasLastKnown = 1;
	}
	else if (now - m.lastCueFrame > c.holdFrames)
	{
		m.awareness -= c.decayPerSecond * dt;
	}
	m.awareness = action::saturate(m.awareness);
	if (m.hasLastKnown != 0 && now - m.lastCueFrame > c.forgetFrames) { m.hasLastKnown = 0; }
	// Alert になった後は、気づきが suspiciousAt を下回るまで維持して境目での行き来を防ぐ。
	if (m.awareness >= c.alertAt || (m.level == AwarenessLevel::Alert && m.awareness >= c.suspiciousAt)) { m.level = AwarenessLevel::Alert; }
	else if (m.awareness >= c.suspiciousAt) { m.level = AwarenessLevel::Suspicious; }
	else { m.level = AwarenessLevel::Unaware; }
}

} // namespace mitiru::gameai
