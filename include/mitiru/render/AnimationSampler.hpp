#pragma once

/// @file AnimationSampler.hpp
/// @brief glTF アニメーションチャンネルのサンプリングと四元数の基本演算。
/// @details GPU に依存せず、状態を持たない。結果が一意に決まる演算 (加減乗除・sqrt・fmod) だけを使うので、同じ入力からは
///          どの機械でも bit-exact に同じ値を返す (決定論、軸②④)。ノード全体の姿勢の評価・混合・
///          階層の合成は `animation/AnimPose.hpp` が受け持つ。
///          規約は sgc::Mat4f が行優先・列ベクトル (p' = M * p)、quaternion が xyzw。

#include <algorithm>
#include <cmath>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>

#include <mitiru/render/GltfTypes.hpp>

namespace mitiru::render
{

/// @brief 1 ノードの局所 TRS ポーズ。rotation は quaternion xyzw。
struct NodeTRS
{
	sgc::Vec3f t{0, 0, 0};
	sgc::Vec4f r{0, 0, 0, 1};
	sgc::Vec3f s{1, 1, 1};
};

/// @brief quaternion を正規化する (ゼロ長は identity を返す)。
[[nodiscard]] inline sgc::Vec4f quatNormalize(const sgc::Vec4f& q)
{
	const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (len <= 1e-8f) { return {0, 0, 0, 1}; }
	return {q.x / len, q.y / len, q.z / len, q.w / len};
}

/// @brief slerp の重み sin(u θ) / sin θ を cos θ (= x) の多項式で出す。
/// @details sin(u θ) / sin θ を (x - 1) のべき級数にした 8 項 (Eberly 2011)。x が 0.7 以上
///          (2 つの四元数のなす角が 45 度以内) なら float の丸め誤差より小さい。
///          acos / sin を使わないのは、CRT の超越関数が CPU (FMA3 の有無) で結果を変えるため。
///          加減乗算だけなら、同じコードはどの機械でも同じビットを返す。
[[nodiscard]] inline float slerpWeight(float u, float x)
{
	constexpr float kU[8] = {1.0f / (1 * 3), 1.0f / (2 * 5), 1.0f / (3 * 7), 1.0f / (4 * 9),
	                         1.0f / (5 * 11), 1.0f / (6 * 13), 1.0f / (7 * 15), 1.0f / (8 * 17)};
	constexpr float kV[8] = {1.0f / 3, 2.0f / 5, 3.0f / 7, 4.0f / 9, 5.0f / 11, 6.0f / 13, 7.0f / 15, 8.0f / 17};
	const float xm1 = x - 1.0f;
	const float u2 = u * u;
	float acc = 1.0f;
	for (int i = 7; i >= 0; --i)
	{
		acc = 1.0f + (kU[i] * u2 - kV[i]) * xm1 * acc;
	}
	return u * acc;
}

/// @brief cos θ = x の 2 つの単位四元数を slerpWeight で混ぜる (x は 0.7 以上)。
[[nodiscard]] inline sgc::Vec4f slerpNear(const sgc::Vec4f& a, const sgc::Vec4f& b, float x, float u)
{
	const float wa = slerpWeight(1.0f - u, x);
	const float wb = slerpWeight(u, x);
	return quatNormalize({a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

/// @brief 最短弧の球面線形補間。u=0 で a、u=1 で b。結果は正規化して返す。
/// @details なす角が 45 度を超えるときは中点 (a + b を正規化したもの、slerp の u=0.5 そのもの) で
///          半分に割り、多項式の精度が出る範囲で混ぜる。
[[nodiscard]] inline sgc::Vec4f quatSlerp(const sgc::Vec4f& a, const sgc::Vec4f& b, float u)
{
	sgc::Vec4f q2 = b;
	float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (dot < 0.0f)  // 最短弧: 反対半球なら符号反転
	{
		q2 = {-b.x, -b.y, -b.z, -b.w};
		dot = -dot;
	}
	const float x = std::min(dot, 1.0f);
	constexpr float kHalfRange = 0.70710678f;
	if (x >= kHalfRange) { return slerpNear(a, q2, x, u); }
	const auto mid = quatNormalize({a.x + q2.x, a.y + q2.y, a.z + q2.z, a.w + q2.w});
	const float xh = std::sqrt((1.0f + x) * 0.5f);
	return (u < 0.5f) ? slerpNear(a, mid, xh, u * 2.0f) : slerpNear(mid, q2, xh, u * 2.0f - 1.0f);
}

/// @brief 単位 quaternion (xyzw) を回転行列へ変換する (行優先・列ベクトル規約)。
[[nodiscard]] inline sgc::Mat4f quatToMat4(const sgc::Vec4f& q)
{
	const auto n = quatNormalize(q);
	const float x = n.x, y = n.y, z = n.z, w = n.w;
	return {
		1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),     0,
		2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),     0,
		2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y), 0,
		0,                       0,                       0,                       1,
	};
}

/// @brief 局所 TRS から局所行列を組む (T * R * S。glTF 仕様の合成順)。
[[nodiscard]] inline sgc::Mat4f localMatrix(const NodeTRS& trs)
{
	return sgc::Mat4f::translation(trs.t) * quatToMat4(trs.r) * sgc::Mat4f::scaling(trs.s);
}

/// @brief ループ再生の時間の折返し。負の値も折返し、duration<=0 は 0 を返す。
[[nodiscard]] inline float wrapTime(float tSec, float durationSec)
{
	if (durationSec <= 0.0f) { return 0.0f; }
	float r = std::fmod(tSec, durationSec);
	if (r < 0.0f) { r += durationSec; }
	return r;
}

/// @brief glTF 仕様の cubic Hermite (in-tangent/value/out-tangent、区間長でスケール済み)。
///        u=0 で a、u=1 で b。成分ごとに独立適用 (quaternion は呼び出し側で正規化する)。
[[nodiscard]] inline sgc::Vec4f hermite(const sgc::Vec4f& a, const sgc::Vec4f& outTanA,
                                        const sgc::Vec4f& b, const sgc::Vec4f& inTanB,
                                        float u, float span)
{
	const float u2 = u * u;
	const float u3 = u2 * u;
	const float h00 = 2 * u3 - 3 * u2 + 1;
	const float h10 = u3 - 2 * u2 + u;
	const float h01 = -2 * u3 + 3 * u2;
	const float h11 = u3 - u2;
	const auto lerp1 = [&](float p0, float m0, float p1, float m1)
	{
		return h00 * p0 + h10 * span * m0 + h01 * p1 + h11 * span * m1;
	};
	return {lerp1(a.x, outTanA.x, b.x, inTanB.x), lerp1(a.y, outTanA.y, b.y, inTanB.y),
	        lerp1(a.z, outTanA.z, b.z, inTanB.z), lerp1(a.w, outTanA.w, b.w, inTanB.w)};
}

/// @brief チャンネルを時刻 t (wrap 済み) でサンプルする。
/// @details 端より外は端のキーへクランプする。STEP は直前のキーを保持する。CubicSpline は glTF 仕様の
///          Hermite (回転は補間後に正規化)。それ以外の Rotation は slerp、Translation/Scale
///          は成分ごとの lerp。空のチャンネルは identity 相当を返す。
[[nodiscard]] inline sgc::Vec4f sampleChannel(const GltfAnimationChannel& ch, float t)
{
	if (ch.times.empty() || ch.values.empty())
	{
		return (ch.path == GltfAnimPath::Rotation) ? sgc::Vec4f{0, 0, 0, 1}
		                                           : sgc::Vec4f{0, 0, 0, 0};
	}
	const auto it = std::upper_bound(ch.times.begin(), ch.times.end(), t);
	const auto idx = static_cast<std::size_t>(it - ch.times.begin());
	if (idx == 0) { return ch.values.front(); }
	if (idx >= ch.times.size()) { return ch.values.back(); }
	if (ch.interpolation == GltfAnimInterp::Step) { return ch.values[idx - 1]; }

	const float t0 = ch.times[idx - 1];
	const float t1 = ch.times[idx];
	const float span = t1 - t0;
	const float u = (span > 1e-8f) ? (t - t0) / span : 0.0f;
	const auto& a = ch.values[idx - 1];
	const auto& b = ch.values[idx];

	if (ch.interpolation == GltfAnimInterp::CubicSpline &&
	    idx < ch.outTangents.size() && (idx - 1) < ch.outTangents.size() && idx < ch.inTangents.size())
	{
		const auto v = hermite(a, ch.outTangents[idx - 1], b, ch.inTangents[idx], u, span);
		return (ch.path == GltfAnimPath::Rotation) ? quatNormalize(v) : v;
	}

	if (ch.path == GltfAnimPath::Rotation)
	{
		return quatSlerp(quatNormalize(a), quatNormalize(b), u);
	}
	return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
	        a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u};
}

} // namespace mitiru::render
