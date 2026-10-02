#pragma once

/// @file NavObstacle.hpp
/// @brief ナビメッシュから除く障害物 (扉、壊せる壁、置いた箱)を表す、DynamicNavMesh 用の固定長 POD

#include <cstdint>
#include <type_traits>

#include "sgc/math/Vec3.hpp"

namespace mitiru::nav
{

enum class NavObstacleShape : std::uint8_t
{
	None,
	Box,        ///< center と halfExtents の箱。yaw で Y 回りに回す
	Cylinder,   ///< 半径 halfExtents.x、高さ 2 * halfExtents.y の円柱。center は高さの真ん中
};

/// @brief 寸法には物の見た目の大きさを指定する。床の縁と同じく agent の体幅を含めるため、反映時に
///        ナビメッシュを焼いたときの半径分だけ広げる
struct NavObstacle
{
	sgc::Vec3f center{};
	sgc::Vec3f halfExtents{};
	float yaw = 0.0f;   ///< rad
	NavObstacleShape shape = NavObstacleShape::None;
	std::uint8_t enabled = 0;
	std::uint8_t pad[2]{};

	[[nodiscard]] static NavObstacle box(const sgc::Vec3f& center, const sgc::Vec3f& halfExtents, float yaw = 0.0f) noexcept
	{
		NavObstacle o;
		o.center = center;
		o.halfExtents = halfExtents;
		o.yaw = yaw;
		o.shape = NavObstacleShape::Box;
		o.enabled = 1;
		return o;
	}

	[[nodiscard]] static NavObstacle cylinder(const sgc::Vec3f& center, float radius, float height) noexcept
	{
		NavObstacle o;
		o.center = center;
		o.halfExtents = sgc::Vec3f{radius, height * 0.5f, radius};
		o.shape = NavObstacleShape::Cylinder;
		o.enabled = 1;
		return o;
	}

	[[nodiscard]] bool active() const noexcept { return enabled != 0 && shape != NavObstacleShape::None; }
};

static_assert(std::is_trivially_copyable_v<NavObstacle>);
static_assert(sizeof(NavObstacle) == 32, "NavObstacle は詰め物の無い 32 byte (bytes を比べて違いを見る)");

} // namespace mitiru::nav
