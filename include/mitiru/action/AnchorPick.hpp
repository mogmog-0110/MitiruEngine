#pragma once

/// @file AnchorPick.hpp
/// @brief フックを掛ける点の選び方 (GAME_REQUESTS #90)。狙いの円錐の中で、見通しがあり、点数が最小の点を選ぶ。
/// @details 候補の点 (壁の縁、足場、アンカー、レールの上の点など) は呼ぶ側が作る。ここは「どれを選ぶか」だけを持つ。
///          SoftLock.hpp は up に垂直な面で測って高さを見ないので、縁や足場の点を選ぶ用途には使えない。こちらは 3D の角度で測る。
///          射程と見通しは体 (body) から、角度は目 (eye) と狙いの向きから測る。点数が同じなら id が小さい方、それも同じなら添字が小さい方。

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>
#include <mitiru/action/CollisionWorld.hpp>

namespace mitiru::action
{

struct AnchorCandidate
{
	Vec3  point{};
	float radius   = 0.0f;   ///< 大きさのある的の半径。見かけの半径ぶん角度を縮め、見通しの余裕も増やす
	float bonusDeg = 0.0f;   ///< 点数から引く角度。種類ごとの優先、前に選んだ点への粘りなどをまとめて渡す
	float extraDeg = 0.0f;   ///< 点数へ足す角度。高さや進行方向の好みなどをまとめて渡す
	float coneDeg  = -1.0f;  ///< この点だけの円錐の半角。負なら AnchorParams::coneDeg
	float minRange = -1.0f;  ///< この点だけの射程の下限。負なら AnchorParams::minRange
	float maxRange = -1.0f;  ///< この点だけの射程の上限。負なら AnchorParams::maxRange
	std::uint32_t id = 0;
};

struct AnchorAim
{
	Vec3 eye{};
	Vec3 dir{0.0f, 0.0f, 1.0f};  ///< 狙いの向き (単位ベクトル)
	Vec3 body{};                 ///< 射程と見通しの起点
};

struct AnchorParams
{
	float coneDeg     = 30.0f;
	float minRange    = 0.0f;
	float maxRange    = 20.0f;
	float distanceDeg = 0.0f;       ///< 体からの遠さ (この maxRange で割った 0 から 1。候補ごとの maxRange は使わない) 1 あたりに足す角度
	float sightMargin = 0.1f;       ///< 見通しの線が点の手前で止まってよい余裕 (m)
	float maxScore    = FLT_MAX;    ///< これより大きい点数の点は選ばない。0 にすると、狙いの中心より内側の点だけが通る
	int   sightTests  = 6;          ///< 点数の小さい順に見通しを調べる数の上限。0 以下で全部
	std::uint32_t mask = kAllLayers;
};

struct AnchorPick
{
	int   index = -1;       ///< 選んだ候補の添字。選べなければ -1
	float score = 0.0f;
	float angle = 0.0f;     ///< 狙いの中心からのずれ (ラジアン)
};

static_assert(std::is_trivially_copyable_v<AnchorCandidate>);
static_assert(std::is_trivially_copyable_v<AnchorPick>);

/// @brief 線分 ab の上で、origin から dir へのレイにいちばん近い点。レイの後ろ側に寄ったときは、origin にいちばん近い点
[[nodiscard]] inline Vec3 closestPointOnSegmentToRay(const Vec3& a, const Vec3& b, const Vec3& origin,
                                                     const Vec3& dir) noexcept
{
	const Vec3  u   = b - a;
	const Vec3  w   = a - origin;
	const float uu  = dot(u, u);
	const float ud  = dot(u, dir);
	const float uw  = dot(u, w);
	const float dw  = dot(dir, w);
	const float den = uu - ud * ud;
	float s = den > 1e-6f ? (ud * dw - uw) / den : 0.0f;
	s = clampf(s, 0.0f, 1.0f);
	if (dot(a + u * s - origin, dir) < 0.0f && uu > 1e-6f) { s = clampf(-uw / uu, 0.0f, 1.0f); }
	return a + u * s;
}

[[nodiscard]] inline Vec3 closestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p) noexcept
{
	const Vec3  u  = b - a;
	const float uu = dot(u, u);
	return uu > 1e-6f ? a + u * clampf(dot(p - a, u) / uu, 0.0f, 1.0f) : a;
}

