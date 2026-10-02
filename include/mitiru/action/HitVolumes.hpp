#pragma once

/// @file HitVolumes.hpp
/// @brief 攻撃判定と食らい判定の形 (球、カプセル、回った箱)、ボーンへの取り付け、重なりと tick 間の掃引。
/// @details 判定に使う姿勢はシミュレーションの側で決めた値 (骨のワールド姿勢) を呼び出し側が渡す。
///          GPU スキニングの結果は使わない。形はすべて POD で、GameMemory にも定数データにも置ける。

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <mitiru/action/TriangleTests.hpp>

namespace mitiru::action
{

enum class VolumeKind : std::uint8_t
{
	Sphere,   ///< 中心 a、半径 radius
	Capsule,  ///< 線分 a-b、半径 radius
	Box,      ///< 中心 a、halfExtents、rotation
};

struct HitVolume
{
	Vec3       a{};
	Vec3       b{};
	Vec3       halfExtents{};
	Quat       rotation{};
	float      radius = 0.0f;
	VolumeKind kind   = VolumeKind::Sphere;
	std::uint8_t pad[3]{};

	[[nodiscard]] static constexpr HitVolume sphere(const Vec3& c, float r) noexcept
	{
		HitVolume v;
		v.a = v.b = c;
		v.radius  = r;
		return v;
	}
	[[nodiscard]] static constexpr HitVolume capsule(const Vec3& p, const Vec3& q, float r) noexcept
	{
		HitVolume v;
		v.a      = p;
		v.b      = q;
		v.radius = r;
		v.kind   = VolumeKind::Capsule;
		return v;
	}
	[[nodiscard]] static constexpr HitVolume box(const Vec3& center, const Vec3& half, const Quat& rot = {}) noexcept
	{
		HitVolume v;
		v.a = v.b     = center;
		v.halfExtents = half;
		v.rotation    = rot;
		v.kind        = VolumeKind::Box;
		return v;
	}
	[[nodiscard]] constexpr Vec3 center() const noexcept { return (a + b) * 0.5f; }
};

static_assert(std::is_trivially_copyable_v<HitVolume>);

/// @brief ボーンの座標系で書いた形を、そのボーンのワールド姿勢で置く
[[nodiscard]] inline HitVolume placeVolume(const HitVolume& local, const Pose& bone) noexcept
{
	HitVolume w = local;
	w.a         = bone.apply(local.a);
	w.b         = local.kind == VolumeKind::Capsule ? bone.apply(local.b) : w.a;
	w.rotation  = (bone.rotation * local.rotation).normalized();
	return w;
}

namespace volume_detail
{

/// @brief 箱の 12 本の辺 (頂点番号は CollisionLevel.hpp の boxCorner と同じ並び)
inline constexpr std::uint8_t kBoxEdges[24] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7};

[[nodiscard]] constexpr Vec3 corner(const Vec3& h, int i) noexcept
{
	return {(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z};
}

[[nodiscard]] constexpr Vec3 clampToBox(const Vec3& p, const Vec3& h) noexcept
{
	return {clampf(p.x, -h.x, h.x), clampf(p.y, -h.y, h.y), clampf(p.z, -h.z, h.z)};
}

/// @brief 線分 p-q (箱のローカル) が箱 [-h, h] を通るか (スラブ)
[[nodiscard]] inline bool segmentHitsBox(const Vec3& p, const Vec3& q, const Vec3& h) noexcept
{
	const Aabb box{-h, h};
	float      enter = 0.0f;
	return geom::rayAabb(p, geom::safeInverse(q - p), box, 1.0f, enter);
}

/// @brief 線分と箱の距離の 2 乗 (厳密)。離れていれば最近点は端点か箱の辺のどちらかにある
[[nodiscard]] inline float segmentBoxDistSq(const Vec3& p, const Vec3& q, const Vec3& h, Vec3& onSeg,
                                            Vec3& onBox) noexcept
{
	if (segmentHitsBox(p, q, h))
	{
		onSeg = onBox = clampToBox((p + q) * 0.5f, h);
		return 0.0f;
	}
	onSeg         = p;
	onBox         = clampToBox(p, h);
	float      best = lengthSq(p - onBox);
	const Vec3 cq   = clampToBox(q, h);
	if (lengthSq(q - cq) < best) { onSeg = q; onBox = cq; best = lengthSq(q - cq); }
	for (int e = 0; e < 24; e += 2)
	{
		const geom::SegSegClosest s =
			geom::closestSegmentSegment(p, q, corner(h, kBoxEdges[e]), corner(h, kBoxEdges[e + 1]));
		if (s.distSq < best) { best = s.distSq; onSeg = s.c1; onBox = s.c2; }
	}
	return best;
}

/// @brief 回った箱どうしの分離軸判定 (Ericson 4.4.1、15 軸)
[[nodiscard]] inline bool boxesOverlap(const HitVolume& x, const HitVolume& y) noexcept
{
	const Vec3  ax[3] = {x.rotation.rotate({1, 0, 0}), x.rotation.rotate({0, 1, 0}), x.rotation.rotate({0, 0, 1})};
	const Vec3  by[3] = {y.rotation.rotate({1, 0, 0}), y.rotation.rotate({0, 1, 0}), y.rotation.rotate({0, 0, 1})};
	const float hx[3] = {x.halfExtents.x, x.halfExtents.y, x.halfExtents.z};
	const float hy[3] = {y.halfExtents.x, y.halfExtents.y, y.halfExtents.z};
	const Vec3  t     = y.a - x.a;
	const auto  separated = [&](const Vec3& axis) {
		if (!(lengthSq(axis) > 1e-10f)) { return false; }  // 平行な辺どうしの外積は軸にならない
		float rx = 0.0f, ry = 0.0f;
		for (int i = 0; i < 3; ++i) { rx += hx[i] * absf(dot(ax[i], axis)); ry += hy[i] * absf(dot(by[i], axis)); }
		return absf(dot(t, axis)) > rx + ry;
	};
	for (int i = 0; i < 3; ++i)
	{
		if (separated(ax[i]) || separated(by[i])) { return false; }
	}
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			if (separated(cross(ax[i], by[j]))) { return false; }
		}
	}
	return true;
}

