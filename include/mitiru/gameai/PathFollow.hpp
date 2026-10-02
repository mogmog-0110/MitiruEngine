#pragma once

/// @file PathFollow.hpp
/// @brief 経路の角をたどる。NavPath.hpp の経路から角の列を写して GameMemory に置く。手で置いた角の列にも使える。
/// @details 経路上を lookAhead だけ進めた先導点へ向かい、角での急旋回を抑える。skipVisibleCorners は球を掃引し、
///          壁に当たらずに届く角まで飛ばす。床の穴は調べないため、穴や崖がある場所では
///          NavPath.hpp の、ナビメッシュの raycast も見る版を使う。
///          地面は XZ 平面とし、返す速度の Y は 0。

#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/action/CollisionWorld.hpp>
#include <mitiru/gameai/GroundPlane.hpp>

namespace mitiru::gameai
{

enum class PathState : std::uint8_t
{
	None,
	Following,
	Arrived,
	Partial,   ///< 終点に届かない経路の端まで来た
	Failed,
};

struct FollowConfig
{
	float speed        = 3.5f;
	float cornerRadius = 0.35f;  ///< 角にこれだけ近づいたら次の角へ
	float lookAhead    = 1.2f;   ///< 先導点を経路の上でどれだけ先に置くか
	float arriveRadius = 0.3f;   ///< 終点にこれだけ近づいたら着いた
	float slowRadius   = 1.2f;   ///< 終点のこの距離から減速する
};

template <int N = 32>
struct PathFollower
{
	Vec3          corners[N]{};
	Vec3          goal{};           ///< 頼んだ終点 (届かない経路でも頼んだ値)
	std::int32_t  count = 0;
	std::int32_t  next  = 0;        ///< 次に向かう角
	std::uint32_t requestedFrame = 0;
	PathState     state = PathState::None;
	std::uint8_t  partial = 0;      ///< 終点まで届かない経路
	std::uint8_t  pad[2]{};

	void clear() noexcept { count = 0; next = 0; state = PathState::None; partial = 0; }

	/// @brief 角の列を写す。points[0] は現在地として飛ばす
	void set(std::span<const Vec3> points, const Vec3& requestedGoal, std::uint32_t now, bool reachesGoal) noexcept
	{
		count = static_cast<std::int32_t>(points.size() < N ? points.size() : N);
		for (std::int32_t i = 0; i < count; ++i) { corners[i] = points[static_cast<std::size_t>(i)]; }
		next  = count > 1 ? 1 : 0;
		goal  = requestedGoal;
		requestedFrame = now;
		partial = reachesGoal ? 0 : 1;
		state = count > 0 ? PathState::Following : PathState::Failed;
	}

	/// @brief 終点が threshold を超えて動いたか、interval tick が経過したときに true を返す
	[[nodiscard]] bool shouldRepath(const Vec3& newGoal, std::uint32_t now, float threshold, std::uint32_t interval) const noexcept
	{
		if (state == PathState::None || state == PathState::Failed) { return true; }
		const Vec3 d{newGoal.x - goal.x, 0.0f, newGoal.z - goal.z};
		return action::lengthSq(d) > threshold * threshold || now - requestedFrame >= interval;
	}
};

namespace path_detail
{

/// @brief XZ 平面で、線分 a-b 上の p に最も近い点を割合で返す
[[nodiscard]] inline float segmentT(const Vec3& a, const Vec3& b, const Vec3& p) noexcept
{
	const float ex = b.x - a.x, ez = b.z - a.z;
	const float l2 = ex * ex + ez * ez;
	if (!(l2 > 1e-12f)) { return 1.0f; }
	return ((p.x - a.x) * ex + (p.z - a.z) * ez) / l2;
}

} // namespace path_detail

/// @brief 角を進め、先導点へ向かう速度を返す。到着時は 0
template <int N>
[[nodiscard]] Vec3 followPath(PathFollower<N>& f, const Vec3& pos, const FollowConfig& c) noexcept
{
	namespace pd = path_detail;
	if (f.state != PathState::Following || f.count == 0) { return Vec3{}; }
	while (f.next < f.count - 1)
	{
		const Vec3& prev = f.corners[f.next > 0 ? f.next - 1 : 0];
		const Vec3& at   = f.corners[f.next];
		if (distanceXZ(pos, at) > c.cornerRadius && pd::segmentT(prev, at, pos) < 1.0f) { break; }
		++f.next;
	}
	const Vec3& last = f.corners[f.count - 1];
	const float toEnd = distanceXZ(pos, last);
	if (f.next >= f.count - 1 && toEnd <= c.arriveRadius)
	{
		f.state = f.partial != 0 ? PathState::Partial : PathState::Arrived;
		return Vec3{};
	}

	Vec3  carrot = f.corners[f.next];
	float left   = c.lookAhead - distanceXZ(pos, carrot);
	for (std::int32_t i = f.next; i + 1 < f.count && left > 0.0f; ++i)
	{
		const float seg = distanceXZ(f.corners[i], f.corners[i + 1]);
		const float t   = seg > 1e-6f ? action::minf(1.0f, left / seg) : 1.0f;
		carrot = action::lerp(f.corners[i], f.corners[i + 1], t);
		left  -= seg;
	}
	const Vec3  dir   = action::normalizeOr(Vec3{carrot.x - pos.x, 0.0f, carrot.z - pos.z}, Vec3{});
	const float scale = c.slowRadius > 0.0f ? action::saturate(toEnd / c.slowRadius) : 1.0f;
	return dir * (c.speed * action::maxf(scale, 0.25f));
}

/// @brief pos から球を掃引し、当たらずに届く角まで飛ばす。probeHeight は足元からの高さで、膝の高さなら段差に引っかからない
/// @return 飛ばした角の数
template <int N>
int skipVisibleCorners(PathFollower<N>& f, const action::CollisionWorld& world, const Vec3& pos, float probeRadius,
                       float probeHeight, std::uint32_t mask, int maxSkips = 2)
{
	int skipped = 0;
	const Vec3 lift{0.0f, probeHeight, 0.0f};
	while (skipped < maxSkips && f.state == PathState::Following && f.next + 1 < f.count)
	{
		const Vec3 from = pos + lift;
		const Vec3 to   = f.corners[f.next + 1] + lift;
		const action::SweepHit h = world.sphereCast(from, probeRadius, to - from, mask);
		if (h.hit) { break; }
		++f.next;
		++skipped;
	}
	return skipped;
}

static_assert(std::is_trivially_copyable_v<PathFollower<>>);
static_assert(std::is_trivially_copyable_v<FollowConfig>);

} // namespace mitiru::gameai
