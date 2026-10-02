#pragma once

/// @file CrowdAgentImage.hpp
/// @brief dtCrowdAgent 1 体と、ポインタを含まない固定長 bytes の相互変換 (NavCrowd の窓口の中身)
/// @details 次の update に使う値をすべて保存する。dtPathCorridor は、位置、行き先、面の列を公開関数で戻す。
///          dtLocalBoundary は private でコピーできないため、同じ並びの BoundaryMirror を介して bytes としてコピーする。
///          Detour の版で並びが変わった場合は、サイズの static_assert で build を止める。使っていない欄は 0 にし、
///          同じ状態から同じ bytes を作る。

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <DetourCrowd.h>

namespace mitiru::nav::detail
{

static_assert(sizeof(dtPolyRef) == 4, "面の番号は 32 bit を前提にする (DT_POLYREF64 を使わない)");

/// @brief dtLocalBoundary と同じ並び。MAX_LOCAL_SEGS は 8、MAX_LOCAL_POLYS は 16
struct BoundaryMirror
{
	float center[3];
	struct Segment { float s[6]; float d; } segs[8];
	std::int32_t nsegs;
	dtPolyRef polys[16];
	std::int32_t npolys;
};
static_assert(std::is_standard_layout_v<dtLocalBoundary>);
static_assert(sizeof(BoundaryMirror) == sizeof(dtLocalBoundary), "dtLocalBoundary の並びが変わった (Detour の版を確かめる)");

struct CrowdAgentImage
{
	std::uint32_t index;
	std::uint32_t checkedEpoch;   ///< NavCrowd がこの agent の通路を確かめたときのナビメッシュの epoch
	std::int32_t nneis;
	std::int32_t ncorners;
	std::int32_t npath;           ///< この後ろに続く面の番号の数
	float topologyOptTime, desiredSpeed, targetReplanTime;
	float npos[3], disp[3], dvel[3], nvel[3], vel[3];
	float corridorPos[3], corridorTarget[3], targetPos[3];
	dtPolyRef targetRef;
	std::uint32_t targetPathqRef;
	float radius, height, maxAcceleration, maxSpeed, collisionQueryRange, pathOptimizationRange, separationWeight;
	std::int32_t neiIdx[DT_CROWDAGENT_MAX_NEIGHBOURS];
	float neiDist[DT_CROWDAGENT_MAX_NEIGHBOURS];
	float cornerVerts[DT_CROWDAGENT_MAX_CORNERS * 3];
	dtPolyRef cornerPolys[DT_CROWDAGENT_MAX_CORNERS];
	BoundaryMirror boundary;
	std::uint8_t cornerFlags[DT_CROWDAGENT_MAX_CORNERS];
	std::uint8_t state, partial, targetState, targetReplan;
	std::uint8_t updateFlags, obstacleAvoidanceType, queryFilterType, pending;
};
static_assert(std::is_trivially_copyable_v<CrowdAgentImage>);
static_assert(sizeof(CrowdAgentImage) % 4 == 0);

inline void copy3(float* dst, const float* src) noexcept { std::memcpy(dst, src, sizeof(float) * 3); }

inline void captureBoundary(const dtLocalBoundary& b, BoundaryMirror& out) noexcept
{
	std::memcpy(&out, static_cast<const void*>(&b), sizeof(out));
	const int nsegs = std::clamp<int>(out.nsegs, 0, 8);
	const int npolys = std::clamp<int>(out.npolys, 0, 16);
	std::memset(out.segs + nsegs, 0, sizeof(out.segs[0]) * static_cast<std::size_t>(8 - nsegs));
	std::memset(out.polys + npolys, 0, sizeof(out.polys[0]) * static_cast<std::size_t>(16 - npolys));
}

inline void captureNeighbours(const dtCrowdAgent& a, CrowdAgentImage& out) noexcept
{
	out.nneis = std::clamp(a.nneis, 0, DT_CROWDAGENT_MAX_NEIGHBOURS);
	for (int i = 0; i < out.nneis; ++i)
	{
		out.neiIdx[i] = a.neis[i].idx;
		out.neiDist[i] = a.neis[i].dist;
	}
	out.ncorners = std::clamp(a.ncorners, 0, DT_CROWDAGENT_MAX_CORNERS);
	std::memcpy(out.cornerVerts, a.cornerVerts, sizeof(float) * 3 * static_cast<std::size_t>(out.ncorners));
	std::memcpy(out.cornerFlags, a.cornerFlags, static_cast<std::size_t>(out.ncorners));
	std::memcpy(out.cornerPolys, a.cornerPolys, sizeof(dtPolyRef) * static_cast<std::size_t>(out.ncorners));
}

inline void captureParams(const dtCrowdAgentParams& p, CrowdAgentImage& out) noexcept
{
	out.radius = p.radius; out.height = p.height; out.maxAcceleration = p.maxAcceleration; out.maxSpeed = p.maxSpeed;
	out.collisionQueryRange = p.collisionQueryRange; out.pathOptimizationRange = p.pathOptimizationRange;
	out.separationWeight = p.separationWeight;
	out.updateFlags = p.updateFlags; out.obstacleAvoidanceType = p.obstacleAvoidanceType; out.queryFilterType = p.queryFilterType;
}

/// @brief 目的地を持たない agent では目的地の欄を読まないため、前に使った値を残さず 0 にする
[[nodiscard]] inline CrowdAgentImage captureAgent(const dtCrowdAgent& a, std::uint32_t index) noexcept
{
	CrowdAgentImage out;
	std::memset(&out, 0, sizeof(out));
	out.index = index;
	out.npath = a.corridor.getPathCount();
	out.topologyOptTime = a.topologyOptTime; out.desiredSpeed = a.desiredSpeed; out.targetReplanTime = a.targetReplanTime;
	copy3(out.npos, a.npos); copy3(out.disp, a.disp); copy3(out.dvel, a.dvel); copy3(out.nvel, a.nvel); copy3(out.vel, a.vel);
	copy3(out.corridorPos, a.corridor.getPos()); copy3(out.corridorTarget, a.corridor.getTarget());
	out.state = a.state; out.partial = a.partial ? 1 : 0; out.targetState = a.targetState;
	if (a.targetState != DT_CROWDAGENT_TARGET_NONE)
	{
		copy3(out.targetPos, a.targetPos);
		out.targetRef = a.targetRef;
		out.targetPathqRef = a.targetPathqRef;
		out.targetReplan = a.targetReplan ? 1 : 0;
	}
	captureParams(a.params, out);
	captureNeighbours(a, out);
	captureBoundary(a.boundary, out.boundary);
	return out;
}

inline void restoreAgent(dtCrowdAgent& a, const CrowdAgentImage& in, const dtPolyRef* path) noexcept
{
	a.active = true;
	a.state = in.state; a.partial = in.partial != 0; a.targetState = in.targetState;
	a.corridor.reset(in.npath > 0 ? path[0] : 0, in.corridorPos);
	a.corridor.setCorridor(in.corridorTarget, path, in.npath);
	std::memcpy(static_cast<void*>(&a.boundary), &in.boundary, sizeof(in.boundary));
	a.topologyOptTime = in.topologyOptTime; a.desiredSpeed = in.desiredSpeed; a.targetReplanTime = in.targetReplanTime;
	copy3(a.npos, in.npos); copy3(a.disp, in.disp); copy3(a.dvel, in.dvel); copy3(a.nvel, in.nvel); copy3(a.vel, in.vel);
	copy3(a.targetPos, in.targetPos);
	a.targetRef = in.targetRef; a.targetPathqRef = in.targetPathqRef; a.targetReplan = in.targetReplan != 0;
	dtCrowdAgentParams& p = a.params;
	p.radius = in.radius; p.height = in.height; p.maxAcceleration = in.maxAcceleration; p.maxSpeed = in.maxSpeed;
	p.collisionQueryRange = in.collisionQueryRange; p.pathOptimizationRange = in.pathOptimizationRange;
	p.separationWeight = in.separationWeight;
	p.updateFlags = in.updateFlags; p.obstacleAvoidanceType = in.obstacleAvoidanceType; p.queryFilterType = in.queryFilterType;
	p.userData = nullptr;
	a.nneis = in.nneis;
	for (int i = 0; i < in.nneis; ++i) { a.neis[i].idx = in.neiIdx[i]; a.neis[i].dist = in.neiDist[i]; }
	a.ncorners = in.ncorners;
	std::memcpy(a.cornerVerts, in.cornerVerts, sizeof(in.cornerVerts));
	std::memcpy(a.cornerFlags, in.cornerFlags, sizeof(in.cornerFlags));
	std::memcpy(a.cornerPolys, in.cornerPolys, sizeof(in.cornerPolys));
}

/// @brief 外から来た bytes の数を確かめ、不正な bytes による配列外の読み取りを防ぐ
[[nodiscard]] inline bool plausible(const CrowdAgentImage& in, int maxPath) noexcept
{
	return in.npath >= 0 && in.npath <= maxPath && in.nneis >= 0 && in.nneis <= DT_CROWDAGENT_MAX_NEIGHBOURS
		&& in.ncorners >= 0 && in.ncorners <= DT_CROWDAGENT_MAX_CORNERS && in.boundary.nsegs >= 0 && in.boundary.nsegs <= 8
		&& in.boundary.npolys >= 0 && in.boundary.npolys <= 16;
}

} // namespace mitiru::nav::detail
