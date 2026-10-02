#pragma once

/// @file ActionMath.hpp
/// @brief action ライブラリが共有する小さな数学 (sgc::Vec3f / Quaternionf の上の関数、箱、姿勢、ばね)。
/// @details ここの型はすべて trivially copyable で、GameMemory にそのまま置ける。
///          座標系はエンジンと同じ右手系・Y が上・単位はメートル。

#include <cmath>
#include <cstdint>
#include <type_traits>

#include <sgc/math/Quaternion.hpp>
#include <sgc/math/Vec3.hpp>

namespace mitiru::action
{

using Vec3 = sgc::Vec3f;
using Quat = sgc::Quaternionf;

inline constexpr float kPi       = 3.14159265358979323846f;
inline constexpr float kDegToRad = kPi / 180.0f;

/// @brief 問い合わせの mask。三角形の layer (0-31) のビットを立てたものと & を取る
inline constexpr std::uint32_t kAllLayers = 0xFFFFFFFFu;

[[nodiscard]] constexpr float minf(float a, float b) noexcept { return a < b ? a : b; }
[[nodiscard]] constexpr float maxf(float a, float b) noexcept { return a > b ? a : b; }
[[nodiscard]] constexpr float absf(float a) noexcept { return a < 0.0f ? -a : a; }
[[nodiscard]] constexpr float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
[[nodiscard]] constexpr float saturate(float v) noexcept { return clampf(v, 0.0f, 1.0f); }
[[nodiscard]] constexpr float lerpf(float a, float b, float t) noexcept { return a + (b - a) * t; }

[[nodiscard]] constexpr float dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] constexpr float lengthSq(const Vec3& v) noexcept { return dot(v, v); }
[[nodiscard]] inline float length(const Vec3& v) noexcept { return std::sqrt(lengthSq(v)); }
[[nodiscard]] constexpr Vec3 vmin(const Vec3& a, const Vec3& b) noexcept
{
	return {minf(a.x, b.x), minf(a.y, b.y), minf(a.z, b.z)};
}
[[nodiscard]] constexpr Vec3 vmax(const Vec3& a, const Vec3& b) noexcept
{
	return {maxf(a.x, b.x), maxf(a.y, b.y), maxf(a.z, b.z)};
}
[[nodiscard]] constexpr Vec3 lerp(const Vec3& a, const Vec3& b, float t) noexcept { return a + (b - a) * t; }
[[nodiscard]] constexpr Vec3 projectOnPlane(const Vec3& v, const Vec3& n) noexcept { return v - n * dot(v, n); }

/// @brief 長さがほぼ 0 なら fallback を返す正規化。NaN を作らない
[[nodiscard]] inline Vec3 normalizeOr(const Vec3& v, const Vec3& fallback) noexcept
{
	const float l2 = lengthSq(v);
	if (!(l2 > 1e-20f)) { return fallback; }
	return v * (1.0f / std::sqrt(l2));
}

/// @brief 長さを maxLen 以下に切る
[[nodiscard]] inline Vec3 clampLength(const Vec3& v, float maxLen) noexcept
{
	const float l2 = lengthSq(v);
	if (l2 <= maxLen * maxLen || !(l2 > 1e-20f)) { return v; }
	return v * (maxLen / std::sqrt(l2));
}

[[nodiscard]] inline bool isFinite(const Vec3& v) noexcept
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// @brief n に垂直な単位ベクトルを 1 つ返す
[[nodiscard]] inline Vec3 anyPerpendicular(const Vec3& n) noexcept
{
	const Vec3 a = absf(n.x) < 0.9f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
	return normalizeOr(cross(n, a), Vec3{0.0f, 0.0f, 1.0f});
}

/// @brief フレームレートに依らない指数の寄り係数 (rate は 1 秒あたり)
[[nodiscard]] inline float expDecay(float rate, float dt) noexcept { return 1.0f - std::exp(-rate * dt); }

