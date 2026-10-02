#pragma once

/// @file CharacterController_impl.hpp
/// @brief stepCharacter の部品。CharacterController.hpp から読む。

#include <cmath>

namespace mitiru::action::character_detail
{

struct Support
{
	Vec3          normal{0.0f, 1.0f, 0.0f};
	Vec3          point{};
	std::uint32_t body     = kLevelBody;
	std::uint32_t triangle = 0;
	bool          edge     = false;  ///< 面でなく辺か角で支えている (段の縁に足の球が載っている)
};

/// @brief v の n へ食い込む成分だけを消す
[[nodiscard]] inline Vec3 clipInto(const Vec3& v, const Vec3& n) noexcept
{
	const float d = dot(v, n);
	return d < 0.0f ? v - n * d : v;
}

/// @brief v を法線 n の面に沿わせる。長さは保つ (坂を上っても歩く速さが落ちない)
[[nodiscard]] inline Vec3 redirectOntoPlane(const Vec3& v, const Vec3& n) noexcept
{
	const Vec3  p  = projectOnPlane(v, n);
	const float pl = length(p);
	const float vl = length(v);
	return (pl > 1e-7f && vl > 1e-7f) ? p * (vl / pl) : p;
}

[[nodiscard]] inline float cosSlope(const CharacterConfig& c) noexcept { return std::cos(c.maxSlopeDeg * kDegToRad); }

inline void setGround(CharacterState& s, const Support& sup) noexcept
{
	s.grounded       = 1;
	s.groundNormal   = sup.normal;
	s.groundBody     = sup.body;
	s.groundTriangle = sup.triangle;
}

inline void leaveGround(CharacterState& s) noexcept
{
	s.grounded = 0;
	s.events |= charevent::kLeftGround;
	s.velocity += s.platformVelocity;
	s.platformVelocity = Vec3{};
	s.groundBody       = kLevelBody;
}

/// @brief 上が変わって今の地面が坂の上限より急になったら、地面を離れて新しい重力の向きへ落ちる
inline void dropUnwalkableGround(CharacterState& s, const CharacterConfig& c, const Vec3& up) noexcept
{
	if (s.grounded != 0 && !(dot(s.groundNormal, up) >= cosSlope(c))) { leaveGround(s); }
}

/// @brief 足元 feet の少し下で、立てる面 (面法線が坂の上限より緩く、接点が下の半球の側) に触れているか
[[nodiscard]] inline bool findSupport(const CollisionWorld& world, const CharacterConfig& c, const Vec3& feet,
                                      const Vec3& up, Support& out) noexcept
{
	const Capsule cap = characterCapsule(c, feet - up * (c.skinWidth * 1.5f), up);
	Contact       cs[16];
	const int     n    = world.overlapCapsule(cap, cs, 16, c.solidMask);
	const float   cosS = cosSlope(c);
	for (int i = 0; i < n; ++i)
	{
		if (dot(cs[i].surfaceNormal, up) >= cosS && dot(cs[i].point - cap.a, up) <= 0.0f)
		{
			out = Support{cs[i].surfaceNormal, cs[i].point, cs[i].body, cs[i].triangle,
			              dot(cs[i].normal, cs[i].surfaceNormal) < 0.9999f};
			return true;
		}
	}
	return false;
}

/// @brief 平らな段の縁に足の球が載っているとき、足元を段の上面の高さまで上げる
/// @details 球のままだと縁の角を転がり越えるので、階段を上るたびに水平の速さが落ちる。
///          足元を上面に合わせれば角は球の下に入り、平らな床と同じ速さで進む。傾いた面では上げない (面にめり込む)。
[[nodiscard]] inline Vec3 liftOntoLedge(const CollisionWorld& world, const CharacterConfig& c, const Vec3& feet,
                                        const Vec3& up, const Support& sup) noexcept
{
	const float upDot = dot(up, sup.normal);
	if (!sup.edge || !(upDot > 0.98f)) { return feet; }
	const float t = dot(sup.point - feet, sup.normal) / upDot;
	if (!(t > 0.0f) || t > c.radius) { return feet; }
	const float    rise = t + c.skinWidth;
	const SweepHit h    = world.capsuleCast(characterCapsule(c, feet, up), up * rise, c.solidMask);
	return h.hit ? feet : feet + up * rise;
}

inline void carryWithPlatform(CharacterState& s, const CollisionWorld& world, float dt) noexcept
{
	if (s.grounded == 0) { return; }
	if (s.groundBody == kLevelBody) { s.platformVelocity = Vec3{}; return; }
	const Vec3 d = world.carryDelta(s.groundBody, s.position);
	s.position += d;
	s.platformVelocity = d * (1.0f / dt);
}

inline void depenetrate(CharacterState& s, const CharacterConfig& c, const CollisionWorld& world, const Vec3& up) noexcept
{
	for (int it = 0; it < 4; ++it)
	{
		Contact   cs[8];
		const int n = world.overlapCapsule(characterCapsule(c, s.position, up), cs, 8, c.solidMask);
		if (n <= 0 || !(cs[0].depth > 1e-6f)) { return; }
		s.position += cs[0].normal * (cs[0].depth + c.skinWidth * 0.5f);
		s.velocity = clipInto(s.velocity, cs[0].normal);
		s.events |= charevent::kDepenetrated;
	}
}

/// @brief この tick の速度を決める。gravityUp は重力の up 方向の成分。跳んだら true
inline bool integrateVelocity(CharacterState& s, const CharacterConfig& c, const CharacterInput& in, const Vec3& up,
                              float gravityUp, float dt) noexcept
{
	const Vec3 horiz = projectOnPlane(in.moveVelocity, up);
	if (s.grounded != 0)
	{
		if (in.jumpSpeed > 0.0f)
		{
			s.velocity = horiz + up * in.jumpSpeed;
			leaveGround(s);
			s.events |= charevent::kJumped;
			return true;
		}
		s.velocity = redirectOntoPlane(horiz, s.groundNormal);
		return false;
	}
	const float vUp = maxf(dot(s.velocity, up) + gravityUp * dt, -c.maxFallSpeed);
	s.velocity      = horiz + up * vUp;
	return false;
}

/// @brief 立てる面に当たったときの処理。接地中は面に沿って進み、空中なら着地する。立てない面なら false
/// @details 面が緩くても接触の法線が急なとき (段差の角) は false にして、段差の判定 (高さの上限つき) に回す。
///          角に沿って滑らせると、半径までの高さの段を上限なしに上ってしまう。
inline bool handleWalkableHit(CharacterState& s, const SweepHit& hit, Vec3& rem, float cosS, const Vec3& up) noexcept
{
	if (!(dot(hit.surfaceNormal, up) >= cosS) || !(dot(hit.normal, up) >= cosS)) { return false; }
	const Support sup{hit.surfaceNormal, hit.point, hit.body, hit.triangle};
	if (s.grounded != 0)
	{
		rem = redirectOntoPlane(rem, hit.normal);
		setGround(s, sup);
		return true;
	}
	rem        = projectOnPlane(rem, hit.normal);
	s.velocity = clipInto(s.velocity, hit.normal);
	if (dot(s.velocity, up) <= 0.0f) { setGround(s, sup); }
	return true;
}

/// @brief 壁と天井。接地中は水平な向きだけ止めて、壁を這い上がらせない
inline void handleWallHit(CharacterState& s, const SweepHit& hit, Vec3& rem, const Vec3& up) noexcept
{
	const Vec3 n = hit.normal;
	s.events |= dot(n, up) < -0.3f ? charevent::kHitCeiling : charevent::kHitWall;
	Vec3 stop = n;
	if (s.grounded != 0)
	{
		const Vec3 nh = projectOnPlane(n, up);
		if (lengthSq(nh) > 1e-4f) { stop = nh * (1.0f / length(nh)); }
	}
	rem        = clipInto(rem, stop);
	s.velocity = clipInto(s.velocity, stop);
}

/// @brief 段差を上る: 持ち上げ、水平に進め、下ろす。下ろした先が立てる面で、元より高ければ確定する
inline bool tryStepUp(CharacterState& s, const CharacterConfig& c, const CollisionWorld& world, const SweepHit& wall,
                      const Vec3& rem, const Vec3& up) noexcept
{
	const Vec3  horiz = projectOnPlane(rem, up);
	const float hl    = length(horiz);
	if (s.grounded == 0 || !(c.stepHeight > 0.0f) || hl < 1e-6f) { return false; }
	if (dot(wall.point - s.position, up) > c.stepHeight + c.skinWidth) { return false; }

	const SweepHit upHit = world.capsuleCast(characterCapsule(c, s.position, up), up * c.stepHeight, c.solidMask);
	if (upHit.startPenetrating) { return false; }
	const float lift = upHit.hit ? maxf(0.0f, upHit.fraction * c.stepHeight - c.skinWidth) : c.stepHeight;
	const Vec3  p1   = s.position + up * lift;
	const SweepHit fwdHit = world.capsuleCast(characterCapsule(c, p1, up), horiz, c.solidMask);
	if (fwdHit.startPenetrating) { return false; }
	const float fwd = fwdHit.hit ? maxf(0.0f, fwdHit.fraction * hl - c.skinWidth) : hl;
	if (fwd < 1e-5f) { return false; }
	const Vec3     p2      = p1 + horiz * (fwd / hl);
	const float    downLen = lift + c.skinWidth * 2.0f;
	const SweepHit downHit = world.capsuleCast(characterCapsule(c, p2, up), up * -downLen, c.solidMask);
	if (!downHit.hit || downHit.startPenetrating) { return false; }
	const Vec3 p3 = p2 - up * maxf(0.0f, downHit.fraction * downLen - c.skinWidth);
	Support    sup;
	if (dot(p3 - s.position, up) < c.skinWidth * 0.5f || !findSupport(world, c, p3, up, sup)) { return false; }
	if (dot(sup.point - s.position, up) > c.stepHeight + c.skinWidth) { return false; }
	s.position = liftOntoLedge(world, c, p3, up, sup);
	setGround(s, sup);
	s.events |= charevent::kSteppedUp;
	return true;
}

/// @brief delta だけ掃引して滑らせる (最大 maxSlideIterations 回)
inline void slideMove(CharacterState& s, const CharacterConfig& c, const CollisionWorld& world, Vec3 delta,
                      const Vec3& up) noexcept
{
	const float cosS  = cosSlope(c);
	const int   iters = c.maxSlideIterations < 1 ? 1 : c.maxSlideIterations;
	for (int it = 0; it < iters && lengthSq(delta) > 1e-12f; ++it)
	{
		const SweepHit hit = world.capsuleCast(characterCapsule(c, s.position, up), delta, c.solidMask);
		if (!hit.hit) { s.position += delta; return; }
		if (hit.startPenetrating)
		{
			s.position += hit.normal * (hit.penetration + c.skinWidth);
			delta      = clipInto(delta, hit.normal);
			s.velocity = clipInto(s.velocity, hit.normal);
			s.events |= charevent::kDepenetrated;
			continue;
		}
		s.position += delta * hit.fraction + hit.normal * c.skinWidth;
		Vec3 rem = delta * (1.0f - hit.fraction);
		if (!handleWalkableHit(s, hit, rem, cosS, up))
		{
			if (tryStepUp(s, c, world, hit, rem, up)) { return; }
			handleWallHit(s, hit, rem, up);
		}
		delta = rem;
	}
}

/// @brief 下へ掃引して地面に吸い付け、接地を決め直す。keepGrounded なら snapDistance まで吸い付ける
inline void settleOnGround(CharacterState& s, const CharacterConfig& c, const CollisionWorld& world, const Vec3& up,
                           bool keepGrounded) noexcept
{
	if (s.grounded == 0 && dot(s.velocity, up) > 0.0f) { return; }
	const float    reach = (keepGrounded || s.grounded != 0) ? c.snapDistance : c.skinWidth * 2.0f;
	const float    len   = reach + c.skinWidth;
	const SweepHit hit   = world.capsuleCast(characterCapsule(c, s.position, up), up * -len, c.solidMask);
	if (!hit.hit) { s.grounded = 0; return; }
	const Vec3 candidate = hit.startPenetrating ? s.position + hit.normal * (hit.penetration + c.skinWidth)
	                                            : s.position - up * maxf(0.0f, hit.fraction * len - c.skinWidth);
	Support    sup;
	if (!findSupport(world, c, candidate, up, sup)) { s.grounded = 0; return; }
	s.position = liftOntoLedge(world, c, candidate, up, sup);
	setGround(s, sup);
	s.velocity = projectOnPlane(s.velocity, sup.normal);
}

} // namespace mitiru::action::character_detail
