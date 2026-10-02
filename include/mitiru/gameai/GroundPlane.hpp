#pragma once

/// @file GroundPlane.hpp
/// @brief 敵の動きは地面 (XZ 平面) の上で扱う。高さを捨てる小さな関数をまとめる

#include <cmath>

#include <mitiru/action/ActionMath.hpp>

namespace mitiru::gameai
{

using Vec3 = action::Vec3;

[[nodiscard]] constexpr Vec3 flatten(const Vec3& v) noexcept { return {v.x, 0.0f, v.z}; }

[[nodiscard]] inline float distanceXZ(const Vec3& a, const Vec3& b) noexcept
{
	const float dx = b.x - a.x, dz = b.z - a.z;
	return std::sqrt(dx * dx + dz * dz);
}

} // namespace mitiru::gameai
