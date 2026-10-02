#pragma once

/// @file Avoidance.hpp
/// @brief 敵どうしの衝突を避ける速度を ORCA で選ぶ。地面は XZ 平面とみなす。
/// @details 近い相手ごとに timeHorizon 秒間衝突しない速度の範囲を求め、preferred に最も近い速度を線形計画で選ぶ。
///          近い相手は距離順、同じ距離なら番号順に選ぶため、結果は入力の順序と値だけで決まる。
///
///          状態を GameMemory に置ける十数体までの分隊向け。50 体を超える群れは nav/NavCrowd.hpp (DetourCrowd を窓口で
///          巻き戻す) を使う。位置と速度は毎 tick、GameMemory の敵の状態から渡す。壁との衝突はキャラクターの掃引で止める。
///          掃引後に重なった場合は resolveOverlapsXZ で押し離す。

#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>
#include <mitiru/gameai/GroundPlane.hpp>
#include <mitiru/gameai/detail/Orca_impl.hpp>

namespace mitiru::gameai
{

struct AvoidAgent
{
	Vec3  position{};
	Vec3  velocity{};     ///< 直前の tick に実際に動いた速度
	Vec3  preferred{};    ///< 行きたい速度
	float radius   = 0.4f;
	float maxSpeed = 4.0f;
	float yield    = 1.0f;  ///< 避けを受け持つ重み。0 は避けない (プレイヤーなど)。相手の分は相手と重みで分ける
};

struct AvoidConfig
{
	float neighborDist = 4.0f;
	float timeHorizon  = 1.0f;   ///< この秒数先まで当たらない速度を選ぶ
	float dt           = 1.0f / 60.0f;
	int   maxNeighbors = 8;      ///< orca_detail::kMaxLines まで
	float sideBias     = 0.1f;   ///< 望む速度に足す横向きの成分 (速さに対する割合)。近くに相手がいるときだけ、全員が同じ回り方へずれる。向き合ったまま釣り合って止まるのを防ぐ
};

static_assert(std::is_trivially_copyable_v<AvoidAgent>);
static_assert(std::is_trivially_copyable_v<AvoidConfig>);

namespace orca_detail
{

/// @brief self に近い相手を距離順、同じ距離なら番号順に最大 max 体選ぶ
inline int nearestNeighbors(std::span<const AvoidAgent> agents, int self, float range, int max, int* out) noexcept
{
	const Vec3 p = agents[static_cast<std::size_t>(self)].position;
	float dist[kMaxLines]{};
	int   n = 0;
	for (int j = 0; j < static_cast<int>(agents.size()); ++j)
	{
		const Vec3  d  = agents[static_cast<std::size_t>(j)].position - p;
		const float d2 = d.x * d.x + d.z * d.z;
		if (j == self || !(d2 < range * range)) { continue; }
		if (n == max && !(d2 < dist[n - 1])) { continue; }
		int k = n < max ? n++ : max - 1;
		for (; k > 0 && dist[k - 1] > d2; --k) { dist[k] = dist[k - 1]; out[k] = out[k - 1]; }
		dist[k] = d2;
		out[k]  = j;
	}
	return n;
}

[[nodiscard]] constexpr V2 toV2(const Vec3& v) noexcept { return {v.x, v.z}; }

} // namespace orca_detail

/// @brief agents[self] の次の速度。Y は 0
[[nodiscard]] inline Vec3 computeAvoidVelocity(std::span<const AvoidAgent> agents, int self, const AvoidConfig& c) noexcept
{
	namespace od = orca_detail;
	const AvoidAgent& me = agents[static_cast<std::size_t>(self)];
	const int max = c.maxNeighbors < 1 ? 1 : (c.maxNeighbors > od::kMaxLines ? od::kMaxLines : c.maxNeighbors);
	int nearby[od::kMaxLines]{};
	const int n = od::nearestNeighbors(agents, self, c.neighborDist, max, nearby);

	od::Line lines[od::kMaxLines];
	const float invH  = 1.0f / action::maxf(1e-3f, c.timeHorizon);
	const float invDt = 1.0f / action::maxf(1e-5f, c.dt);
	for (int k = 0; k < n; ++k)
	{
		const AvoidAgent& o = agents[static_cast<std::size_t>(nearby[k])];
		const float sum   = me.yield + o.yield;
		const float share = sum > 0.0f ? me.yield / sum : 0.5f;
		lines[k] = od::orcaLine(od::toV2(me.position), od::toV2(me.velocity), od::toV2(o.position), od::toV2(o.velocity),
		                        me.radius + o.radius, share, invH, invDt, self < nearby[k]);
	}
	od::V2 v{};
	const od::V2 pref = od::toV2(me.preferred);
	const od::V2 opt  = n > 0 ? pref + od::V2{pref.y, -pref.x} * c.sideBias : pref;
	const int fail = od::linearProgram2(lines, n, me.maxSpeed, opt, false, v);
	if (fail < n) { od::linearProgram3(lines, n, fail, me.maxSpeed, v); }
	return Vec3{v.x, 0.0f, v.y};
}

/// @brief 重なった円を XZ 平面で番号順に押し離す。weights が 0 のものは動かさない
/// @return 最後の回で見つかった最も深い重なり (m)
inline float resolveOverlapsXZ(std::span<Vec3> positions, std::span<const float> radii, std::span<const float> weights,
                               int iterations = 2) noexcept
{
	const std::size_t n = positions.size();
	float deepest = 0.0f;
	for (int it = 0; it < iterations; ++it)
	{
		deepest = 0.0f;
		for (std::size_t i = 0; i < n; ++i)
		{
			for (std::size_t j = i + 1; j < n; ++j)
			{
				const Vec3  d    = flatten(positions[j] - positions[i]);
				const float dist = action::length(d);
				const float over = radii[i] + radii[j] - dist;
				const float wsum = weights[i] + weights[j];
				if (!(over > 0.0f) || !(wsum > 0.0f)) { continue; }
				deepest = action::maxf(deepest, over);
				const Vec3 dir = dist > 1e-6f ? d * (1.0f / dist) : Vec3{1.0f, 0.0f, 0.0f};
				positions[i] = positions[i] - dir * (over * weights[i] / wsum);
				positions[j] = positions[j] + dir * (over * weights[j] / wsum);
			}
		}
	}
	return deepest;
}

} // namespace mitiru::gameai
