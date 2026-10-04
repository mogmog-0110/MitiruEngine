#pragma once

/// @file AnimRetargetAxes.hpp
/// @brief リターゲットで、レスト姿勢の前と上の軸が違う骨格どうしをそろえる 90 度単位の回転。
/// @details 骨格の向きは JSON の "forward" / "up" に軸の名前 ("+Z"、"-Y" など) で書く (AnimRetarget.hpp の parseSkeletonAxes)。どちらの骨格にも書いていなければ、
///          対応した関節のレストの位置が最も合う回転を 24 通りから選ぶ。回転の成分は 0 と ±1 だけなので、
///          掛けても DLL と host で値が変わらない。

#include <cmath>
#include <vector>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>

namespace mitiru::animation
{

/// @brief 骨格の向き: モデル空間でキャラの前と上がどの軸か。既定は +Z が前、+Y が上
struct SkeletonAxes
{
	sgc::Vec3f forward{0, 0, 1};
	sgc::Vec3f up{0, 1, 0};
	bool declared = false;   ///< JSON に書いてあった
};

namespace detail
{

[[nodiscard]] inline float axisComponent(const sgc::Vec3f& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

/// 列が (横, 上, 前) の基底
inline void axesBasis(const SkeletonAxes& a, sgc::Vec3f (&basis)[3])
{
	basis[0] = a.up.cross(a.forward);
	basis[1] = a.up;
	basis[2] = a.forward;
}

} // namespace detail

/// @brief from の向きで置かれた骨格を、to の向きへ回す行列 (from の前を to の前へ、上を上へ)
[[nodiscard]] inline sgc::Mat4f axesRotation(const SkeletonAxes& from, const SkeletonAxes& to)
{
	sgc::Vec3f f[3], t[3];
	detail::axesBasis(from, f);
	detail::axesBasis(to, t);
	sgc::Mat4f m = sgc::Mat4f::identity();
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			float sum = 0.0f;
			for (int k = 0; k < 3; ++k) { sum += detail::axisComponent(t[k], r) * detail::axisComponent(f[k], c); }
			m.m[r][c] = sum;
		}
	}
	return m;
}

namespace detail
{

/// 点の組を、根からの距離の二乗平均が 1 になるように縮める
[[nodiscard]] inline std::vector<sgc::Vec3f> unitSpread(const std::vector<sgc::Vec3f>& points)
{
	float sum = 0.0f;
	for (const auto& p : points) { sum += p.lengthSquared(); }
	const float s = sum > 1e-12f ? 1.0f / std::sqrt(sum / static_cast<float>(points.size())) : 1.0f;
	std::vector<sgc::Vec3f> out;
	out.reserve(points.size());
	for (const auto& p : points) { out.push_back(p * s); }
	return out;
}

[[nodiscard]] inline float spreadError(const int (&perm)[3], const float (&sign)[3], const std::vector<sgc::Vec3f>& a,
                                       const std::vector<sgc::Vec3f>& b)
{
	float err = 0.0f;
	for (std::size_t k = 0; k < a.size(); ++k)
	{
		for (int r = 0; r < 3; ++r)
		{
			const float d = sign[r] * axisComponent(a[k], perm[r]) - axisComponent(b[k], r);
			err += d * d;
		}
	}
	return err;
}

[[nodiscard]] inline sgc::Mat4f signedPermutation(const int (&perm)[3], const float (&sign)[3])
{
	sgc::Mat4f m = sgc::Mat4f::identity();
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c) { m.m[r][c] = c == perm[r] ? sign[r] : 0.0f; }
	}
	return m;
}

} // namespace detail

/// @brief 根からの関節の位置 src (元の骨格) と dst (こちら、同じ順) が最も合う 90 度単位の回転。
///        単位行列の誤差の半分より小さくならなければ単位行列 (関節が 1 列に並ぶなど、向きが決まらない時も)
[[nodiscard]] inline sgc::Mat4f detectAxesRotation(const std::vector<sgc::Vec3f>& src, const std::vector<sgc::Vec3f>& dst)
{
	const auto a = detail::unitSpread(src);
	const auto b = detail::unitSpread(dst);
	static constexpr int kPerms[6][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}, {0, 2, 1}, {2, 1, 0}, {1, 0, 2}};
	const float identityErr = detail::spreadError(kPerms[0], {1, 1, 1}, a, b);
	float bestErr = identityErr;
	sgc::Mat4f best = sgc::Mat4f::identity();
	for (int p = 0; p < 6; ++p)
	{
		for (int s = 0; s < 8; ++s)
		{
			const float sign[3] = {(s & 1) ? -1.0f : 1.0f, (s & 2) ? -1.0f : 1.0f, (s & 4) ? -1.0f : 1.0f};
			const float parity = p < 3 ? 1.0f : -1.0f;   // 前の 3 つは偶置換
			if (parity * sign[0] * sign[1] * sign[2] < 0.0f) { continue; }   // 鏡映は除く
			const float err = detail::spreadError(kPerms[p], sign, a, b);
			if (!(err < bestErr)) { continue; }
			bestErr = err;
			best = detail::signedPermutation(kPerms[p], sign);
		}
	}
	return bestErr < 0.5f * identityErr ? best : sgc::Mat4f::identity();
}

} // namespace mitiru::animation
