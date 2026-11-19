#pragma once

/// @file FluidFrameInterp.hpp
/// @brief コンテナの置き方を、前の tick から今の tick へ substep ごとに補間する (FluidBody.hpp から読む)。
/// @details 前の置き方は呼び手の姿勢か coreVelocity から作り、作業領域に履歴を持たない (巻き戻しても同じビットになる)。

#include <cmath>

namespace mitiru::softbody::detail
{

struct FluidQuat
{
	float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

[[nodiscard]] inline FluidQuat fluidQuatFromBasis(const Basis& b) noexcept
{
	const float m00 = b.c0.x, m01 = b.c1.x, m02 = b.c2.x;
	const float m10 = b.c0.y, m11 = b.c1.y, m12 = b.c2.y;
	const float m20 = b.c0.z, m21 = b.c1.z, m22 = b.c2.z;
	const float trace = m00 + m11 + m22;
	if (trace > 0.0f)
	{
		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		return {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
	}
	if (m00 > m11 && m00 > m22)
	{
		const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
		return {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
	}
	if (m11 > m22)
	{
		const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
		return {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
	}
	const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
	return {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
}

[[nodiscard]] inline Basis fluidBasisFromQuat(const FluidQuat& q) noexcept
{
	const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
	return {Vec3{1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy)},
	        Vec3{2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx)},
	        Vec3{2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy)}};
}

/// 1 tick の間の回転は小さいので、球面補間でなく正規化した線形補間で足りる
[[nodiscard]] inline FluidQuat fluidNlerp(const FluidQuat& a, FluidQuat b, float u) noexcept
{
	if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) { b = {-b.x, -b.y, -b.z, -b.w}; }
	FluidQuat q{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u};
	const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (n < 1e-12f) { return a; }
	const float inv = 1.0f / n;
	return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

/// 前の姿勢が渡されないときの代わり。足元だけを coreVelocity で戻し、向きは今のまま
[[nodiscard]] inline FluidPose fluidEstimatePreviousPose(const FluidPose& pose, const Vec3& velocity, float tick) noexcept
{
	FluidPose before = pose;
	before.root = pose.root - velocity * tick;
	return before;
}

/// end は急停止のずれを含む今のコンテナ、before と after は姿勢だけから決めた置き方 (ずれは補間せず、動きの差だけを end に足す)
struct FluidFrameSweep
{
	FluidPlacement end{};
	FluidPlacement before{};
	FluidPlacement after{};
	FluidQuat bodyBefore{}, bodyAfter{}, groundBefore{}, groundAfter{};
	bool rotates = false;   ///< 向きが同じならクォータニオンを通さない (丸めの誤差を入れない)
};

[[nodiscard]] inline bool fluidSameBasis(const Basis& a, const Basis& b) noexcept
{
	return a.c0.x == b.c0.x && a.c0.y == b.c0.y && a.c0.z == b.c0.z && a.c1.x == b.c1.x && a.c1.y == b.c1.y && a.c1.z == b.c1.z &&
	       a.c2.x == b.c2.x && a.c2.y == b.c2.y && a.c2.z == b.c2.z;
}

[[nodiscard]] inline FluidFrameSweep fluidMakeSweep(const FluidFrame& f, const FluidPlacement& before, const FluidPlacement& after) noexcept
{
	FluidFrameSweep s;
	s.end = {f.ground, f.body, f.groundRoot, f.bodyRoot};
	s.before = before;
	s.after = after;
	s.bodyBefore = fluidQuatFromBasis(before.body);
	s.bodyAfter = fluidQuatFromBasis(after.body);
	s.groundBefore = fluidQuatFromBasis(before.ground);
	s.groundAfter = fluidQuatFromBasis(after.ground);
	s.rotates = !fluidSameBasis(before.body, after.body) || !fluidSameBasis(before.ground, after.ground);
	return s;
}

/// u = 1 は今のコンテナそのものにして、最後の substep が補間を使わないときと同じ置き方になるようにする
inline void fluidApplySweep(FluidFrame& f, const FluidFrameSweep& s, float u) noexcept
{
	if (u >= 1.0f)
	{
		f.ground = s.end.ground;
		f.body = s.end.body;
		f.groundRoot = s.end.groundRoot;
		f.bodyRoot = s.end.bodyRoot;
		return;
	}
	const float back = 1.0f - u;
	f.groundRoot = s.end.groundRoot + (s.before.groundRoot - s.after.groundRoot) * back;
	f.bodyRoot = s.end.bodyRoot + (s.before.bodyRoot - s.after.bodyRoot) * back;
	f.ground = s.end.ground;
	f.body = s.end.body;
	if (!s.rotates) { return; }
	f.ground = fluidBasisFromQuat(fluidNlerp(s.groundBefore, s.groundAfter, u));
	f.body = fluidBasisFromQuat(fluidNlerp(s.bodyBefore, s.bodyAfter, u));
}

} // namespace mitiru::softbody::detail