/// @brief 軸に平行な箱。既定値は空 (何も含まない)
struct Aabb
{
	Vec3 min{1e30f, 1e30f, 1e30f};
	Vec3 max{-1e30f, -1e30f, -1e30f};

	constexpr void expand(const Vec3& p) noexcept { min = vmin(min, p); max = vmax(max, p); }
	constexpr void expand(const Aabb& b) noexcept { min = vmin(min, b.min); max = vmax(max, b.max); }
	[[nodiscard]] constexpr Aabb inflated(const Vec3& r) const noexcept { return {min - r, max + r}; }
	[[nodiscard]] constexpr Vec3 center() const noexcept { return (min + max) * 0.5f; }
	[[nodiscard]] constexpr Vec3 extent() const noexcept { return max - min; }
	[[nodiscard]] constexpr bool overlaps(const Aabb& o) const noexcept
	{
		return min.x <= o.max.x && max.x >= o.min.x && min.y <= o.max.y && max.y >= o.min.y &&
		       min.z <= o.max.z && max.z >= o.min.z;
	}
	[[nodiscard]] constexpr float surfaceArea() const noexcept
	{
		const Vec3 e = extent();
		return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
	}
};

/// @brief 位置と回転 (拡大は持たない)。p' = position + rotation * p
struct Pose
{
	Vec3 position{};
	Quat rotation{};

	[[nodiscard]] constexpr Vec3 apply(const Vec3& p) const noexcept { return position + rotation.rotate(p); }
	[[nodiscard]] constexpr Vec3 applyDir(const Vec3& d) const noexcept { return rotation.rotate(d); }
	[[nodiscard]] constexpr Vec3 inverseApply(const Vec3& p) const noexcept
	{
		return rotation.conjugate().rotate(p - position);
	}
	[[nodiscard]] constexpr Vec3 inverseApplyDir(const Vec3& d) const noexcept
	{
		return rotation.conjugate().rotate(d);
	}
	/// @brief parent * child (child の姿勢を parent の座標系から外へ出す)
	[[nodiscard]] Pose operator*(const Pose& child) const noexcept
	{
		return {apply(child.position), (rotation * child.rotation).normalized()};
	}
};

/// @brief 臨界減衰ばね (Game Programming Gems 4 の SmoothDamp)。smoothTime はおよそ追いつくまでの秒数
struct SpringV3
{
	Vec3 value{};
	Vec3 velocity{};

	constexpr void reset(const Vec3& v) noexcept { value = v; velocity = Vec3{}; }
	Vec3 update(const Vec3& target, float smoothTime, float dt) noexcept
	{
		const float omega  = 2.0f / maxf(1e-4f, smoothTime);
		const float x      = omega * dt;
		const float decay  = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
		const Vec3  change = value - target;
		const Vec3  temp   = (velocity + change * omega) * dt;
		velocity = (velocity - temp * omega) * decay;
		value    = target + (change + temp) * decay;
		return value;
	}
};

/// @brief SpringV3 のスカラー版
struct SpringF
{
	float value    = 0.0f;
	float velocity = 0.0f;

	constexpr void reset(float v) noexcept { value = v; velocity = 0.0f; }
	float update(float target, float smoothTime, float dt) noexcept
	{
		const float omega  = 2.0f / maxf(1e-4f, smoothTime);
		const float x      = omega * dt;
		const float decay  = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
		const float change = value - target;
		const float temp   = (velocity + omega * change) * dt;
		velocity = (velocity - omega * temp) * decay;
		value    = target + (change + temp) * decay;
		return value;
	}
};

static_assert(std::is_trivially_copyable_v<Aabb>);
static_assert(std::is_trivially_copyable_v<Pose>);
static_assert(std::is_trivially_copyable_v<SpringV3>);
static_assert(std::is_trivially_copyable_v<SpringF>);

} // namespace mitiru::action
