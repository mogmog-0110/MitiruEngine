#pragma once

/// @file FluidStick.hpp
/// @brief 粘着の動き。FluidBody.hpp から読み、面に止まった位置、糸、垂れ、広がり、形の遅い戻り、綱の伸びを tick ごとに進める。
/// @details 型は FluidStickTypes.hpp。FluidStepInput::stick が nullptr なら、ここの関数は 1 つも呼ばれず、体は今までと同じビットになる。
///          器の伸びと垂れは FluidContainer.hpp、止まった所のパーティクルを引くばねは FluidStep.hpp が、ここで進めた値を読む。
///          糸の形は FluidThreadShape.hpp の buildFluidThread で作り、手足と同じ面に足す。

#include <algorithm>
#include <cmath>

#include <mitiru/softbody/FluidThreadShape.hpp>

namespace mitiru::softbody::detail
{

/// @brief 綱で伸びている量が tetherPull のこの割合になると、走りの尾 (体の後ろへ伸びる) は消える。尾は綱の向きと逆に伸びて、打ち消し合うため
inline constexpr float kFluidTetherTrailFade = 0.3f;

[[nodiscard]] inline float fluidSmooth01(float x) noexcept
{
	const float u = clamp01(x);
	return u * u * (3.0f - 2.0f * u);
}

[[nodiscard]] inline float fluidApproachTimed(float v, float goal, float tick, float timeUp, float timeDown) noexcept
{
	const float time = goal > v ? timeUp : timeDown;
	return v + (goal - v) * std::fmin(1.0f, tick / std::fmax(time, 1e-4f));
}

/// @brief 変形 now を速い成分と遅い成分に分ける。変形が大きくなるときは遅い成分も追従し、戻るときは遅い成分だけ slowTime をかけて残す。
[[nodiscard]] inline float fluidSlowMemory(float& slow, float now, float tick, const FluidStickParams& k) noexcept
{
	if (k.slowMix <= 0.0f)
	{
		slow = now;
		return now;
	}
	if (std::fabs(now) >= std::fabs(slow) || now * slow < 0.0f) { slow = now; }
	else { slow += (now - slow) * std::fmin(1.0f, tick / std::fmax(k.slowTime, 1e-4f)); }
	return now * (1.0f - k.slowMix) + slow * k.slowMix;
}

[[nodiscard]] inline float fluidStickStill(const FluidStickParams& k, float speed) noexcept
{
	return 1.0f - fluidSmooth01((speed - 0.5f * k.stickSpeed) / std::fmax(k.stickSpeed, 1e-3f));
}

inline void fluidStickClear(FluidStickState& st) noexcept
{
	st.phase = 0;
	st.grip = 0.0f;
	st.away = 0.0f;
	st.tension = 0.0f;
	st.pullAmount = 0.0f;
	st.recoil = 0.0f;
}

/// @brief 面に止まった位置 anchor は、張りついている間は足元から stickSlide より離れず、離れた後も残る。体との距離が stickReach を超えると糸が切れ、先端が体へ戻る。
inline void fluidStickThread(FluidStickState& st, const FluidStickParams& k, const FluidStepInput& in, float still, float tick) noexcept
{
	const bool attached = in.mode != FluidMode::Air;
	const Vec3 up = normalizeOr(in.pose.up);
	const Vec3 foot = in.pose.root;
	if (attached) { st.grip = fluidApproachTimed(st.grip, still, tick, 0.1f, 0.2f); }
	float tension = 0.0f;
	if (st.phase == 0 || st.phase == 2)
	{
		if (st.phase == 2)
		{
			st.tip += (foot - st.tip) * std::fmin(1.0f, tick / std::fmax(k.stickRecoilTime, 1e-4f));
			st.recoil -= tick / std::fmax(1.5f * k.stickRecoilTime, 1e-4f);
			tension = 1.0f;
			if (st.recoil <= 0.0f) { st.phase = 0; }
		}
		if (attached && st.grip > 0.5f && st.phase == 0)
		{
			st.phase = 1;
			st.anchor = foot;
			st.normal = up;
		}
	}
	else if (attached)
	{
		if (up.dot(st.normal) < 0.7f)
		{
			st.anchor = foot;
			st.normal = up;
		}
		const Vec3 d = st.anchor - foot;
		const float len = length(d);
		if (len > 1.5f * k.stickSlide)
		{
			st.phase = 2;
			st.tip = st.anchor;
			st.recoil = 1.0f;
			tension = 1.0f;
		}
		else if (len > k.stickSlide) { st.anchor = foot + d * (k.stickSlide / len); }
	}
	else
	{
		const float len = length(st.anchor - foot);
		if (len > k.stickReach)
		{
			st.phase = 2;
			st.tip = st.anchor;
			st.recoil = 1.0f;
			tension = 1.0f;
		}
		else { tension = clamp01((len - k.stickSlack) / std::fmax(k.stickReach - k.stickSlack, 1e-3f)); }
	}
	const Vec3 away = foot - st.anchor;
	if (length(away) > 1e-3f) { st.dir = normalizeOr(away); }
	st.tension = tension;
	st.away = fluidSmooth01((in.coreVelocity.dot(st.dir) + 1.0f) / 4.0f);
	const float pullGoal = st.phase != 0 ? tension * st.grip * st.away * k.stickPull : 0.0f;
	st.pullAmount = fluidApproachTimed(st.pullAmount, pullGoal, tick, 0.05f, attached ? 0.08f : 0.5f * k.stickRecoilTime);
}

/// @brief 張りついたときの垂れと、床で止まったときの広がりを進める。粘着が切れている間はどちらも 0 のままにする。
inline void fluidStickShape(FluidStickState& st, const FluidStickParams& k, const FluidStepInput& in, float still, float speed, float tick) noexcept
{
	const bool attached = in.mode != FluidMode::Air;
	const float upY = normalizeOr(in.pose.up).y;
	const float sagGoal = attached ? k.sagMax * still * clamp01(1.0f - upY) : 0.0f;
	st.sag = fluidApproachTimed(st.sag, sagGoal, tick, k.sagGrowTime, k.sagReleaseTime);
	const float settled = 1.0f - fluidSmooth01((speed - 0.3f) / 1.2f);
	const float spreadGoal = in.mode == FluidMode::Ground ? settled * clamp01((upY - 0.7f) / 0.3f) : 0.0f;
	st.spread = fluidApproachTimed(st.spread, k.spreadFlatten > 0.0f ? spreadGoal : 0.0f, tick, k.spreadGrowTime, k.spreadReleaseTime);
}

/// @brief 綱の向きへ器を伸ばす量。軽い物でも傾く分 (tetherLean) を残し、綱が無くなると tetherReleaseTime で縮み戻る
inline void fluidTetherUpdate(FluidStickState& st, const FluidStickParams& k, const FluidTether& t, float tick) noexcept
{
	if (k.tetherPull <= 0.0f)
	{
		st.tetherAmount = 0.0f;
		st.tetherSlow = 0.0f;
		return;
	}
	float goal = 0.0f;
	if (t.active != 0)
	{
		const float share = k.tetherLean + (1.0f - k.tetherLean) * clamp01(t.load);
		goal = k.tetherPull * clamp01(t.tension) * share;
		if (length(t.toward) > 1e-3f) { st.tetherToward = normalizeOr(t.toward); }
	}
	st.tetherAmount = fluidApproachTimed(st.tetherAmount, goal, tick, k.tetherGrowTime, k.tetherReleaseTime);
	if (st.tetherAmount < 1e-4f) { st.tetherAmount = 0.0f; }
}

/// @brief 糸の引きと綱の伸びを、走りの尾と足し合わせた 1 本の伸び (f.stretch、f.trailDir) にする。綱が強いと尾は消える
inline void fluidStickStretch(FluidStickState& st, const FluidStickParams& k, FluidFrame& f, float tick) noexcept
{
	const float tether = k.tetherPull > 0.0f ? fluidSlowMemory(st.tetherSlow, st.tetherAmount, tick, k) : 0.0f;
	if (st.pullAmount <= 1e-3f && tether <= 1e-3f) { return; }
	const float trailKeep = tether > 1e-3f ? 1.0f - fluidSmooth01(tether / (kFluidTetherTrailFade * k.tetherPull)) : 1.0f;
	Vec3 tv = f.trailOn != 0 ? f.trailDir * (f.stretch * trailKeep) : Vec3{};
	if (st.pullAmount > 1e-3f) { tv += toLocal(f.body, st.dir) * st.pullAmount; }
	if (tether > 1e-3f) { tv -= toLocal(f.body, st.tetherToward) * tether; }
	const float length3 = length(tv);
	if (length3 > 1e-4f)
	{
		f.stretch = length3;
		f.trailDir = tv / length3;
		f.trailOn = 1;
	}
}

/// @brief link が使えるか。params と state の両方が要る
[[nodiscard]] inline bool fluidStickUsable(const FluidStickLink* link) noexcept
{
	return link != nullptr && link->params != nullptr && link->state != nullptr;
}

inline void fluidStickUpdate(const FluidStickLink& link, const FluidStepInput& in, float speed, float tick) noexcept
{
	FluidStickState& st = *link.state;
	const FluidStickParams& k = *link.params;
	const float still = fluidStickStill(k, speed);
	fluidStickShape(st, k, in, still, speed, tick);
	fluidTetherUpdate(st, k, link.tether, tick);
	if (k.stickReach <= 0.0f)
	{
		fluidStickClear(st);
		return;
	}
	fluidStickThread(st, k, in, still, tick);
}

[[nodiscard]] inline FluidThreadParams fluidStickThreadParams(const FluidStickParams& k, bool withPad) noexcept
{
	FluidThreadParams t;
	t.rootRadius = k.stickRoot;
	t.footRadius = k.stickFoot;
	t.neckRadius = k.stickNeck;
	t.thinPower = k.stickThinPower;
	t.smooth = k.stickSmooth;
	t.bulb = k.stickBulb;
	t.segments = std::isfinite(k.stickSegments) ? static_cast<int>(std::clamp(k.stickSegments, 0.0f, 64.0f)) : 0;
	t.droop = k.stickDroop;
	t.droopPower = k.stickDroopPower;
	t.droopFloor = k.stickDroopFloor;
	t.wobble = k.stickWobble;
	t.sway = k.stickSway;
	t.rootPower = k.stickRootPower;
	t.footPower = k.stickFootPower;
	t.neckShift = k.stickNeckShift;
	t.footPad = withPad ? 2.0f : 0.0f;
	return t;
}

/// @brief 面に付いた所から体の重心へ伸びる糸を、手足の後ろに足す。切れた糸は先が体へ戻る間だけ細くなりながら残る。
///        手足の空きに入る分しか足さない (空きが少ないときは分割が減る)
inline void fluidBuildThread(FluidBodyState& s, const FluidStickLink& link, const Vec3& centroid, const Vec3& foot) noexcept
{
	const FluidStickState& st = *link.state;
	const FluidStickParams& k = *link.params;
	if (k.stickReach <= 0.0f || st.phase == 0) { return; }
	const bool cut = st.phase == 2;
	FluidThreadInput in;
	in.start = centroid;
	in.end = cut ? st.tip : st.anchor;
	in.tension = cut ? 1.0f : st.tension;
	in.time = s.t;
	const float grip = fluidSmooth01((st.grip - 0.15f) / 0.5f);
	const float visible = fluidSmooth01((length(in.end - foot) - 0.15f) / 0.3f);
	in.weight = (cut ? st.recoil : 1.0f) * grip * visible;
	in.surfaceNormal = st.normal;
	const int room = kFluidMaxLimbShapes - s.limbCount;
	if (room <= 0) { return; }
	s.limbCount += buildFluidThread(fluidStickThreadParams(k, !cut), in, s.limbs + s.limbCount, room);
}

} // namespace mitiru::softbody::detail