/// @brief 線分 ab の上の点 p が origin から range より遠いとき、線分の origin に近い点から p へ向かう線の上で、range の中へ入る点まで引き戻す
/// @details 遠い縁の根元だけが届くとき、根元ではなくそこへ届く最も狙いに近い点を返すため。届かない線分は p のまま。slack は縁ぎりぎりを避ける余裕
[[nodiscard]] inline Vec3 pullPointIntoRange(const Vec3& a, const Vec3& b, const Vec3& p, const Vec3& origin, float range,
                                             float slack = 0.05f) noexcept
{
	if (lengthSq(p - origin) <= range * range) { return p; }
	const float pullRange = range - slack;
	const Vec3  u  = b - a;
	const float uu = dot(u, u);
	if (uu < 1e-6f) { return p; }
	const Vec3 q = a + u * saturate(dot(origin - a, u) / uu);
	const Vec3 d = p - q;
	const Vec3 w = q - origin;
	const float qa   = dot(d, d);
	const float qb   = 2.0f * dot(w, d);
	const float qc   = dot(w, w) - pullRange * pullRange;
	const float disc = qb * qb - 4.0f * qa * qc;
	if (qa < 1e-6f || qc > 0.0f || disc < 0.0f) { return p; }
	return q + d * saturate((-qb + std::sqrt(disc)) / (2.0f * qa));
}

/// @brief from から p までの間に地形が無い (点の手前 margin までは許す)
[[nodiscard]] inline bool hasLineOfSight(const CollisionWorld& world, const Vec3& from, const Vec3& p, float margin,
                                         std::uint32_t mask = kAllLayers)
{
	const Vec3  to   = p - from;
	const float dist = length(to);
	if (dist < margin) { return true; }
	const RayHit hit = world.raycast(from, to * (1.0f / dist), dist, mask);
	return !hit.hit || hit.distance >= dist - margin;
}

namespace anchor_detail
{

struct Key
{
	float         score = 0.0f;
	float         angle = 0.0f;
	std::uint32_t id    = 0;
	int           index = -1;
};

[[nodiscard]] inline bool before(const Key& a, const Key& b) noexcept
{
	if (a.score != b.score) { return a.score < b.score; }
	if (a.id != b.id) { return a.id < b.id; }
	return a.index < b.index;
}

/// @brief 円錐と射程を通った候補の点数。通らなければ false
[[nodiscard]] inline bool scoreOf(const AnchorAim& aim, const AnchorParams& p, const AnchorCandidate& c, int index,
                                  Key& out) noexcept
{
	const float minRange = c.minRange >= 0.0f ? c.minRange : p.minRange;
	const float maxRange = c.maxRange >= 0.0f ? c.maxRange : p.maxRange;
	const float reach    = length(c.point - aim.body);
	if (!(reach >= minRange && reach <= maxRange)) { return false; }
	const Vec3  toPoint = normalizeOr(c.point - aim.eye, aim.dir);
	float       angle   = std::acos(clampf(dot(toPoint, aim.dir), -1.0f, 1.0f));
	if (c.radius > 0.0f) { angle = maxf(0.0f, angle - std::atan2(c.radius, length(c.point - aim.eye))); }
	const float cone = c.coneDeg >= 0.0f ? c.coneDeg : p.coneDeg;
	if (angle > cone * kDegToRad) { return false; }
	float score = angle - c.bonusDeg * kDegToRad;
	const float farShare = p.maxRange > 0.0f ? saturate(reach / p.maxRange) : 0.0f;
	score += (p.distanceDeg * farShare + c.extraDeg) * kDegToRad;
	if (!std::isfinite(score)) { return false; }
	out = {score, angle, c.id, index};
	return true;
}

}  // namespace anchor_detail

/// @brief 候補のうち、円錐と射程を通り、点数の小さい順に見通しを調べて最初に通った点を選ぶ
/// @details 点数は「角度 − bonusDeg + distanceDeg × 遠さ + extraDeg」(角度はラジアン、そのほかは度から直す)。
///          見通しを調べるのは点数の小さい順に sightTests 点までで、そこで見つからなければ選ばない (index -1)。
///          maxScore を超える点は調べずに打ち切る。同じ入力からは、候補の並びが同じなら同じ点が選ばれる
[[nodiscard]] inline AnchorPick pickAnchor(const CollisionWorld& world, const AnchorAim& aim,
                                           std::span<const AnchorCandidate> candidates, const AnchorParams& p = {})
{
	AnchorPick     result;
	const int      count = static_cast<int>(candidates.size());
	const int      limit = p.sightTests > 0 ? p.sightTests : count;
	anchor_detail::Key prev{};
	bool               hasPrev = false;
	for (int tested = 0; tested < limit; ++tested)
	{
		anchor_detail::Key next{};
		bool               found = false;
		for (int i = 0; i < count; ++i)
		{
			anchor_detail::Key k{};
			if (!anchor_detail::scoreOf(aim, p, candidates[static_cast<std::size_t>(i)], i, k)) { continue; }
			if (hasPrev && !anchor_detail::before(prev, k)) { continue; }
			if (!found || anchor_detail::before(k, next)) { next = k; found = true; }
		}
		if (!found || next.score > p.maxScore) { break; }
		const AnchorCandidate& c      = candidates[static_cast<std::size_t>(next.index)];
		const float            margin = p.sightMargin + c.radius;
		if (hasLineOfSight(world, aim.body, c.point, margin, p.mask))
		{
			result = {next.index, next.score, next.angle};
			break;
		}
		prev    = next;
		hasPrev = true;
	}
	return result;
}

}  // namespace mitiru::action
