#pragma once

/// @file FluidStep.hpp
/// @brief 流れる体を 1 tick 進める入口と、仮足の操作 (FluidBody.hpp から読む)。

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mitiru::softbody
{

namespace detail
{

inline constexpr float kFluidTwoPi = 6.28318531f;

/// @brief 器が外へ出た粒を押し戻す速さ (Hz)。着地の直後は緩め、時間をかけて戻す
[[nodiscard]] inline float fluidContainerHz(const FluidBodyState& s, const FluidParams& k) noexcept
{
	if (s.mode == FluidMode::Air) { return k.airContainerHz; }
	const float since = s.t - s.landT;
	if (since < k.landContainerDelay) { return k.landContainerHz; }
	const float u = std::fmin(1.0f, (since - k.landContainerDelay) / k.landRecoverTime);
	return k.landContainerHz + (k.containerHz - k.landContainerHz) * u;
}

/// @brief 仮足の粒の行き先。先の粒ほど筒の先に並べ、伸びている間は横にうねらせる
inline void fluidPodGoals(FluidScratch& w, const FluidBodyState& s, const FluidParams& k) noexcept
{
	for (int slot = 0; slot < kFluidMaxPods; ++slot)
	{
		const FluidPod& pod = s.pods[slot];
		const int n = pod.memberCount;
		if (n == 0) { continue; }
		const Vec3 root = w.frame.podRoot[slot], tip = w.frame.podTip[slot];
		const Vec3 side = normalizeOr(normalizeOr(tip - root).cross(Vec3{0.0f, 0.0f, 1.0f}));
		for (int m = 0; m < n; ++m)
		{
			const float u = 1.0f - static_cast<float>(m) / static_cast<float>(n);
			Vec3 goal = root + (tip - root) * u;
			if (pod.state == FluidPodState::Shoot)
			{
				const float phase = kFluidTwoPi * k.podWaveHz * (s.t - pod.born) - u * 6.0f;
				goal += side * (k.podWave * std::sin(phase) * 4.0f * u * (1.0f - u));
			}
			w.podGoal[pod.members[m]] = goal;
		}
	}
}

/// @brief 粒 [begin, end) の器の押し戻し (加速度) を w.push に書く。まず 4 粒ずつ中心の距離を測り、器の内側に 2 cm より
///        深い粒と仮足の粒は 0 にする (正四面体の 4 点の平均も負になるので押し戻しは要らない)。残りを 4 粒ずつ差分で測る
inline void fluidContainerPush(const FluidBodyState& s, FluidScratch& w, const FluidParams& k, float wc, int begin, int end) noexcept
{
	const FluidFrame& f = w.frame;
	int list[kFluidMaxParticles];
	int listCount = 0;
	for (int i = begin; i < end; i += 4)
	{
		int j[4];
		for (int l = 0; l < 4; ++l) { j[l] = std::min(i + l, end - 1); }
		float d[4];
		fluidContainer4(f, k, s.t, fluidGather(w.pos.x, w.pos.y, w.pos.z, j)).store(d);
		for (int l = 0; l < 4 && i + l < end; ++l)
		{
			w.push[i + l] = Vec3{};
			if (s.inPod[i + l] == 0 && d[l] >= -0.02f) { list[listCount++] = i + l; }
		}
	}
	for (int n = 0; n < listCount; n += 4)
	{
		int j[4];
		for (int l = 0; l < 4; ++l) { j[l] = list[std::min(n + l, listCount - 1)]; }
		F4 dist4{};
		F4x3 normal4{};
		fluidContainerGrad4(f, k, s.t, fluidGather(w.pos.x, w.pos.y, w.pos.z, j), dist4, normal4);
		float dist[4], nx[4], ny[4], nz[4];
		dist4.store(dist);
		normal4.x.store(nx);
		normal4.y.store(ny);
		normal4.z.store(nz);
		for (int l = 0; l < 4 && n + l < listCount; ++l)
		{
			if (dist[l] <= 0.0f) { continue; }
			const Vec3 nrm{nx[l], ny[l], nz[l]};
			w.push[j[l]] = nrm * -(wc * wc * dist[l] + 2.0f * k.containerZeta * wc * s.v[j[l]].dot(nrm));
		}
	}
}

/// @brief 引き合い・器・重力・芯への追従・中への引きを足して、予測の位置を出す。位置は w.pos (x と同じ) から読む
inline void fluidPredict(FluidBodyState& s, FluidScratch& w, const FluidParams& k, const FluidStepInput& in, float dt) noexcept
{
	const FluidFrame& f = w.frame;
	const Vec3 g = k.gravity * (s.mode != FluidMode::Air ? k.groundGravityScale : 1.0f);
	const float wc = kFluidTwoPi * fluidContainerHz(s, k);
	const float wpull = kFluidTwoPi * k.centerPullHz * (s.mode == FluidMode::Wall ? k.wallCenterPull : 1.0f);
	const float wpod = kFluidTwoPi * k.podHz;
	const Vec3 center = f.bodyRoot + f.body.c1 * k.bodyCenterY;
	fluidForRanges(w.parallel, s.count, [&](int begin, int end) {
		fluidCohesion(w, k.kernelRadius, k.cohesion, begin, end);
		fluidContainerPush(s, w, k, wc, begin, end);
		for (int i = begin; i < end; ++i)
		{
			Vec3 a{};
			if (s.inPod[i] != 0)
			{
				a = (w.podGoal[i] - s.x[i]) * (wpod * wpod) + (in.coreVelocity - s.v[i]) * (2.0f * k.podZeta * wpod);
			}
			else
			{
				a = w.push[i] + g + (in.coreVelocity - s.v[i]) * k.followRate + w.acc[i];
				a -= (s.x[i] - center) * (wpull * wpull);
			}
			const Vec3 v = s.v[i] + a * dt;
			w.p[i] = s.x[i] + v * dt;
		}
	});
}

/// @brief 掴んでいる仮足の先の粒を target に留める
inline void fluidPinTips(const FluidBodyState& s, FluidScratch& w) noexcept
{
	for (const FluidPod& pod : s.pods)
	{
		if (pod.state == FluidPodState::Grab && pod.memberCount > 0) { w.p[pod.members[0]] = pod.target; }
	}
}

inline void fluidSubstep(FluidBodyState& s, FluidScratch& w, const FluidParams& k, const FluidStepInput& in, float dt) noexcept
{
	const int n = s.count;
	const FluidKernels kk = fluidKernels(k.kernelRadius);
	s.t += dt;
	const Vec3 centroid = fluidBodyCentroid(s);
	for (int slot = 0; slot < kFluidMaxPods; ++slot)
	{
		const FluidPod& pod = s.pods[slot];
		fluidPodAxis(pod, centroid, k.bodyHalf.x, w.frame.podRoot[slot], w.frame.podTip[slot]);
		w.frame.podOn[slot] = pod.state != FluidPodState::Off && pod.length > 0.02f ? 1 : 0;
	}
	fluidPodGoals(w, s, k);
	w.parallel = in.parallel;
	fluidFindNeighbors(w.cache, s.x, n, kk.h, w.pairs, w.parallel);
	fluidToSoa(s.x, n, w.pos);
	fluidPredict(s, w, k, in, dt);
	fluidPinTips(s, w);
	fluidCollide(in, k.collisionRadius, w.p, n);
	fluidFindNeighbors(w.cache, w.p, n, kk.h, w.pairs, w.parallel);
	fluidToSoa(w.p, n, w.pos);
	for (int it = 0; it < k.densityIterations; ++it)
	{
		fluidLambda(w, kk, s.rho0, k.relaxation, n);
		fluidApplyDensity(w, in, kk, s.rho0, k.tensileK, k.collisionRadius, n);
	}
	fluidPinTips(s, w);
	const float invDt = 1.0f / dt;
	for (int i = 0; i < n; ++i)
	{
		s.v[i] = (w.p[i] - s.x[i]) * invDt;
		w.pos.x[i] = w.p[i].x;
		w.pos.y[i] = w.p[i].y;
		w.pos.z[i] = w.p[i].z;
		w.vel.x[i] = s.v[i].x;
		w.vel.y[i] = s.v[i].y;
		w.vel.z[i] = s.v[i].z;
	}
	fluidViscosity(w, kk, s.rho0, k.viscosity, s.v, s.x, n);
}

/// @brief 目と口を置く点を重心の少し上へ 2 次のばねで追わせる。張りついたときは顔を面の外へ起こし、顔の上を世界の上にそろえる
inline void fluidFollowFace(FluidBodyState& s, const FluidParams& k, const FluidPose& pose, float tick) noexcept
{
	const Vec3 target = fluidBodyCentroid(s) + pose.up * k.faceAboveCentroid;
	if (s.faceValid == 0)
	{
		s.face = target;
		s.faceValid = 1;
	}
	const float wf = kFluidTwoPi * k.faceHz;
	const Vec3 a = (target - s.face) * (wf * wf) - s.faceVelocity * (2.0f * k.faceZeta * wf);
	s.faceVelocity += a * tick;
	s.face += s.faceVelocity * tick;
	const Vec3 up = normalizeOr(pose.up);
	const Vec3 fwd = normalizeOr(pose.forward - up * pose.forward.dot(up));
	s.faceForward = fwd;
	s.faceUp = up;
	if (s.mode != FluidMode::Stick) { return; }
	const float tilt = k.stickFaceTiltDeg * kFluidDegToRad;
	s.faceForward = normalizeOr(fwd * std::cos(tilt) + up * std::sin(tilt));
	const Vec3 worldUp{0.0f, 1.0f, 0.0f};
	Vec3 faceUp = worldUp - s.faceForward * worldUp.dot(s.faceForward);
	if (length(faceUp) < 0.2f) { faceUp = up - s.faceForward * up.dot(s.faceForward); }
	s.faceUp = normalizeOr(faceUp);
}

/// @brief 仮足を伸ばし縮めし、長さに合わせて粒を筒へ移す (根元に近い粒から)。戻りきった仮足は外す
inline void fluidUpdatePod(FluidBodyState& s, const FluidParams& k, FluidPod& pod, float tick) noexcept
{
	if (pod.state == FluidPodState::Off) { return; }
	Vec3 root{}, tip{};
	fluidPodAxis(pod, fluidBodyCentroid(s), k.bodyHalf.x, root, tip);
	const float dist = length(pod.target - root);
	if (pod.state == FluidPodState::Shoot)
	{
		pod.length = std::fmin(dist, pod.length + k.podSpeed * tick);
		if (pod.length >= dist - 1e-3f) { pod.state = FluidPodState::Grab; }
	}
	else if (pod.state == FluidPodState::Grab) { pod.length = std::fmax(0.0f, dist); }
	else { pod.length = std::fmax(0.0f, pod.length - k.podSpeed * 0.5f * tick); }
	const float byLength = pod.length / k.podSpacing;
	const float byShare = k.podMaxShare * static_cast<float>(s.count);
	const int want = std::min(static_cast<int>(std::fmin(byShare, byLength)), kFluidMaxPodMembers);
	while (pod.memberCount < want)
	{
		int best = -1;
		float bestD = 0.0f;
		for (int i = 0; i < s.count; ++i)
		{
			if (s.inPod[i] != 0) { continue; }
			const Vec3 d = s.x[i] - root;
			const float d2 = d.dot(d);
			if (best < 0 || d2 < bestD) { best = i; bestD = d2; }
		}
		if (best < 0) { break; }
		pod.members[pod.memberCount++] = static_cast<std::uint16_t>(best);
		s.inPod[best] = 1;
	}
	while (pod.memberCount > want) { s.inPod[pod.members[--pod.memberCount]] = 0; }
	if (pod.state == FluidPodState::Release && pod.memberCount == 0) { pod = FluidPod{}; }
}

inline void fluidClearPod(FluidBodyState& s, FluidPod& pod) noexcept
{
	for (int m = 0; m < pod.memberCount; ++m) { s.inPod[pod.members[m]] = 0; }
	pod = FluidPod{};
}

/// @brief 芯の状態の切り替え (着地の時刻を覚える)
inline void fluidSetMode(FluidBodyState& s, FluidMode mode) noexcept
{
	if (mode != FluidMode::Air && s.mode == FluidMode::Air) { s.landT = s.t; }
	s.mode = mode;
}

} // namespace detail

/// @brief 休みの器を間隔 particleSpacing の格子で埋める (少し揺らす)。並べた数を返す
[[nodiscard]] inline int fluidSampleRest(const FluidParams& k, Vec3* out, int capacity, std::uint32_t seed = 3u) noexcept
{
	std::uint64_t state = 0x853C49E6748FEA9Bull ^ seed;
	const auto uniform = [&state](float lo, float hi) {
		state = state * 6364136223846793005ull + 1442695040888963407ull;
		const auto bits = static_cast<std::uint32_t>(state >> 40);
		return lo + (hi - lo) * (static_cast<float>(bits) / 16777216.0f);
	};
	const float sp = k.particleSpacing;
	const Vec3& h = k.bodyHalf;
	const auto steps = [sp](float half) { return static_cast<int>(std::ceil((2.0f * half - sp * 0.5f) / sp)); };
	int n = 0;
	for (int ix = 0; ix < steps(h.x); ++ix)
	{
		for (int iy = 0; iy < steps(h.y); ++iy)
		{
			for (int iz = 0; iz < steps(h.z); ++iz)
			{
				const Vec3 c = Vec3{-h.x, -h.y, -h.z} + Vec3{static_cast<float>(ix) + 0.5f, static_cast<float>(iy) + 0.5f, static_cast<float>(iz) + 0.5f} * sp;
				const Vec3 g = c + Vec3{uniform(-0.08f, 0.08f), uniform(-0.08f, 0.08f), uniform(-0.08f, 0.08f)} * sp;
				if (n < capacity && sdSuperEllipsoid(g, h, k.bodyExponent) < -0.02f) { out[n++] = g + Vec3{0.0f, k.bodyCenterY, 0.0f}; }
			}
		}
	}
	return n;
}

/// @brief 休みの位置 rest (足元が原点、y が上、z が前) で体を作り、姿勢 pose の上に置く。休みの密度もここで決める
inline void fluidReset(FluidBodyState& s, const FluidParams& k, const Vec3* rest, int count, const FluidPose& pose, FluidScratch& w) noexcept
{
	s = FluidBodyState{};
	s.count = std::clamp(count, 0, kFluidMaxParticles);
	const detail::FluidKernels kk = detail::fluidKernels(k.kernelRadius);
	detail::fluidBuildNeighbors(w.cache.grid, rest, s.count, kk.h, w.pairs);
	w.cache.refCount = -1;
	float sorted[kFluidMaxParticles];
	std::fill(sorted, sorted + s.count, kk.w0);
	for (int i = 0; i < s.count; ++i)
	{
		for (int e = w.pairs.rowStart[i]; e < w.pairs.rowStart[i + 1]; ++e)
		{
			const Vec3 d = rest[i] - rest[w.pairs.j[e]];
			sorted[i] += detail::fluidPoly6(kk, d.dot(d));
		}
	}
	std::sort(sorted, sorted + s.count);
	const float at = 0.75f * static_cast<float>(s.count - 1);
	const int lo = static_cast<int>(at);
	const int hi = std::min(lo + 1, s.count - 1);
	s.rho0 = k.restDensityShare * (sorted[lo] + (sorted[hi] - sorted[lo]) * (at - static_cast<float>(lo)));
	detail::fluidPlaceBody(w.frame, s, k, pose, 0.0f);
	for (int i = 0; i < s.count; ++i) { s.x[i] = w.frame.bodyRoot + toWorld(w.frame.body, rest[i]); }
	s.initialized = 1;
}

/// @brief 体を tick 秒進める。小さな刻み (substepHz) に分け、手足・顔・仮足を最後に進める
inline void fluidStep(FluidBodyState& s, const FluidParams& k, const FluidStepInput& in, float tick, FluidScratch& w) noexcept
{
	if (s.initialized == 0 || s.count == 0 || tick <= 0.0f) { return; }
	const Vec3 up = normalizeOr(in.pose.up);
	const Vec3 plane = in.coreVelocity - up * in.coreVelocity.dot(up);
	const float speed = length(plane);
	const Vec3 moveDir = speed > 1e-3f ? plane / speed : Vec3{};
	detail::fluidSetMode(s, in.mode);
	detail::fluidLimbSpring(s, k, detail::fluidLimbTarget(s, k, speed), tick);
	detail::fluidBuildFrame(w.frame, s, k, in.pose, speed, moveDir, tick);
	detail::fluidPlaceShapes(w.frame, in.shapes, in.shapeCount);
	std::copy(s.x, s.x + s.count, s.xStepStart);
	std::copy(s.limbs, s.limbs + s.limbCount, s.limbsBefore);
	s.limbCountBefore = s.limbCount;
	const int sub = std::max(1, static_cast<int>(std::lround(tick * k.substepHz)));
	for (int i = 0; i < sub; ++i) { detail::fluidSubstep(s, w, k, in, tick / static_cast<float>(sub)); }
	const FluidFrame& f = w.frame;
	const Vec3 center = f.bodyRoot + toWorld(f.body, Vec3{0.0f, k.bodyCenterY - k.bodyHalf.y + k.bodyHalf.y * f.sy, 0.0f});
	detail::fluidBuildLimbs(s, k, f, in.pose, in.phase, in.cadence, speed, detail::fluidBodyCentroid(s) - center);
	detail::fluidFollowFace(s, k, in.pose, tick);
	for (FluidPod& pod : s.pods) { detail::fluidUpdatePod(s, k, pod, tick); }
}

/// @brief 仮足 slot を target へ撃つ。同じ slot の前の仮足は消す
inline void fluidPodShoot(FluidBodyState& s, int slot, const Vec3& target) noexcept
{
	if (slot < 0 || slot >= kFluidMaxPods) { return; }
	FluidPod& pod = s.pods[slot];
	detail::fluidClearPod(s, pod);
	pod.state = FluidPodState::Shoot;
	pod.target = target;
	pod.born = s.t;
}

/// @brief 仮足 slot の状態を変える (掴む・戻す)。伸びていない slot には何もしない
inline void fluidPodSetState(FluidBodyState& s, int slot, FluidPodState state) noexcept
{
	if (slot < 0 || slot >= kFluidMaxPods || s.pods[slot].state == FluidPodState::Off) { return; }
	s.pods[slot].state = state;
}

/// @brief 仮足へ移っていない粒の重心
[[nodiscard]] inline Vec3 fluidCentroid(const FluidBodyState& s) noexcept { return detail::fluidBodyCentroid(s); }

} // namespace mitiru::softbody