/// @brief 点 p に一番近い、箱の上の点 (ワールド座標)
[[nodiscard]] inline Vec3 closestOnBox(const HitVolume& box, const Vec3& p) noexcept
{
	return box.a + box.rotation.rotate(clampToBox(box.rotation.conjugate().rotate(p - box.a), box.halfExtents));
}

/// @brief 球かカプセル (round) と箱
[[nodiscard]] inline bool roundBoxOverlap(const HitVolume& r, const HitVolume& box, Vec3* contact) noexcept
{
	const Quat inv = box.rotation.conjugate();
	const Vec3 p = inv.rotate(r.a - box.a), q = inv.rotate(r.b - box.a);
	Vec3       onSeg{}, onBox{};
	if (segmentBoxDistSq(p, q, box.halfExtents, onSeg, onBox) > r.radius * r.radius) { return false; }
	if (contact != nullptr) { contact[0] = box.a + box.rotation.rotate((onSeg + onBox) * 0.5f); }
	return true;
}

} // namespace volume_detail

/// @brief 2 つの形が重なるか。重なれば contact に接点 (両者の最近点の中点。箱どうしは互いの中心に近い点の中点) を書く
[[nodiscard]] inline bool overlap(const HitVolume& x, const HitVolume& y, Vec3* contact = nullptr) noexcept
{
	namespace vd = volume_detail;
	const bool xBox = x.kind == VolumeKind::Box, yBox = y.kind == VolumeKind::Box;
	if (xBox && yBox)
	{
		if (!vd::boxesOverlap(x, y)) { return false; }
		if (contact != nullptr) { contact[0] = (vd::closestOnBox(x, y.a) + vd::closestOnBox(y, x.a)) * 0.5f; }
		return true;
	}
	if (xBox) { return vd::roundBoxOverlap(y, x, contact); }
	if (yBox) { return vd::roundBoxOverlap(x, y, contact); }
	const geom::SegSegClosest s = geom::closestSegmentSegment(x.a, x.b, y.a, y.b);
	const float               r = x.radius + y.radius;
	if (s.distSq > r * r) { return false; }
	if (contact != nullptr) { contact[0] = (s.c1 + s.c2) * 0.5f; }
	return true;
}

/// @brief ボーンに付けた形 local が、ボーンの姿勢 previousBone から bone へ動く間に target と重なったか
/// @details 速い振りの取りこぼしを防ぐ。姿勢は位置を線形に、回転を slerp で補う。剣を振る弧はボーンの回転で
///          表すので、端点を線形に結ぶより正しい。回らない球 (弾) は中心の軌跡をカプセルにして厳密に調べる。
///          それ以外は、1 区間の移動 (回転で一番遠い点が描く弧を含む) が太さの半分を超えないよう区間に割り、
///          各区間の形を移動の半分だけ太らせて調べる。取りこぼしは無いが、境目では少し早めに当たることがある。
///          contact には最初に重なった区間の接点を書く。
[[nodiscard]] inline bool sweptOverlap(const HitVolume& local, const Pose& previousBone, const Pose& bone,
                                       const HitVolume& target, Vec3* contact = nullptr) noexcept
{
	const float angle = 2.0f * std::acos(minf(1.0f, absf(previousBone.rotation.dot(bone.rotation))));
	if (local.kind == VolumeKind::Sphere && angle < 1e-4f)
	{
		const HitVolume path = HitVolume::capsule(previousBone.apply(local.a), bone.apply(local.a), local.radius);
		return overlap(path, target, contact);
	}
	const bool  isBox  = local.kind == VolumeKind::Box;
	const float reach  = maxf(length(local.a), length(local.b)) + (isBox ? length(local.halfExtents) : 0.0f);
	const float travel = length(bone.position - previousBone.position) + reach * angle;
	const float thick  = isBox ? minf(local.halfExtents.x, minf(local.halfExtents.y, local.halfExtents.z)) : local.radius;
	const int   n      = static_cast<int>(minf(32.0f, maxf(1.0f, std::ceil(travel / maxf(1e-4f, thick * 0.5f)))));
	const float grow   = travel / static_cast<float>(n) * 0.5f;
	for (int i = 0; i <= n; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(n);
		const Pose  p{lerp(previousBone.position, bone.position, t), Quat::slerp(previousBone.rotation, bone.rotation, t)};
		HitVolume   v = placeVolume(local, p);
		v.radius += grow;
		v.halfExtents = v.halfExtents + Vec3{grow, grow, grow};
		if (overlap(v, target, contact)) { return true; }
	}
	return false;
}

} // namespace mitiru::action
