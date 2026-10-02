#pragma once

/// @file NavPath.hpp
/// @brief ナビメッシュ (mitiru/nav/) の経路を PathFollower に移す。mitiru_nav をリンクした DLL 向け
/// @details NavQuery::findPath はポリゴン列を Detour の findStraightPath で角だけの折れ線にし、ポリゴンの縁を
///          通る最短経路を返す。固定長の PathFollower に移すため、ヒープは使わない。
///          NavMesh と DynamicNavMesh は、どちらも query() を渡す。

#include <mitiru/gameai/PathFollow.hpp>
#include <mitiru/nav/NavMesh.hpp>
#include <mitiru/nav/NavQuery.hpp>

namespace mitiru::gameai
{

template <int N>
nav::NavPathStatus requestPath(nav::NavQuery& query, PathFollower<N>& f, const Vec3& from, const Vec3& to, std::uint32_t now)
{
	sgc::Vec3f points[N];
	const nav::NavPathResult r = query.findPath(from, to, points);
	const bool usable = r.status == nav::NavPathStatus::Found || r.status == nav::NavPathStatus::Partial;
	if (!usable || r.pointCount == 0)
	{
		f.clear();
		f.goal = to;
		f.requestedFrame = now;
		f.state = PathState::Failed;
		return r.status;
	}
	f.set(std::span<const Vec3>(points, static_cast<std::size_t>(r.pointCount)), to, now, r.status == nav::NavPathStatus::Found);
	return r.status;
}

template <int N>
nav::NavPathStatus requestPath(nav::NavMesh& mesh, PathFollower<N>& f, const Vec3& from, const Vec3& to, std::uint32_t now)
{
	return requestPath(mesh.query(), f, from, to, now);
}

/// @brief skipVisibleCorners にナビメッシュの raycast を加え、掃引では検出できない床の穴や崖の縁を越える角を残す
template <int N>
int skipVisibleCorners(PathFollower<N>& f, const action::CollisionWorld& world, const nav::NavQuery& query, const Vec3& pos,
                       float probeRadius, float probeHeight, std::uint32_t mask, int maxSkips = 2)
{
	int skipped = 0;
	const Vec3 lift{0.0f, probeHeight, 0.0f};
	while (skipped < maxSkips && f.state == PathState::Following && f.next + 1 < f.count)
	{
		const Vec3& corner = f.corners[f.next + 1];
		// 角は面の縁にあるため、角から 5 cm 以内で縁に触れた場合は到達とみなす
		const nav::NavRayHit ray = query.raycast(pos, corner);
		if (!ray.ok || (ray.hit && distanceXZ(ray.point, corner) > 0.05f)) { break; }
		const Vec3 from = pos + lift;
		if (world.sphereCast(from, probeRadius, corner + lift - from, mask).hit) { break; }
		++f.next;
		++skipped;
	}
	return skipped;
}

} // namespace mitiru::gameai
