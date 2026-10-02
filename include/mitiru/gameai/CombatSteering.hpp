#pragma once

/// @file CombatSteering.hpp
/// @brief 標的との間合いを取る動き (接近、後退、周りを回る、輪への配置)。望む水平速度を返し、
///        CharacterInput::moveVelocity や回避 (Avoidance.hpp) の preferred に渡せる。
/// @details 地面を XZ 平面として Y 成分を捨てる。dir = +1 では atan2(z, x) が増える向きに回る。

#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>

#include <mitiru/action/ActionMath.hpp>
#include <mitiru/gameai/GroundPlane.hpp>

namespace mitiru::gameai
{

struct SpacingConfig
{
	float preferred     = 3.0f;  ///< 保ちたい距離 (m)
	float tolerance     = 0.5f;  ///< preferred からこれ以内なら近づきも下がりもしない
	float approachSpeed = 3.5f;
	float retreatSpeed  = 2.5f;
	float strafeSpeed   = 2.0f;
};

static_assert(std::is_trivially_copyable_v<SpacingConfig>);

/// @brief 標的へ近づく正の速度、または離れる負の速度を返す。不感帯の外では帯からの距離に比例し、tolerance で最大になる。
[[nodiscard]] inline Vec3 spacingVelocity(const Vec3& self, const Vec3& target, const SpacingConfig& c) noexcept
{
	const Vec3  to   = flatten(target - self);
	const float d    = action::length(to);
	const Vec3  dir  = action::normalizeOr(to, Vec3{0.0f, 0.0f, 1.0f});
	const float band = action::maxf(1e-3f, c.tolerance);
	if (d > c.preferred + c.tolerance)
	{
		return dir * (c.approachSpeed * action::saturate((d - c.preferred - c.tolerance) / band));
	}
	if (d < c.preferred - c.tolerance)
	{
		return dir * (-c.retreatSpeed * action::saturate((c.preferred - c.tolerance - d) / band));
	}
	return Vec3{};
}

/// @brief 間合いを保ちながら標的の周りを横歩きする速度を返す。
[[nodiscard]] inline Vec3 circleVelocity(const Vec3& self, const Vec3& target, const SpacingConfig& c, int dir) noexcept
{
	const Vec3 radial  = action::normalizeOr(flatten(self - target), Vec3{1.0f, 0.0f, 0.0f});
	const Vec3 tangent = Vec3{-radial.z, 0.0f, radial.x} * (dir >= 0 ? 1.0f : -1.0f);
	return action::clampLength(tangent * c.strafeSpeed + spacingVelocity(self, target, c),
	                           action::maxf(c.strafeSpeed, action::maxf(c.approachSpeed, c.retreatSpeed)));
}

/// @brief 標的を中心とする輪の slot 番目の点を返す。slot 0 の角度は baseAngle。
[[nodiscard]] inline Vec3 ringSlotPosition(const Vec3& target, float radius, int slot, int slotCount, float baseAngle) noexcept
{
	const float a = baseAngle + 2.0f * action::kPi * static_cast<float>(slot) / static_cast<float>(slotCount < 1 ? 1 : slotCount);
	return Vec3{target.x + std::cos(a) * radius, target.y, target.z + std::sin(a) * radius};
}

/// @brief 標的の周りに並ぶ敵へ等間隔の席を割り当てる。現在の角度順に割り当て、全員の角度の移動量の合計が最小になるように輪を回す。
/// out[i] は positions[i] の席番号で、範囲は 0 から n - 1。
/// @return 席 0 の角度。ringSlotPosition の baseAngle に渡す。
inline float assignRingSlots(std::span<const Vec3> positions, const Vec3& target, std::span<int> out) noexcept
{
	constexpr int kMax = 64;
	const int     n    = static_cast<int>(positions.size() < out.size() ? positions.size() : out.size());
	if (n <= 0 || n > kMax) { return 0.0f; }
	float angle[kMax]{};
	int   order[kMax]{};
	for (int i = 0; i < n; ++i)
	{
		angle[i] = std::atan2(positions[i].z - target.z, positions[i].x - target.x);
		int k = i;
		for (; k > 0 && angle[order[k - 1]] > angle[i]; --k) { order[k] = order[k - 1]; }
		order[k] = i;
	}
	const float step = 2.0f * action::kPi / static_cast<float>(n);
	float sx = 0.0f, sy = 0.0f;
	for (int k = 0; k < n; ++k)
	{
		const float off = angle[order[k]] - step * static_cast<float>(k);
		sx += std::cos(off);
		sy += std::sin(off);
		out[order[k]] = k;
	}
	return std::atan2(sy, sx);
}

} // namespace mitiru::gameai
