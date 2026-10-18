#pragma once

/// @file FluidContainer.hpp
/// @brief 器 (体の形の符号付き距離) と、その tick の器の置き方 (FluidBody.hpp から読む)。

#include <cmath>

namespace mitiru::softbody::detail
{

inline constexpr float kFluidDegToRad = 0.017453292519943295f;
inline constexpr float kFluidPi = 3.14159265f;

[[nodiscard]] inline bool fluidOnWall(FluidMode m) noexcept { return m == FluidMode::Wall || m == FluidMode::Stick; }

/// @brief 仮足へ移っていない粒の重心
[[nodiscard]] inline Vec3 fluidBodyCentroid(const FluidBodyState& s) noexcept
{
	Vec3 sum{};
	int n = 0;
	for (int i = 0; i < s.count; ++i)
	{
		if (s.inPod[i] != 0) { continue; }
		sum += s.x[i];
		++n;
	}
	return n > 0 ? sum / static_cast<float>(n) : sum;
}

[[nodiscard]] inline int fluidPodMembers(const FluidBodyState& s) noexcept
{
	int n = 0;
	for (const FluidPod& pod : s.pods) { n += pod.memberCount; }
	return n;
}

[[nodiscard]] inline bool fluidAnyPod(const FluidBodyState& s) noexcept
{
	for (const FluidPod& pod : s.pods)
	{
		if (pod.state != FluidPodState::Off) { return true; }
	}
	return false;
}

/// @brief 掴んでいる仮足 (なければ -1)。引き寄せの間は器をその向きへ伸ばす
[[nodiscard]] inline int fluidGrabbingPod(const FluidBodyState& s) noexcept
{
	for (int k = 0; k < kFluidMaxPods; ++k)
	{
		if (s.pods[k].state == FluidPodState::Grab) { return k; }
	}
	return -1;
}

/// @brief 仮足の根元と先。根元は重心から target へ体の半幅の 6 割だけ出た所
inline void fluidPodAxis(const FluidPod& pod, const Vec3& centroid, float bodyHalfX, Vec3& root, Vec3& tip) noexcept
{
	const Vec3 d = normalizeOr(pod.target - centroid);
	root = centroid + d * (bodyHalfX * 0.6f);
	tip = root + d * pod.length;
}

[[nodiscard]] inline Vec3 fluidShapePoint(const FluidFrame& f, FluidShape::Space space, const Vec3& p) noexcept
{
	if (space == FluidShape::Space::Body) { return f.bodyRoot + toWorld(f.body, p); }
	if (space == FluidShape::Space::Ground) { return f.groundRoot + toWorld(f.ground, p); }
	return p;
}

/// @brief ゲームが器に足す形をワールドの座標へ置く
inline void fluidPlaceShapes(FluidFrame& f, const FluidShape* shapes, int count) noexcept
{
	f.shapeCount = 0;
	for (int k = 0; k < count && k < kFluidMaxShapes; ++k)
	{
		FluidShape w = shapes[k];
		w.a = fluidShapePoint(f, shapes[k].space, shapes[k].a);
		w.b = fluidShapePoint(f, shapes[k].space, shapes[k].b);
		if (shapes[k].space != FluidShape::Space::World)
		{
			const Basis& outer = shapes[k].space == FluidShape::Space::Body ? f.body : f.ground;
			w.axes = {toWorld(outer, w.axes.c0), toWorld(outer, w.axes.c1), toWorld(outer, w.axes.c2)};
		}
		w.space = FluidShape::Space::World;
		f.shapes[f.shapeCount++] = w;
	}
}

/// @brief 急停止の判定 (ならした減速) と、器を伸ばす向き・量を時間をかけて追う。急停止の間は伸ばさない
inline void fluidUpdateTrail(FluidBodyState& s, const FluidParams& k, const FluidFrame& f, float speed, const Vec3& moveDir,
                             float tick, float stretchTarget) noexcept
{
	const float accel = (speed - s.prevSpeed) / std::fmax(tick, 1e-6f);
	s.prevSpeed = speed;
	s.accelSmooth += (accel - s.accelSmooth) * std::fmin(1.0f, tick / k.brakeAccelTime);
	if (s.accelSmooth < -k.brakeAccel && s.mode != FluidMode::Air && speed < k.brakeSpeed) { s.brakeT = s.t; }
	const bool braking = s.t - s.brakeT < k.brakeHold;
	if (speed > 0.5f && !braking)
	{
		const Vec3 want = toLocal(f.body, moveDir);
		const float u = std::fmin(1.0f, tick / k.stretchTurnTime);
		s.moveLocal = length(s.moveLocal) < 1e-6f ? want : normalizeOr(s.moveLocal * (1.0f - u) + want * u);
	}
	const int grab = fluidGrabbingPod(s);
	if (grab >= 0 && speed > 1.0f)
	{
		s.moveLocal = toLocal(f.body, normalizeOr(s.pods[grab].target - f.bodyRoot));
		stretchTarget = k.zipStretch;
	}
	if (braking) { stretchTarget = 0.0f; }
	const float rate = stretchTarget > s.stretch ? 1.0f / k.stretchGrowTime : 1.0f / k.stretchRelaxTime;
	s.stretch += (stretchTarget - s.stretch) * std::fmin(1.0f, rate * tick);
}

/// @brief 急に止まると中身が前へ流れるので、器ごと前へずらしてばねのように戻す
inline void fluidBrakeSlide(FluidBodyState& s, const FluidParams& k, FluidFrame& f, float speed) noexcept
{
	const float u = (s.t - s.brakeT) / k.brakeSlideTime;
	if (u >= 0.0f && u < 1.0f)
	{
		const float slide = k.brakeSlide * std::sin(kFluidPi * u) * std::exp(-1.5f * u);
		f.bodyRoot += toWorld(f.body, s.brakeForward * slide);
	}
	if (s.t - s.brakeT >= k.brakeHold && speed > 0.5f) { s.brakeForward = normalizeOr(Vec3{s.moveLocal.x, 0.0f, s.moveLocal.z}); }
}

/// @brief 器の向きと位置。手足が生えている間は脚の長さだけ体を持ち上げる
inline void fluidPlaceBody(FluidFrame& f, const FluidBodyState& s, const FluidParams& k, const FluidPose& pose, float wheel) noexcept
{
	const Vec3 up = normalizeOr(pose.up);
	const Vec3 fwd = normalizeOr(pose.forward - up * pose.forward.dot(up));
	const Vec3 right = up.cross(fwd);
	f.ground = {right, up, fwd};
	f.groundRoot = pose.root + right * pose.sway;
	Basis b = rotateBasis(f.ground, up, pose.lookYawDeg * kFluidDegToRad);
	b = rotateBasis(b, fwd, -pose.rollDeg * kFluidDegToRad);
	f.body = rotateBasis(b, right, pose.leanDeg * kFluidDegToRad);
	const float lift = k.legLift + (k.wheelLift - k.legLift) * wheel;
	f.bodyRoot = f.groundRoot + up * (pose.bob + lift * s.limbGrow);
}

/// @brief 輪に回す足に切り替わる割合 (たたむ足では 0)
[[nodiscard]] inline float fluidWheelWeight(const FluidParams& k, float speed) noexcept
{
	return k.dashWheel != 0 ? clamp01((speed - k.wheelSpeed) / k.tuckBlendSpeed + 1.0f) : 0.0f;
}

/// @brief 参照実装の frame()。芯の姿勢・速さ・仮足から、その tick の器の置き方を決める (状態の伸び・急停止も進める)
inline void fluidBuildFrame(FluidFrame& f, FluidBodyState& s, const FluidParams& k, const FluidPose& pose, float speed,
                            const Vec3& moveDir, float tick) noexcept
{
	const float wheel = fluidWheelWeight(k, speed);
	fluidPlaceBody(f, s, k, pose, wheel);
	float sy = 1.0f + k.poseSquashShare * (pose.height - 1.0f);
	sy *= 1.0f - std::fmin(k.flattenMax, k.flattenPerSpeed * speed) * (1.0f - wheel);
	const float shrink = std::cbrt(1.0f - static_cast<float>(fluidPodMembers(s)) / static_cast<float>(s.count));
	float spread = 1.0f;
	if (fluidOnWall(s.mode))
	{
		sy *= k.wallFlatten;
		spread = 1.0f + k.wallSpread;
	}
	f.sx = shrink * spread / std::sqrt(sy * spread);
	f.sy = sy * shrink;
	float stretch = std::fmin(k.trailMax, k.trailPerSpeed * speed);
	if (s.mode == FluidMode::Wall) { stretch = std::fmin(k.wallTailMax, k.wallTailPerSpeed * speed); }
	fluidUpdateTrail(s, k, f, speed, moveDir, tick, stretch);
	const float still = s.mode != FluidMode::Air ? std::fmax(0.0f, 1.0f - speed / k.sloshSpeed) : 0.0f;
	f.slosh = still * k.sloshAmp * std::sin(2.0f * kFluidPi * s.t / k.sloshPeriod);
	f.taper = s.mode == FluidMode::Wall ? k.wallTaper : 0.0f;
	f.wave = s.mode == FluidMode::Wall ? k.wallWaveAmp : 0.0f;
	f.halfX = k.bodyHalf.x * f.sx;
	f.stretch = s.stretch;
	Vec3 m = s.moveLocal;
	if (fluidGrabbingPod(s) < 0 && !fluidOnWall(s.mode)) { m.y = 0.0f; }
	f.trailOn = f.stretch > 1e-3f && length(m) > 1e-6f ? 1 : 0;
	f.trailDir = f.trailOn != 0 ? normalizeOr(m) : Vec3{};
	fluidBrakeSlide(s, k, f, speed);
}

/// @brief 4 点の位置 (F4 の x, y, z)
using P4 = F4x3;

[[nodiscard]] inline F4 fluidDot4(const P4& p, const Vec3& v) noexcept
{
	return p.x * F4::splat(v.x) + p.y * F4::splat(v.y) + p.z * F4::splat(v.z);
}

/// @brief 壁の尾のうねり。sin はレーンごとに std::sin で測る
[[nodiscard]] inline F4 fluidWallWave4(const FluidFrame& f, const FluidParams& k, float t, const F4& u) noexcept
{
	float arg[4], out[4];
	(F4::splat(2.0f * kFluidPi * k.wallWaveHz * t) - u * F4::splat(k.wallWaves * 2.0f * kFluidPi)).store(arg);
	for (int l = 0; l < 4; ++l) { out[l] = std::sin(arg[l]); }
	return F4::splat(f.wave) * u * F4::load(out);
}

/// @brief 伸ばした器の座標 l を休みの器の座標へ戻す。下ほど後ろへ引きずり、断面を細くして体積を保つ。
///        壁では頭を丸いまま残し、後ろ半分だけを長い尾に引き伸ばして細くし、うねらせる
[[nodiscard]] inline P4 fluidUntrail4(const FluidFrame& f, const FluidParams& k, float t, const P4& l) noexcept
{
	const Vec3& m = f.trailDir;
	const float bottom = k.bodyCenterY - k.bodyHalf.y;
	const F4 q = fluidDot4(l, m);
	const F4 low = clamp01x4(F4::splat(1.0f) - (l.y - F4::splat(bottom)) / F4::splat(2.0f * k.bodyHalf.y));
	const float base = f.taper > 0.0f ? 1.0f : k.trailBase;
	const F4 st = F4::splat(f.stretch) * (F4::splat(base) + F4::splat(1.0f - base) * low);
	const P4 side{l.x - F4::splat(m.x) * q, l.y - F4::splat(m.y) * q, l.z - F4::splat(m.z) * q};
	if (f.taper <= 0.0f)
	{
		const F4 q2 = (q + st * F4::splat(k.bodyHalf.x)) / (F4::splat(1.0f) + st);
		const F4 s = sqrt(F4::splat(1.0f) + st);
		return {side.x * s + F4::splat(m.x) * q2, side.y * s + F4::splat(m.y) * q2, side.z * s + F4::splat(m.z) * q2};
	}
	const F4 q2 = ifLess(q, F4::splat(0.0f), q / (F4::splat(1.0f) + F4::splat(k.wallTailLength) * st), q);
	const F4 u = pow4(clamp01x4(F4::splat(0.0f) - q2 / F4::splat(k.bodyHalf.x)), k.wallTaperPower);
	const Vec3 lat = normalizeOr(Vec3{0.0f, 1.0f, 0.0f}.cross(m));
	const F4 w = fluidWallWave4(f, k, t, u);
	const F4 thin = F4::splat(1.0f) - F4::splat(f.taper) * u;
	return {side.x / thin - F4::splat(lat.x) * w + F4::splat(m.x) * q2, side.y / thin - F4::splat(lat.y) * w + F4::splat(m.y) * q2,
	        side.z / thin - F4::splat(lat.z) * w + F4::splat(m.z) * q2};
}

/// @brief 超楕円体の距離もどき (sdSuperEllipsoid と同じ式)
[[nodiscard]] inline F4 fluidSuperEllipsoid4(const P4& p, const Vec3& half, float e) noexcept
{
	const F4 s = pow4(abs(p.x) / F4::splat(half.x), e) + pow4(abs(p.y) / F4::splat(half.y), e) + pow4(abs(p.z) / F4::splat(half.z), e);
	return (pow4(s, 1.0f / e) - F4::splat(1.0f)) * F4::splat(std::fmin(half.x, std::fmin(half.y, half.z)));
}

/// @brief 体の超楕円体 (伸び・たぷん・つぶれを掛けたもの)
[[nodiscard]] inline F4 fluidBodyDistance4(const FluidFrame& f, const FluidParams& k, float t, const P4& p) noexcept
{
	const P4 rel{p.x - F4::splat(f.bodyRoot.x), p.y - F4::splat(f.bodyRoot.y), p.z - F4::splat(f.bodyRoot.z)};
	P4 l{fluidDot4(rel, f.body.c0), fluidDot4(rel, f.body.c1), fluidDot4(rel, f.body.c2)};
	if (f.trailOn != 0) { l = fluidUntrail4(f, k, t, l); }
	const float bottom = k.bodyCenterY - k.bodyHalf.y;
	const F4 rise = clamp01x4((l.y - F4::splat(bottom)) / F4::splat(2.0f * k.bodyHalf.y));
	l.x = l.x - F4::splat(f.slosh) * rise * rise;
	const F4 y = (l.y - F4::splat(bottom)) / F4::splat(f.sy) + F4::splat(bottom);
	const P4 q{l.x / F4::splat(f.sx), y - F4::splat(k.bodyCenterY), l.z / F4::splat(f.sx)};
	return fluidSuperEllipsoid4(q, k.bodyHalf, k.bodyExponent) * F4::splat(std::fmin(f.sx, f.sy));
}

[[nodiscard]] inline F4 fluidSmoothMin4(F4 a, F4 b, float k) noexcept
{
	const F4 h = clamp01x4(F4::splat(0.5f) + F4::splat(0.5f) * (b - a) / F4::splat(k));
	return b * (F4::splat(1.0f) - h) + a * h - F4::splat(k) * h * (F4::splat(1.0f) - h);
}

[[nodiscard]] inline F4 fluidCapsule4(const P4& p, const Vec3& a, const Vec3& b, float ra, float rb) noexcept
{
	const Vec3 ba = b - a;
	const float bb = ba.dot(ba);
	const P4 pa{p.x - F4::splat(a.x), p.y - F4::splat(a.y), p.z - F4::splat(a.z)};
	const F4 h = clamp01x4(fluidDot4(pa, ba) / F4::splat(bb > 1e-9f ? bb : 1e-9f));
	const F4 qx = pa.x - F4::splat(ba.x) * h, qy = pa.y - F4::splat(ba.y) * h, qz = pa.z - F4::splat(ba.z) * h;
	return sqrt(qx * qx + qy * qy + qz * qz) - (F4::splat(ra) + F4::splat(rb - ra) * h);
}

/// @brief ゲームが器に足す形の距離。筒はまとめて、ほかはレーンごとに測る
[[nodiscard]] inline F4 fluidShape4(const FluidShape& s, const P4& p) noexcept
{
	if (s.kind == FluidShape::Kind::Capsule) { return fluidCapsule4(p, s.a, s.b, s.ra, s.rb); }
	float x[4], y[4], z[4], out[4];
	p.x.store(x);
	p.y.store(y);
	p.z.store(z);
	for (int l = 0; l < 4; ++l) { out[l] = fluidShapeDistance(s, Vec3{x[l], y[l], z[l]}); }
	return F4::load(out);
}

/// @brief 器の符号付き距離 (外が正) を 4 点まとめて測る。体・ゲームの形・仮足の順に滑らかに足す
[[nodiscard]] inline F4 fluidContainer4(const FluidFrame& f, const FluidParams& k, float t, const P4& p) noexcept
{
	F4 d = fluidBodyDistance4(f, k, t, p);
	for (int i = 0; i < f.shapeCount; ++i) { d = fluidSmoothMin4(d, fluidShape4(f.shapes[i], p), f.shapes[i].fillet); }
	for (int i = 0; i < kFluidMaxPods; ++i)
	{
		if (f.podOn[i] != 0) { d = fluidSmoothMin4(d, fluidCapsule4(p, f.podRoot[i], f.podTip[i], k.podRootRadius, k.podRadius), k.podFillet); }
	}
	return d;
}

/// @brief 4 点の器の距離と外向きの単位法線。正四面体の 4 点の差分で、距離は 4 点の平均
inline void fluidContainerGrad4(const FluidFrame& f, const FluidParams& k, float t, const P4& p, F4& dist, P4& normal) noexcept
{
	constexpr float kEps = 0.01f;
	constexpr float kTet[4][3] = {{1, -1, -1}, {-1, -1, 1}, {-1, 1, -1}, {1, 1, 1}};
	P4 g{F4::splat(0.0f), F4::splat(0.0f), F4::splat(0.0f)};
	F4 sum = F4::splat(0.0f);
	for (const auto& e : kTet)
	{
		const F4 ox = F4::splat(e[0] * kEps), oy = F4::splat(e[1] * kEps), oz = F4::splat(e[2] * kEps);
		const F4 v = fluidContainer4(f, k, t, P4{p.x + ox, p.y + oy, p.z + oz});
		g = {g.x + ox * v, g.y + oy * v, g.z + oz * v};
		sum = sum + v;
	}
	dist = sum * F4::splat(0.25f);
	const F4 len = max(sqrt(g.x * g.x + g.y * g.y + g.z * g.z), F4::splat(1e-12f));
	normal = {g.x / len, g.y / len, g.z / len};
}

} // namespace mitiru::softbody::detail
