#pragma once

/// @file AnimMath.hpp
/// @brief アニメーションランタイムの四元数と剛体変換の小道具。
/// @details 使う演算は加減乗除と sqrt だけにしている。sqrt は IEEE で正しく丸められるので、
///          同じコードはどの CPU でも同じビットを返す (CRT の sin / acos / atan2 は FMA3 の有無で変わる)。
///          四元数は xyzw、行列は sgc::Mat4f (行優先・列ベクトル、p' = M * p)。

#include <algorithm>
#include <cmath>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>

#include <mitiru/render/AnimationSampler.hpp>

namespace mitiru::animation
{

using render::NodeTRS;
using render::quatNormalize;
using render::quatSlerp;
using render::quatToMat4;
using render::localMatrix;

inline constexpr sgc::Vec4f kQuatIdentity{0, 0, 0, 1};

[[nodiscard]] inline sgc::Vec4f quatMul(const sgc::Vec4f& a, const sgc::Vec4f& b)
{
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
	        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
	        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
	        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

[[nodiscard]] inline sgc::Vec4f quatConj(const sgc::Vec4f& q) { return {-q.x, -q.y, -q.z, q.w}; }

[[nodiscard]] inline float quatDot(const sgc::Vec4f& a, const sgc::Vec4f& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/// @brief 単位四元数で v を回す。
[[nodiscard]] inline sgc::Vec3f quatRotate(const sgc::Vec4f& q, const sgc::Vec3f& v)
{
	const sgc::Vec3f u{q.x, q.y, q.z};
	const sgc::Vec3f t = u.cross(v) * 2.0f;
	return v + t * q.w + u.cross(t);
}

/// @brief 単位ベクトル a を b へ向ける最短の回転。a と b が逆向きなら a に直交する軸で 180 度回す。
[[nodiscard]] inline sgc::Vec4f quatFromTo(const sgc::Vec3f& a, const sgc::Vec3f& b)
{
	const float d = a.dot(b);
	if (d < -0.999999f)
	{
		sgc::Vec3f axis = sgc::Vec3f{1, 0, 0}.cross(a);
		if (axis.lengthSquared() < 1e-12f) { axis = sgc::Vec3f{0, 1, 0}.cross(a); }
		axis = axis.normalized();
		return {axis.x, axis.y, axis.z, 0.0f};
	}
	const sgc::Vec3f c = a.cross(b);
	return quatNormalize({c.x, c.y, c.z, 1.0f + d});
}

/// @brief q の Y 軸まわりの成分 (twist) だけを取り出す。真上・真下を向く回転では identity。
[[nodiscard]] inline sgc::Vec4f quatTwistY(const sgc::Vec4f& q)
{
	const float len2 = q.y * q.y + q.w * q.w;
	if (len2 < 1e-12f) { return kQuatIdentity; }
	const float inv = 1.0f / std::sqrt(len2);
	return {0.0f, q.y * inv, 0.0f, q.w * inv};
}

/// @brief 正規化して符号を揃えた nlerp。重みの小さな混合 (加算レイヤ、IK の効き) に使う。
[[nodiscard]] inline sgc::Vec4f quatNlerp(const sgc::Vec4f& a, const sgc::Vec4f& b, float u)
{
	const float s = quatDot(a, b) < 0.0f ? -1.0f : 1.0f;
	return quatNormalize({a.x + (b.x * s - a.x) * u, a.y + (b.y * s - a.y) * u,
	                      a.z + (b.z * s - a.z) * u, a.w + (b.w * s - a.w) * u});
}

[[nodiscard]] inline sgc::Vec3f lerp3(const sgc::Vec3f& a, const sgc::Vec3f& b, float u)
{
	return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u};
}

/// @brief 回転行列 (各列が正規直交) を四元数へ直す (Shepperd 法)。
[[nodiscard]] inline sgc::Vec4f quatFromRotation(const sgc::Mat4f& m)
{
	const float tr = m.m[0][0] + m.m[1][1] + m.m[2][2];
	if (tr > 0.0f)
	{
		const float s = std::sqrt(tr + 1.0f) * 2.0f;
		return quatNormalize({(m.m[2][1] - m.m[1][2]) / s, (m.m[0][2] - m.m[2][0]) / s,
		                      (m.m[1][0] - m.m[0][1]) / s, 0.25f * s});
	}
	if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2])
	{
		const float s = std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.0f;
		return quatNormalize({0.25f * s, (m.m[0][1] + m.m[1][0]) / s, (m.m[0][2] + m.m[2][0]) / s,
		                      (m.m[2][1] - m.m[1][2]) / s});
	}
	if (m.m[1][1] > m.m[2][2])
	{
		const float s = std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.0f;
		return quatNormalize({(m.m[0][1] + m.m[1][0]) / s, 0.25f * s, (m.m[1][2] + m.m[2][1]) / s,
		                      (m.m[0][2] - m.m[2][0]) / s});
	}
	const float s = std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.0f;
	return quatNormalize({(m.m[0][2] + m.m[2][0]) / s, (m.m[1][2] + m.m[2][1]) / s, 0.25f * s,
	                      (m.m[1][0] - m.m[0][1]) / s});
}

/// @brief アフィン行列を平行移動・回転・スケールへ分ける。せん断は捨てる (骨の行列には出ない前提)。
[[nodiscard]] inline NodeTRS decomposeAffine(const sgc::Mat4f& m)
{
	NodeTRS out;
	out.t = {m.m[0][3], m.m[1][3], m.m[2][3]};
	const sgc::Vec3f c0{m.m[0][0], m.m[1][0], m.m[2][0]};
	const sgc::Vec3f c1{m.m[0][1], m.m[1][1], m.m[2][1]};
	const sgc::Vec3f c2{m.m[0][2], m.m[1][2], m.m[2][2]};
	out.s = {c0.length(), c1.length(), c2.length()};
	const auto safeDiv = [](const sgc::Vec3f& v, float l) { return l > 1e-12f ? v / l : v; };
	const auto n0 = safeDiv(c0, out.s.x);
	const auto n1 = safeDiv(c1, out.s.y);
	const auto n2 = safeDiv(c2, out.s.z);
	sgc::Mat4f r = sgc::Mat4f::identity();
	r.m[0][0] = n0.x; r.m[1][0] = n0.y; r.m[2][0] = n0.z;
	r.m[0][1] = n1.x; r.m[1][1] = n1.y; r.m[2][1] = n1.z;
	r.m[0][2] = n2.x; r.m[1][2] = n2.y; r.m[2][2] = n2.z;
	out.r = quatFromRotation(r);
	return out;
}

/// @brief Y 軸まわりの回転と平行移動だけの剛体変換。ルートモーションの合成に使う。
struct YawXform
{
	sgc::Vec3f t{0, 0, 0};
	sgc::Vec4f r{0, 0, 0, 1};
};

/// @brief a の後に b を (a の座標系で) 続けた変換。
[[nodiscard]] inline YawXform composeYaw(const YawXform& a, const YawXform& b)
{
	return {a.t + quatRotate(a.r, b.t), quatNormalize(quatMul(a.r, b.r))};
}

[[nodiscard]] inline YawXform inverseYaw(const YawXform& a)
{
	const auto ri = quatConj(a.r);
	return {quatRotate(ri, -a.t), ri};
}

} // namespace mitiru::animation
